/**
 * src/fs.c - Lazy Loading Filesystem (Load-On-Demand)
 * Only loads nodes from disk when accessed, not at boot
 */

#include "../include/fs.h"
#include "../include/string.h"
#include "../include/memory.h"
#include "../include/console.h"
#include "../include/ata.h"
#include "../include/task.h"

// --- Configuration ---
#define FS_MAGIC         0xEF5342
#define FS_ROOT_ID       1
#define SECTOR_SIZE      512

// --- NEW: Cache Management ---
#define FS_CACHE_SIZE    32  // Keep 32 nodes in RAM (adjustable)

typedef struct {
    fs_node_t node;          // The actual node data
    uint32_t  id;            // Node ID (0 = empty slot)
    uint32_t  last_access;   // For LRU eviction
    uint8_t   dirty;         // 1 = modified, needs write-back
} fs_cache_entry_t;

// --- Data Structures ---
typedef struct {
    uint32_t magic;
    uint32_t root_inode;
    uint32_t total_inodes;
    uint32_t total_blocks;
    uint32_t free_inodes;
    uint32_t free_blocks;
    uint8_t  reserved[488];
} superblock_t;

// --- Globals ---
uint32_t fs_root_id = FS_ROOT_ID;
uint32_t fs_current_dir_id = FS_ROOT_ID;

static superblock_t sb;
static fs_cache_entry_t cache[FS_CACHE_SIZE];  // Cache instead of full table!
static uint32_t access_counter = 0;            // For LRU tracking

// Helper macros
#define INODE_TO_SECTOR(id) (FS_INODE_TABLE_START + ((id) / (SECTOR_SIZE / sizeof(inode_t))))
#define INODE_OFF_IN_SECTOR(id) (((id) % (SECTOR_SIZE / sizeof(inode_t))) * sizeof(inode_t))

// --- Bitmap Helpers ---
static int bitmap_get(uint32_t sector_start, uint32_t bit) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t sector = sector_start + (bit / (SECTOR_SIZE * 8));
    uint32_t bit_off = bit % (SECTOR_SIZE * 8);
    
    ata_read_sectors(sector, 1, buf);
    return (buf[bit_off / 8] & (1 << (bit_off % 8))) != 0;
}

static void bitmap_set(uint32_t sector_start, uint32_t bit, int value) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t sector = sector_start + (bit / (SECTOR_SIZE * 8));
    uint32_t bit_off = bit % (SECTOR_SIZE * 8);
    
    ata_read_sectors(sector, 1, buf);
    if (value) buf[bit_off / 8] |= (1 << (bit_off % 8));
    else buf[bit_off / 8] &= ~(1 << (bit_off % 8));
    ata_write_sectors(sector, 1, buf);
}

static int bitmap_find_free(uint32_t sector_start, uint32_t count) {
    uint8_t buf[SECTOR_SIZE];
    for (uint32_t s = 0; s < (count + 4095) / 4096; s++) {
        ata_read_sectors(sector_start + s, 1, buf);
        for (int i = 0; i < SECTOR_SIZE; i++) {
            if (buf[i] != 0xFF) {
                for (int b = 0; b < 8; b++) {
                    if (!(buf[i] & (1 << b))) {
                        uint32_t res = (s * 4096) + (i * 8) + b;
                        if (res < count) return res;
                    }
                }
            }
        }
    }
    return -1;
}

// --- Internal Helpers ---

static void save_superblock() {
    ata_write_sectors(FS_SUPERBLOCK_SECTOR, 1, &sb);
}

static void save_node(uint32_t id);

// --- Allocation ---
static int inode_alloc() {
    int id = bitmap_find_free(FS_INODE_BITMAP_SECTOR, FS_MAX_INODES);
    if (id < 0) return -1;
    bitmap_set(FS_INODE_BITMAP_SECTOR, id, 1);
    sb.free_inodes--;
    save_superblock();
    return id;
}

static int block_alloc() {
    int id = bitmap_find_free(FS_BLOCK_BITMAP_START, sb.total_blocks);
    if (id < 0) return -1;
    bitmap_set(FS_BLOCK_BITMAP_START, id, 1);
    sb.free_blocks--;
    save_superblock();
    
    // Zero out the newly allocated block
    uint8_t zero[SECTOR_SIZE];
    memset(zero, 0, SECTOR_SIZE);
    ata_write_sectors(id, 1, zero);
    
    return id;
}

static void block_free(uint32_t id) {
    if (id < FS_DATA_BLOCKS_START) return;
    bitmap_set(FS_BLOCK_BITMAP_START, id, 0);
    sb.free_blocks++;
    save_superblock();
}
static int cache_find(uint32_t id) {
    for (int i = 0; i < FS_CACHE_SIZE; i++) {
        if (cache[i].id == id) {
            cache[i].last_access = ++access_counter;  // Update LRU
            return i;
        }
    }
    return -1;
}

/**
 * @brief Finds an empty slot in cache, or evicts least recently used
 * @return Index of available slot
 */
static int cache_find_slot() {
    // First, try to find an empty slot
    for (int i = 0; i < FS_CACHE_SIZE; i++) {
        if (cache[i].id == 0) {
            return i;
        }
    }

    // No empty slots - find LRU
    int lru_index = 0;
    uint32_t oldest_access = cache[0].last_access;

    for (int i = 1; i < FS_CACHE_SIZE; i++) {
        if (cache[i].last_access < oldest_access) {
            oldest_access = cache[i].last_access;
            lru_index = i;
        }
    }

    if (cache[lru_index].dirty) {
        save_node(cache[lru_index].id);
    }

    return lru_index;
}

/**
 * @brief Loads an inode from disk into cache
 * @return Pointer to cached inode, or NULL on error
 */
static inode_t* cache_load(uint32_t id) {
    if (id == 0 || id >= FS_MAX_INODES) return 0;

    int idx = cache_find(id);
    if (idx >= 0) return &cache[idx].node;

    int slot = cache_find_slot();
    
    // Read the sector containing this inode
    uint8_t sector_buf[SECTOR_SIZE];
    ata_read_sectors(INODE_TO_SECTOR(id), 1, sector_buf);
    
    // Copy only the 128-byte inode
    memcpy(&cache[slot].node, sector_buf + INODE_OFF_IN_SECTOR(id), sizeof(inode_t));

    if (cache[slot].node.id != id) {
        return 0; // Empty or mismatch
    }

    cache[slot].id = id;
    cache[slot].dirty = 0;
    cache[slot].last_access = ++access_counter;

    return &cache[slot].node;
}

/**
 * @brief Marks a cached node as dirty (needs write-back)
 */
static void cache_mark_dirty(uint32_t id) {
    int idx = cache_find(id);
    if (idx >= 0) {
        cache[idx].dirty = 1;
    }
}

/**
 * @brief Writes a node to disk and updates cache
 */
static void save_node(uint32_t id) {
    if (id == 0 || id >= FS_MAX_INODES) return;

    int idx = cache_find(id);
    inode_t* node_ptr = 0;

    if (idx >= 0) {
        node_ptr = &cache[idx].node;
    } else {
        node_ptr = cache_load(id);
    }

    if (!node_ptr) return;

    // Read the whole sector, update the inode's part, then write back
    uint8_t sector_buf[SECTOR_SIZE];
    ata_read_sectors(INODE_TO_SECTOR(id), 1, sector_buf);
    memcpy(sector_buf + INODE_OFF_IN_SECTOR(id), node_ptr, sizeof(inode_t));
    ata_write_sectors(INODE_TO_SECTOR(id), 1, sector_buf);

    if (idx >= 0) cache[idx].dirty = 0;
}

/**
 * @brief Flushes all dirty nodes to disk
 */
void fs_sync() {
    for (int i = 0; i < FS_CACHE_SIZE; i++) {
        if (cache[i].id != 0 && cache[i].dirty) {
            save_node(cache[i].id);
        }
    }
    console_print_colored("FS: Cache synced to disk.\n", COLOR_GREEN_ON_BLACK);
}

// Formats the disk
static void mkfs() {
    console_print_colored("FS: Formatting drive (UNIX-Lite)...\n", COLOR_YELLOW_ON_BLACK);

    // 1. Clear Bitmaps and Inode Table
    uint8_t zero_sector[SECTOR_SIZE];
    memset(zero_sector, 0, SECTOR_SIZE);

    // Clear Inode Bitmap
    ata_write_sectors(FS_INODE_BITMAP_SECTOR, 1, zero_sector);

    // Clear Block Bitmaps
    for (int i = 0; i < FS_BLOCK_BITMAP_COUNT; i++) {
        ata_write_sectors(FS_BLOCK_BITMAP_START + i, 1, zero_sector);
    }

    // Clear Inode Table
    for (int i = 0; i < FS_INODE_TABLE_COUNT; i++) {
        ata_write_sectors(FS_INODE_TABLE_START + i, 1, zero_sector);
    }

    // 2. Setup Superblock
    memset(&sb, 0, sizeof(sb));
    sb.magic = FS_MAGIC;
    sb.root_inode = FS_ROOT_ID;
    sb.total_inodes = FS_MAX_INODES;
    sb.total_blocks = 102400; // 50MB
    sb.free_inodes = FS_MAX_INODES;
    sb.free_blocks = sb.total_blocks - FS_DATA_BLOCKS_START;

    save_superblock();

    // 3. Mark reserved blocks and inodes 0 as used
    bitmap_set(FS_INODE_BITMAP_SECTOR, 0, 1); // Inode 0 reserved
    for (int i = 0; i < FS_DATA_BLOCKS_START; i++) {
        bitmap_set(FS_BLOCK_BITMAP_START, i, 1);
    }

    // 4. Create Root Inode
    inode_t root;
    memset(&root, 0, sizeof(inode_t));
    root.id = FS_ROOT_ID;
    root.type = FS_TYPE_DIRECTORY;
    root.mode = 0755;
    root.size = 0;
    root.link_count = 1;

    // Save Root Inode
    uint8_t buf[SECTOR_SIZE];
    ata_read_sectors(INODE_TO_SECTOR(FS_ROOT_ID), 1, buf);
    memcpy(buf + INODE_OFF_IN_SECTOR(FS_ROOT_ID), &root, sizeof(inode_t));
    ata_write_sectors(INODE_TO_SECTOR(FS_ROOT_ID), 1, buf);
    
    bitmap_set(FS_INODE_BITMAP_SECTOR, FS_ROOT_ID, 1);
    sb.free_inodes--;
    save_superblock();

    // 5. Create initial directories
    fs_create_node(FS_ROOT_ID, "home", FS_TYPE_DIRECTORY, 0, 0);
    fs_create_node(FS_ROOT_ID, "bin", FS_TYPE_DIRECTORY, 0, 0);
    fs_create_node(FS_ROOT_ID, "etc", FS_TYPE_DIRECTORY, 0, 0);

    console_print_colored("FS: Format complete.\n", COLOR_GREEN_ON_BLACK);
}

// --- Public API ---

void fs_init() {
    // Initialize cache
    memset(cache, 0, sizeof(cache));
    access_counter = 0;

    // Read Superblock
    ata_read_sectors(FS_SUPERBLOCK_SECTOR, 1, &sb);

    if (sb.magic != FS_MAGIC) {
        console_print_colored("FS: No filesystem detected. Formatting...\n", COLOR_LIGHT_RED);
        mkfs();
    } else {
        console_print_colored("FS: PUNIX-FS mounted.\n", COLOR_GREEN_ON_BLACK);
    }

    fs_root_id = FS_ROOT_ID;
    fs_current_dir_id = FS_ROOT_ID;

    // Ensure /dev and /dev/tty0-3 exist for the new TTY subsystem
    fs_node_t* dev_dir = fs_find_node("/dev", FS_ROOT_ID);
    if (!dev_dir) {
        fs_create_node(FS_ROOT_ID, "dev", FS_TYPE_DIRECTORY, 0, 0);
        dev_dir = fs_find_node("/dev", FS_ROOT_ID);
    }

    if (dev_dir) {
        char tty_name[16];
        for (int i = 0; i < 8; i++) {
            if (i >= 4) {
                strcpy(tty_name, "ttyS");
                tty_name[4] = '0' + (i - 4);
                tty_name[5] = '\0';

            } else {
                strcpy(tty_name, "tty");
                tty_name[3] = '0' + i;
                tty_name[4] = '\0';
            }
            if (!fs_find_node_local_id(dev_dir->id, tty_name)) {
                fs_create_node(dev_dir->id, tty_name, FS_TYPE_CHARDEV, 0, 0);
                // Major 4 for TTY, Minor i
                fs_node_t* tty_node = fs_find_node(tty_name, dev_dir->id);
                if (tty_node) {
                    tty_node->indirect_block = (4 << 8) | i; // Store major/minor
                    fs_update_node(tty_node);
                }
            }
        }
    }

    // Set initial working directory to root or /home
    inode_t* root = cache_load(FS_ROOT_ID);
    if (root) {
        fs_node_t* dir_home = fs_find_node("home", FS_ROOT_ID);
        if (dir_home && dir_home->type == FS_TYPE_DIRECTORY) {
            fs_current_dir_id = dir_home->id;
        }
    }
}

fs_node_t* fs_get_node(uint32_t id) {
    return cache_load(id);
}

int fs_update_node(fs_node_t* node) {
    if (!node || node->id == 0) return 0;
    cache_mark_dirty(node->id);
    save_node(node->id);
    return 1;
}

int fs_check_permission(inode_t* node, uint32_t uid, uint32_t gid, uint32_t mask) {
    if (uid == 0) return 1; // Root bypass

    // Owner checks
    if (uid == node->uid) {
        if (((node->mode >> 6) & mask) == mask) return 1;
        return 0; // If owner doesn't have it, we don't fall through to group/others for the owner
    }

    // Group checks
    if (gid == node->gid) {
        if (((node->mode >> 3) & mask) == mask) return 1;
    }

    // Other checks
    if ((node->mode & mask) == mask) return 1;

    return 0;
}

uint32_t fs_find_node_local_id(uint32_t parent_id, char* name) {
    inode_t* parent = cache_load(parent_id);
    if (!parent || parent->type != FS_TYPE_DIRECTORY) return 0;

    fs_dirent_t entries[SECTOR_SIZE / sizeof(fs_dirent_t)];
    
    // Search through all direct blocks of the parent directory
    for (int i = 0; i < 12; i++) {
        uint32_t b_id = parent->blocks[i];
        if (b_id == 0) continue;

        ata_read_sectors(b_id, 1, entries);
        for (int j = 0; j < (SECTOR_SIZE / sizeof(fs_dirent_t)); j++) {
            if (entries[j].inode_id != 0 && strcmp(entries[j].name, name) == 0) {
                return entries[j].inode_id;
            }
        }
    }
    return 0;
}

fs_node_t* fs_find_node(const char* path, uint32_t start_id) {
    if (!path) return 0;

    uint32_t current_id = start_id;

    if (path[0] == '/') {
        current_id = fs_root_id;
        path++;
    }

    char temp_path[128];
    strncpy(temp_path, path, 127);
    temp_path[127] = '\0';
    char* component = temp_path;
    char* next_component = 0;

    while (*component != '\0') {
        int i = 0;
        while (component[i] != '/' && component[i] != '\0') {
            i++;
        }

        if (component[i] == '/') {
            component[i] = '\0';
            next_component = component + i + 1;
        } else {
            next_component = 0;
        }

        if (strlen(component) > 0) {
            if (strcmp(component, "..") == 0) {
                fs_node_t* cur = fs_get_node(current_id);  // Lazy load
                if (cur) current_id = cur->parent_id;
            }
            else if (strcmp(component, ".") == 0) {
                // Do nothing
            }
            else {
                fs_node_t* parent = fs_get_node(current_id);
                if (parent && !fs_check_permission(parent, current_task->uid, current_task->gid, 1)) {
                    return 0; // Permission denied
                }
                uint32_t next_id = fs_find_node_local_id(current_id, component);
                if (next_id == 0) return 0;
                current_id = next_id;
            }
        }

        if (!next_component) break;
        component = next_component;
    }

    return fs_get_node(current_id);  // Lazy load final node
}

int fs_create_node(uint32_t parent_id, char* name, uint8_t type, uint32_t uid, uint32_t gid) {
    if (sb.free_inodes == 0) {
        console_print_colored("FS: Disk full (inodes).\n", COLOR_LIGHT_RED);
        return 0;
    }

    inode_t* parent = cache_load(parent_id);
    if (!parent || parent->type != FS_TYPE_DIRECTORY) return 0;

    // 1. Find or allocate a slot in parent directory
    int b_idx = -1;
    int e_idx = -1;
    fs_dirent_t entries[SECTOR_SIZE / sizeof(fs_dirent_t)];

    for (int i = 0; i < 12; i++) {
        uint32_t b_id = parent->blocks[i];
        if (b_id == 0) {
            // Allocate new block for directory entries
            b_id = block_alloc();
            if (b_id == 0) return 0;
            parent->blocks[i] = b_id;
            parent->block_count++;
            fs_update_node(parent);
        }

        ata_read_sectors(b_id, 1, entries);
        for (int j = 0; j < (SECTOR_SIZE / sizeof(fs_dirent_t)); j++) {
            if (entries[j].inode_id == 0) {
                b_idx = i;
                e_idx = j;
                break;
            }
        }
        if (b_idx != -1) break;
    }

    if (b_idx == -1) {
        console_print_colored("FS: Directory full.\n", COLOR_LIGHT_RED);
        return 0;
    }

    // 2. Allocate and initialize new inode
    int new_id = inode_alloc();
    if (new_id < 0) return 0;

    inode_t node;
    memset(&node, 0, sizeof(inode_t));
    node.id = new_id;
    node.parent_id = parent_id;
    node.type = type;
    node.link_count = 1;
    node.uid = uid;
    node.gid = gid;

    if (type == FS_TYPE_DIRECTORY) {
        node.mode = S_IFDIR | 0755;
    } else if (type == FS_TYPE_CHARDEV) {
        node.mode = S_IFCHR | 0666;
    } else {
        node.mode = S_IFREG | 0644;
    }

    // Special case: if parent is set-gid, inherit gid and set set-gid bit
    if (parent->mode & 02000) {
        node.gid = parent->gid;
        if (type == FS_TYPE_DIRECTORY) node.mode |= 02000;
    }
    
    // Save new inode
    uint8_t buf[SECTOR_SIZE];
    ata_read_sectors(INODE_TO_SECTOR(new_id), 1, buf);
    memcpy(buf + INODE_OFF_IN_SECTOR(new_id), &node, sizeof(inode_t));
    ata_write_sectors(INODE_TO_SECTOR(new_id), 1, buf);

    // 3. Add entry to parent
    entries[e_idx].inode_id = new_id;
    strncpy(entries[e_idx].name, name, FS_MAX_NAME - 1);
    entries[e_idx].name[FS_MAX_NAME - 1] = '\0';
    ata_write_sectors(parent->blocks[b_idx], 1, entries);

    parent->size += sizeof(fs_dirent_t);
    fs_update_node(parent);

    return 1;
}

int fs_delete_node(uint32_t id) {
    inode_t* node = cache_load(id);
    if (!node || id == FS_ROOT_ID) return 0;

    // 1. If directory, ensure it's empty
    if (node->type == FS_TYPE_DIRECTORY) {
        fs_dirent_t entries[SECTOR_SIZE / sizeof(fs_dirent_t)];
        for (int i = 0; i < 12; i++) {
            if (node->blocks[i] == 0) continue;
            ata_read_sectors(node->blocks[i], 1, entries);
            for (int j = 0; j < (SECTOR_SIZE / sizeof(fs_dirent_t)); j++) {
                if (entries[j].inode_id != 0) return 0; // Not empty
            }
        }
    }

    // 2. Remove from parent's directory entries
    inode_t* parent = cache_load(node->parent_id);
    if (parent) {
        fs_dirent_t entries[SECTOR_SIZE / sizeof(fs_dirent_t)];
        int found = 0;
        for (int i = 0; i < 12; i++) {
            if (parent->blocks[i] == 0) continue;
            ata_read_sectors(parent->blocks[i], 1, entries);
            for (int j = 0; j < (SECTOR_SIZE / sizeof(fs_dirent_t)); j++) {
                if (entries[j].inode_id == id) {
                    entries[j].inode_id = 0;
                    memset(entries[j].name, 0, FS_MAX_NAME);
                    ata_write_sectors(parent->blocks[i], 1, entries);
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
    }

    // 3. Free all data blocks
    for (int i = 0; i < 12; i++) {
        if (node->blocks[i] != 0) {
            block_free(node->blocks[i]);
            node->blocks[i] = 0;
        }
    }

    // 4. Free indirect blocks
    if (node->indirect_block != 0) {
        uint32_t indices[SECTOR_SIZE / sizeof(uint32_t)];
        ata_read_sectors(node->indirect_block, 1, indices);
        for (int i = 0; i < (SECTOR_SIZE / sizeof(uint32_t)); i++) {
            if (indices[i] != 0) {
                block_free(indices[i]);
            }
        }
        block_free(node->indirect_block);
        node->indirect_block = 0;
    }

    // 5. Free the inode
    bitmap_set(FS_INODE_BITMAP_SECTOR, id, 0);
    sb.free_inodes++;
    save_superblock();

    // Invalidate cache
    int idx = cache_find(id);
    if (idx >= 0) cache[idx].id = 0;

    return 1;
}

void fs_get_disk_stats(uint32_t* total_kb, uint32_t* used_kb, uint32_t* free_kb) {
    *total_kb = (sb.total_blocks * SECTOR_SIZE) / 1024;
    *used_kb = ((sb.total_blocks - sb.free_blocks) * SECTOR_SIZE) / 1024;
    *free_kb = (sb.free_blocks * SECTOR_SIZE) / 1024;
}

// NEW: Get cache statistics
void fs_get_cache_stats(uint32_t* cache_size, uint32_t* cached_nodes, uint32_t* dirty_nodes) {
    *cache_size = FS_CACHE_SIZE;
    *cached_nodes = 0;
    *dirty_nodes = 0;

    for (int i = 0; i < FS_CACHE_SIZE; i++) {
        if (cache[i].id != 0) {
            (*cached_nodes)++;
            if (cache[i].dirty) {
                (*dirty_nodes)++;
            }
        }
    }
}

int fs_read(inode_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !buffer) return 0;
    if (offset >= node->size) return 0;
    if (offset + size > node->size) size = node->size - offset;

    uint32_t read_count = 0;
    uint8_t sector_buf[SECTOR_SIZE];

    while (read_count < size) {
        uint32_t block_idx = (offset + read_count) / SECTOR_SIZE;
        uint32_t block_off = (offset + read_count) % SECTOR_SIZE;
        uint32_t to_read = SECTOR_SIZE - block_off;
        if (to_read > (size - read_count)) to_read = size - read_count;

        uint32_t b_id = 0;
        if (block_idx < 12) {
            b_id = node->blocks[block_idx];
        } else {
            if (node->indirect_block != 0) {
                uint32_t indices[SECTOR_SIZE / sizeof(uint32_t)];
                ata_read_sectors(node->indirect_block, 1, indices);
                uint32_t indirect_idx = block_idx - 12;
                if (indirect_idx < (SECTOR_SIZE / sizeof(uint32_t))) {
                    b_id = indices[indirect_idx];
                }
            }
        }

        if (b_id == 0) break;

        ata_read_sectors(b_id, 1, sector_buf);
        memcpy(buffer + read_count, sector_buf + block_off, to_read);
        read_count += to_read;
    }
    return read_count;
}

int fs_write(inode_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (!node || !buffer) return 0;

    uint32_t write_count = 0;
    uint8_t sector_buf[SECTOR_SIZE];

    while (write_count < size) {
        uint32_t block_idx = (offset + write_count) / SECTOR_SIZE;
        uint32_t block_off = (offset + write_count) % SECTOR_SIZE;
        uint32_t to_write = SECTOR_SIZE - block_off;
        if (to_write > (size - write_count)) to_write = size - write_count;

        uint32_t b_id = 0;
        if (block_idx < 12) {
            if (node->blocks[block_idx] == 0) {
                b_id = block_alloc();
                if (b_id == 0) break;
                node->blocks[block_idx] = b_id;
                node->block_count++;
            } else {
                b_id = node->blocks[block_idx];
            }
        } else {
            // Indirect block logic
            uint32_t indirect_idx = block_idx - 12;
            if (indirect_idx >= (SECTOR_SIZE / sizeof(uint32_t))) {
                console_print_colored("FS: File too large (max limit reached).\n", COLOR_LIGHT_RED);
                break;
            }

            if (node->indirect_block == 0) {
                uint32_t ind_id = block_alloc();
                if (ind_id == 0) break;
                node->indirect_block = ind_id;
                // Zero out the indirect block sector
                uint8_t zero[SECTOR_SIZE];
                memset(zero, 0, SECTOR_SIZE);
                ata_write_sectors(ind_id, 1, zero);
            }

            uint32_t indices[SECTOR_SIZE / sizeof(uint32_t)];
            ata_read_sectors(node->indirect_block, 1, indices);
            if (indices[indirect_idx] == 0) {
                b_id = block_alloc();
                if (b_id == 0) break;
                indices[indirect_idx] = b_id;
                node->block_count++;
                ata_write_sectors(node->indirect_block, 1, indices);
            } else {
                b_id = indices[indirect_idx];
            }
        }

        if (b_id == 0) break;

        if (to_write < SECTOR_SIZE) {
            ata_read_sectors(b_id, 1, sector_buf);
        }
        memcpy(sector_buf + block_off, buffer + write_count, to_write);
        ata_write_sectors(b_id, 1, sector_buf);

        write_count += to_write;
    }

    if (offset + write_count > node->size) {
        node->size = offset + write_count;
    }
    fs_update_node(node);

    return write_count;
}

int fs_get_inode_name(uint32_t id, char* buffer) {
    if (id == FS_ROOT_ID) {
        strcpy(buffer, "/");
        return 1;
    }

    inode_t* node = cache_load(id);
    if (!node) return 0;

    inode_t* parent = cache_load(node->parent_id);
    if (!parent || parent->type != FS_TYPE_DIRECTORY) return 0;

    fs_dirent_t entries[SECTOR_SIZE / sizeof(fs_dirent_t)];
    for (int i = 0; i < 12; i++) {
        uint32_t b_id = parent->blocks[i];
        if (b_id == 0) continue;

        ata_read_sectors(b_id, 1, entries);
        for (int j = 0; j < (SECTOR_SIZE / sizeof(fs_dirent_t)); j++) {
            if (entries[j].inode_id == id) {
                strncpy(buffer, entries[j].name, FS_MAX_NAME);
                return 1;
            }
        }
    }

    return 0;
}

void fs_get_full_path(uint32_t id, char* buffer) {
    if (id == FS_ROOT_ID) {
        strcpy(buffer, "/");
        return;
    }

    char components[10][FS_MAX_NAME];
    int count = 0;
    uint32_t current_id = id;

    // Walk up to root, collecting path components
    while (current_id != FS_ROOT_ID && count < 10) {
        char name[FS_MAX_NAME];
        if (fs_get_inode_name(current_id, name)) {
            strcpy(components[count++], name);
            inode_t* node = cache_load(current_id);
            if (node) {
                current_id = node->parent_id;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    // Build path from root down
    buffer[0] = '\0';  // Start with empty string

    if (count == 0) {
        // Fallback if something went wrong
        strcpy(buffer, "/");
        return;
    }

    for (int i = count - 1; i >= 0; i--) {
        strcat(buffer, "/");
        strcat(buffer, components[i]);
    }
}
