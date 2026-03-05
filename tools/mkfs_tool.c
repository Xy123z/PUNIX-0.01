
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

// --- Configuration ---
#define FS_MAGIC         0xEF5342
#define FS_ROOT_ID       1
#define SECTOR_SIZE      512
#define FS_MAX_INODES       256
#define FS_MAX_NAME         60

// --- Disk Layout ---
#define FS_SUPERBLOCK_SECTOR    61
#define FS_INODE_BITMAP_SECTOR  62
#define FS_BLOCK_BITMAP_START   63
#define FS_BLOCK_BITMAP_COUNT   25
#define FS_INODE_TABLE_START    88
#define FS_INODE_TABLE_COUNT    64
#define FS_DATA_BLOCKS_START    152

// --- Types ---
typedef struct {
    uint32_t magic;
    uint32_t root_inode;
    uint32_t total_inodes;
    uint32_t total_blocks;
    uint32_t free_inodes;
    uint32_t free_blocks;
    uint8_t  reserved[488];
} superblock_t;

typedef struct inode {
    uint32_t id;
    uint32_t parent_id;
    uint8_t  type;
    uint32_t mode;
    uint16_t link_count;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t block_count;
    uint32_t blocks[12];
    uint32_t indirect_block;
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint8_t  padding[32];
} inode_t;

typedef struct {
    uint32_t inode_id;
    char     name[FS_MAX_NAME];
} fs_dirent_t;

// --- Global Disk File ---
FILE* disk_img = NULL;
superblock_t sb;

// --- Helpers ---
void write_sector(uint32_t sector, void* buf) {
    fseek(disk_img, sector * SECTOR_SIZE, SEEK_SET);
    fwrite(buf, 1, SECTOR_SIZE, disk_img);
}

void read_sector(uint32_t sector, void* buf) {
    fseek(disk_img, sector * SECTOR_SIZE, SEEK_SET);
    fread(buf, 1, SECTOR_SIZE, disk_img);
}

void mark_bitmap(uint32_t start_sector, uint32_t index, int value) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t sector = start_sector + (index / (SECTOR_SIZE * 8));
    uint32_t bit_off = index % (SECTOR_SIZE * 8);
    
    read_sector(sector, buf);
    if (value) buf[bit_off / 8] |= (1 << (bit_off % 8));
    else buf[bit_off / 8] &= ~(1 << (bit_off % 8));
    write_sector(sector, buf);
}

int find_free_bitmap(uint32_t start_sector, uint32_t count) {
    uint8_t buf[SECTOR_SIZE];
    for (uint32_t s = 0; s < (count + 4095) / 4096; s++) {
        read_sector(start_sector + s, buf);
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

int alloc_inode() {
    int id = find_free_bitmap(FS_INODE_BITMAP_SECTOR, FS_MAX_INODES);
    if (id < 0) return -1;
    mark_bitmap(FS_INODE_BITMAP_SECTOR, id, 1);
    sb.free_inodes--;
    return id;
}

int alloc_block() {
    int id = find_free_bitmap(FS_BLOCK_BITMAP_START, sb.total_blocks);
    if (id < 0) return -1;
    mark_bitmap(FS_BLOCK_BITMAP_START, id, 1);
    sb.free_blocks--;
    // Zero out
    uint8_t zero[SECTOR_SIZE] = {0};
    write_sector(id, zero);
    return id;
}

void save_inode(inode_t* node) {
    uint32_t sector = FS_INODE_TABLE_START + (node->id / (SECTOR_SIZE / sizeof(inode_t)));
    uint32_t offset = (node->id % (SECTOR_SIZE / sizeof(inode_t))) * sizeof(inode_t);
    
    uint8_t buf[SECTOR_SIZE];
    read_sector(sector, buf);
    memcpy(buf + offset, node, sizeof(inode_t));
    write_sector(sector, buf);
}

void load_inode(uint32_t id, inode_t* node) {
    uint32_t sector = FS_INODE_TABLE_START + (id / (SECTOR_SIZE / sizeof(inode_t)));
    uint32_t offset = (id % (SECTOR_SIZE / sizeof(inode_t))) * sizeof(inode_t);
    
    uint8_t buf[SECTOR_SIZE];
    read_sector(sector, buf);
    memcpy(node, buf + offset, sizeof(inode_t));
}

// --- High Level ---

void init_disk(const char* filename) {
    disk_img = fopen(filename, "rb+");
    if (!disk_img) {
        printf("Error opening disk image %s\n", filename);
        exit(1);
    }
}

void format_fs() {
    printf("Formatting...\n");
    // Clear bitmaps and inode table (simplified)
    uint8_t zero[SECTOR_SIZE] = {0};
    write_sector(FS_INODE_BITMAP_SECTOR, zero);
    for (int i=0; i<FS_BLOCK_BITMAP_COUNT; i++) write_sector(FS_BLOCK_BITMAP_START+i, zero);
    for (int i=0; i<FS_INODE_TABLE_COUNT; i++) write_sector(FS_INODE_TABLE_START+i, zero);

    // Reserved
    mark_bitmap(FS_INODE_BITMAP_SECTOR, 0, 1);
    for(int i=0; i<FS_DATA_BLOCKS_START; i++) mark_bitmap(FS_BLOCK_BITMAP_START, i, 1);

    // Superblock
    sb.magic = FS_MAGIC;
    sb.root_inode = FS_ROOT_ID;
    sb.total_inodes = FS_MAX_INODES;
    sb.total_blocks = 102400; // 50MB
    sb.free_inodes = FS_MAX_INODES - 1; 
    sb.free_blocks = sb.total_blocks - FS_DATA_BLOCKS_START;

    // Root Inode
    inode_t root = {0};
    root.id = FS_ROOT_ID;
    root.type = 1; // Dir
    root.mode = 0755;
    root.link_count = 1;
    save_inode(&root);

    mark_bitmap(FS_INODE_BITMAP_SECTOR, FS_ROOT_ID, 1);
    sb.free_inodes--;

    write_sector(FS_SUPERBLOCK_SECTOR, &sb);
}

// Add a file to a directory (very partial - assumes parent is root for now or handled externally)
void inject_file(const char* host_path, const char* dest_name, uint32_t parent_id) {
    FILE* f = fopen(host_path, "rb");
    if (!f) {
        printf("Cannot open host file: %s\n", host_path);
        return;
    }
    fseek(f, 0, SEEK_END);
    int size = ftell(f);
    fseek(f, 0, SEEK_SET);

    void* data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    int new_id = alloc_inode();
    if (new_id < 0) { printf("No inodes\n"); free(data); return; }

    inode_t node = {0};
    node.id = new_id;
    node.parent_id = parent_id;
    node.type = 0; // File
    node.size = size;
    node.link_count = 1;

    // Write data
    int remaining = size;
    int offset = 0;
    int b_idx = 0;
    while (remaining > 0) {
        int b_id = alloc_block();
        node.blocks[b_idx++] = b_id;
        node.block_count++;
        
        int to_write = (remaining > SECTOR_SIZE) ? SECTOR_SIZE : remaining;
        uint8_t buf[SECTOR_SIZE] = {0};
        memcpy(buf, data + offset, to_write);
        write_sector(b_id, buf);
        
        offset += to_write;
        remaining -= to_write;
    }

    save_inode(&node);
    free(data);

    // Add to parent directory
    inode_t parent;
    load_inode(parent_id, &parent);
    
    // Find space in parent
    uint8_t buf[SECTOR_SIZE];
    for(int i=0; i<12; i++) {
        if (parent.blocks[i] == 0) {
            parent.blocks[i] = alloc_block();
            parent.block_count++;
        }
        read_sector(parent.blocks[i], buf);
        fs_dirent_t* entries = (fs_dirent_t*)buf;
        for(int j=0; j < SECTOR_SIZE/sizeof(fs_dirent_t); j++) {
            if (entries[j].inode_id == 0) {
                entries[j].inode_id = new_id;
                strncpy(entries[j].name, dest_name, FS_MAX_NAME);
                write_sector(parent.blocks[i], buf);
                save_inode(&parent);
                printf("Injected %s as %s (inode %d)\n", host_path, dest_name, new_id);
                write_sector(FS_SUPERBLOCK_SECTOR, &sb); // Update SB
                return;
            }
        }
    }
}

// Simple mkdir (single level)
int inject_mkdir(const char* name, uint32_t parent_id) {
    int new_id = alloc_inode();
    inode_t node = {0};
    node.id = new_id;
    node.parent_id = parent_id;
    node.type = 1; // Directory
    node.mode = 0755;
    node.link_count = 1;
    save_inode(&node);

    // Add to parent
    inode_t parent;
    load_inode(parent_id, &parent);
    
    uint8_t buf[SECTOR_SIZE];
    for(int i=0; i<12; i++) {
        if (parent.blocks[i] == 0) {
            parent.blocks[i] = alloc_block();
            parent.block_count++;
        }
        read_sector(parent.blocks[i], buf);
        fs_dirent_t* entries = (fs_dirent_t*)buf;
        for(int j=0; j < SECTOR_SIZE/sizeof(fs_dirent_t); j++) {
            if (entries[j].inode_id == 0) {
                entries[j].inode_id = new_id;
                strncpy(entries[j].name, name, FS_MAX_NAME);
                write_sector(parent.blocks[i], buf);
                save_inode(&parent);
                printf("Created directory %s (inode %d)\n", name, new_id);
                write_sector(FS_SUPERBLOCK_SECTOR, &sb);
                return new_id;
            }
        }
    }
    return -1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: mkfs_tool <disk_image> [cmd] [args...]\n");
        return 1;
    }

    init_disk(argv[1]);

    if (argc > 2) {
        if (strcmp(argv[2], "format") == 0) {
            format_fs();
        } else if (strcmp(argv[2], "add") == 0) {
            // add <host_path> <dest_name> <parent_inode>
            read_sector(FS_SUPERBLOCK_SECTOR, &sb);
            inject_file(argv[3], argv[4], atoi(argv[5]));
        } else if (strcmp(argv[2], "mkdir") == 0) {
            read_sector(FS_SUPERBLOCK_SECTOR, &sb);
            inject_mkdir(argv[3], atoi(argv[4]));
        }
    }

    fclose(disk_img);
    return 0;
}
