/**
 * mkfs_host.c - Host-side Filesystem Image Creator with Linux-style Layout
 * Creates a PUNIX-FS compatible filesystem image with standard Unix directories
 *
 * Compile: gcc -o mkfs_host mkfs_host.c
 * Usage: ./mkfs_host disk.img
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

// --- Filesystem Constants (Must match fs.h) ---
#define FS_MAGIC              0xEF5342
#define FS_ROOT_ID            1
#define FS_MAX_INODES         256
#define FS_MAX_NAME           60
#define SECTOR_SIZE           512

#define FS_TYPE_FILE          0
#define FS_TYPE_DIRECTORY     1

// Permission bits
#ifndef S_IRUSR
#define S_IRUSR 0400
#endif
#ifndef S_IWUSR
#define S_IWUSR 0200
#endif
#ifndef S_IXUSR
#define S_IXUSR 0100
#endif
#ifndef S_IRGRP
#define S_IRGRP 0040
#endif
#ifndef S_IWGRP
#define S_IWGRP 0020
#endif
#ifndef S_IXGRP
#define S_IXGRP 0010
#endif
#ifndef S_IROTH
#define S_IROTH 0004
#endif
#ifndef S_IWOTH
#define S_IWOTH 0002
#endif
#ifndef S_IXOTH
#define S_IXOTH 0001
#endif

// Disk Layout (Must match fs.h)
#define FS_SUPERBLOCK_SECTOR  256
#define FS_INODE_BITMAP_SECTOR 257
#define FS_BLOCK_BITMAP_START 258
#define FS_BLOCK_BITMAP_COUNT 25
#define FS_INODE_TABLE_START  283
#define FS_INODE_TABLE_COUNT  64
#define FS_DATA_BLOCKS_START  347

// --- Data Structures (Must match fs.h) ---
typedef struct {
    uint32_t magic;
    uint32_t root_inode;
    uint32_t total_inodes;
    uint32_t total_blocks;
    uint32_t free_inodes;
    uint32_t free_blocks;
    uint8_t  reserved[488];
} superblock_t;

typedef struct {
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

// --- Global State ---
static FILE* disk_file = NULL;
static superblock_t sb;

// --- Helper Functions ---

static void die(const char* msg) {
    fprintf(stderr, "ERROR: %s\n", msg);
    if (disk_file) fclose(disk_file);
    exit(1);
}

static void seek_sector(uint32_t sector) {
    if (fseek(disk_file, sector * SECTOR_SIZE, SEEK_SET) != 0) {
        die("Failed to seek to sector");
    }
}

static void read_sector(uint32_t sector, void* buffer) {
    seek_sector(sector);
    if (fread(buffer, SECTOR_SIZE, 1, disk_file) != 1) {
        die("Failed to read sector");
    }
}

static void write_sector(uint32_t sector, const void* buffer) {
    seek_sector(sector);
    if (fwrite(buffer, SECTOR_SIZE, 1, disk_file) != 1) {
        die("Failed to write sector");
    }
}

static void write_superblock() {
    write_sector(FS_SUPERBLOCK_SECTOR, &sb);
}

// --- Bitmap Management ---

static int bitmap_get(uint32_t sector_start, uint32_t bit) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t sector = sector_start + (bit / (SECTOR_SIZE * 8));
    uint32_t bit_off = bit % (SECTOR_SIZE * 8);

    read_sector(sector, buf);
    return (buf[bit_off / 8] & (1 << (bit_off % 8))) != 0;
}

static void bitmap_set(uint32_t sector_start, uint32_t bit, int value) {
    uint8_t buf[SECTOR_SIZE];
    uint32_t sector = sector_start + (bit / (SECTOR_SIZE * 8));
    uint32_t bit_off = bit % (SECTOR_SIZE * 8);

    read_sector(sector, buf);
    if (value)
        buf[bit_off / 8] |= (1 << (bit_off % 8));
    else
        buf[bit_off / 8] &= ~(1 << (bit_off % 8));
    write_sector(sector, buf);
}

static int bitmap_find_free(uint32_t sector_start, uint32_t count) {
    uint8_t buf[SECTOR_SIZE];
    for (uint32_t s = 0; s < (count + 4095) / 4096; s++) {
        read_sector(sector_start + s, buf);
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

// --- Inode Management ---

static int inode_alloc() {
    int id = bitmap_find_free(FS_INODE_BITMAP_SECTOR, FS_MAX_INODES);
    if (id < 0) return -1;
    bitmap_set(FS_INODE_BITMAP_SECTOR, id, 1);
    sb.free_inodes--;
    write_superblock();
    return id;
}

static int block_alloc() {
    int id = bitmap_find_free(FS_BLOCK_BITMAP_START, sb.total_blocks);
    if (id < 0) return -1;
    bitmap_set(FS_BLOCK_BITMAP_START, id, 1);
    sb.free_blocks--;
    write_superblock();

    // Zero out the block
    uint8_t zero[SECTOR_SIZE];
    memset(zero, 0, SECTOR_SIZE);
    write_sector(id, zero);

    return id;
}

#define INODE_TO_SECTOR(id) (FS_INODE_TABLE_START + ((id) / (SECTOR_SIZE / sizeof(inode_t))))
#define INODE_OFF_IN_SECTOR(id) (((id) % (SECTOR_SIZE / sizeof(inode_t))) * sizeof(inode_t))

static void write_inode(uint32_t id, const inode_t* node) {
    uint8_t buf[SECTOR_SIZE];
    read_sector(INODE_TO_SECTOR(id), buf);
    memcpy(buf + INODE_OFF_IN_SECTOR(id), node, sizeof(inode_t));
    write_sector(INODE_TO_SECTOR(id), buf);
}

static void read_inode(uint32_t id, inode_t* node) {
    uint8_t buf[SECTOR_SIZE];
    read_sector(INODE_TO_SECTOR(id), buf);
    memcpy(node, buf + INODE_OFF_IN_SECTOR(id), sizeof(inode_t));
}

// --- High-level Operations ---

static void format_filesystem() {
    uint8_t zero_sector[SECTOR_SIZE];
    memset(zero_sector, 0, SECTOR_SIZE);

    printf("Formatting filesystem...\n");

    // Clear bitmaps and inode table
    write_sector(FS_INODE_BITMAP_SECTOR, zero_sector);

    for (int i = 0; i < FS_BLOCK_BITMAP_COUNT; i++) {
        write_sector(FS_BLOCK_BITMAP_START + i, zero_sector);
    }

    for (int i = 0; i < FS_INODE_TABLE_COUNT; i++) {
        write_sector(FS_INODE_TABLE_START + i, zero_sector);
    }

    // Setup superblock
    memset(&sb, 0, sizeof(sb));
    sb.magic = FS_MAGIC;
    sb.root_inode = FS_ROOT_ID;
    sb.total_inodes = FS_MAX_INODES;
    sb.total_blocks = 102400; // 50MB
    sb.free_inodes = FS_MAX_INODES;
    sb.free_blocks = sb.total_blocks - FS_DATA_BLOCKS_START;

    write_superblock();

    // Mark reserved blocks and inode 0 as used
    bitmap_set(FS_INODE_BITMAP_SECTOR, 0, 1);
    for (int i = 0; i < FS_DATA_BLOCKS_START; i++) {
        bitmap_set(FS_BLOCK_BITMAP_START, i, 1);
    }

    printf("Superblock created (magic: 0x%X)\n", sb.magic);
}

static uint32_t create_directory(uint32_t parent_id, const char* name, uint32_t mode) {
    if (sb.free_inodes == 0) {
        die("No free inodes");
    }

    int new_id = inode_alloc();
    if (new_id < 0) die("Failed to allocate inode");

    // Create inode
    inode_t node;
    memset(&node, 0, sizeof(inode_t));
    node.id = new_id;
    node.parent_id = parent_id;
    node.type = FS_TYPE_DIRECTORY;
    node.mode = mode;
    node.link_count = 1;
    node.uid = 0;
    node.gid = 0;
    node.size = 0;
    node.block_count = 0;
    node.atime = node.mtime = node.ctime = (uint32_t)time(NULL);

    write_inode(new_id, &node);

    printf("  Created directory /%s (inode %d, mode 0%o)\n", name, new_id, mode);

    // Add to parent directory if not root
    if (parent_id != 0 && parent_id != new_id) {
        inode_t parent;
        read_inode(parent_id, &parent);

        if (parent.type != FS_TYPE_DIRECTORY) {
            die("Parent is not a directory");
        }

        // Find empty slot in parent
        fs_dirent_t entries[SECTOR_SIZE / sizeof(fs_dirent_t)];
        int found_slot = 0;
        int block_idx = -1;
        int entry_idx = -1;

        for (int i = 0; i < 12; i++) {
            uint32_t b_id = parent.blocks[i];
            if (b_id == 0) {
                b_id = block_alloc();
                if (b_id == 0) die("Failed to allocate block");
                parent.blocks[i] = b_id;
                parent.block_count++;
                memset(entries, 0, SECTOR_SIZE);
                block_idx = i;
                entry_idx = 0;
                found_slot = 1;
                break;
            }

            read_sector(b_id, entries);
            for (int j = 0; j < (SECTOR_SIZE / sizeof(fs_dirent_t)); j++) {
                if (entries[j].inode_id == 0) {
                    block_idx = i;
                    entry_idx = j;
                    found_slot = 1;
                    break;
                }
            }
            if (found_slot) break;
        }

        if (!found_slot) {
            die("Parent directory is full");
        }

        // Add entry
        entries[entry_idx].inode_id = new_id;
        strncpy(entries[entry_idx].name, name, FS_MAX_NAME - 1);
        entries[entry_idx].name[FS_MAX_NAME - 1] = '\0';

        write_sector(parent.blocks[block_idx], entries);

        parent.size += sizeof(fs_dirent_t);
        write_inode(parent_id, &parent);
    }

    return new_id;
}

static uint32_t create_file_from_buffer(uint32_t parent_id, const char* name,
                                         const void* data, uint32_t data_len, uint32_t mode) {
    if (sb.free_inodes == 0) {
        die("No free inodes");
    }

    int new_id = inode_alloc();
    if (new_id < 0) die("Failed to allocate inode");

    // Create inode
    inode_t node;
    memset(&node, 0, sizeof(inode_t));
    node.id = new_id;
    node.parent_id = parent_id;
    node.type = FS_TYPE_FILE;
    node.mode = mode;
    node.link_count = 1;
    node.uid = 0;
    node.gid = 0;
    node.size = data_len;
    node.atime = node.mtime = node.ctime = (uint32_t)time(NULL);

    // Write content to blocks if any
    if (data_len > 0) {
        uint32_t remaining = data_len;
        uint32_t offset = 0;
        uint8_t block_buf[SECTOR_SIZE];

        for (int i = 0; remaining > 0; i++) {
            uint32_t b_id = block_alloc();
            if (b_id == 0) die("Failed to allocate block");

            uint32_t to_write = remaining > SECTOR_SIZE ? SECTOR_SIZE : remaining;
            memset(block_buf, 0, SECTOR_SIZE);
            memcpy(block_buf, (const uint8_t*)data + offset, to_write);
            write_sector(b_id, block_buf);

            if (i < 12) {
                node.blocks[i] = b_id;
                node.block_count++;
            } else {
                // Indirect block logic
                if (node.indirect_block == 0) {
                    uint32_t ind_id = block_alloc();
                    if (ind_id == 0) die("Failed to allocate indirect block");
                    node.indirect_block = ind_id;
                    uint8_t zero[SECTOR_SIZE];
                    memset(zero, 0, SECTOR_SIZE);
                    write_sector(ind_id, zero);
                }

                uint32_t indices[SECTOR_SIZE / sizeof(uint32_t)];
                read_sector(node.indirect_block, indices);
                uint32_t ind_idx = i - 12;
                if (ind_idx >= (SECTOR_SIZE / sizeof(uint32_t))) {
                    fprintf(stderr, "WARNING: File '%s' exceeded max size limits\n", name);
                    break;
                }
                indices[ind_idx] = b_id;
                node.block_count++;
                write_sector(node.indirect_block, indices);
            }

            offset += to_write;
            remaining -= to_write;
        }

        if (remaining > 0) {
            fprintf(stderr, "WARNING: File '%s' truncated (%u bytes lost)\n",
                    name, remaining);
        }
    }

    write_inode(new_id, &node);

    printf("  Created file /%s (inode %d, %u bytes, mode 0%o)\n",
           name, new_id, data_len, mode);

    // Add to parent directory
    inode_t parent;
    read_inode(parent_id, &parent);

    if (parent.type != FS_TYPE_DIRECTORY) {
        die("Parent is not a directory");
    }

    // Find empty slot in parent
    fs_dirent_t entries[SECTOR_SIZE / sizeof(fs_dirent_t)];
    int found_slot = 0;
    int block_idx = -1;
    int entry_idx = -1;

    for (int i = 0; i < 12; i++) {
        uint32_t b_id = parent.blocks[i];
        if (b_id == 0) {
            b_id = block_alloc();
            if (b_id == 0) die("Failed to allocate block");
            parent.blocks[i] = b_id;
            parent.block_count++;
            memset(entries, 0, SECTOR_SIZE);
            block_idx = i;
            entry_idx = 0;
            found_slot = 1;
            break;
        }

        read_sector(b_id, entries);
        for (int j = 0; j < (SECTOR_SIZE / sizeof(fs_dirent_t)); j++) {
            if (entries[j].inode_id == 0) {
                block_idx = i;
                entry_idx = j;
                found_slot = 1;
                break;
            }
        }
        if (found_slot) break;
    }

    if (!found_slot) {
        die("Parent directory is full");
    }

    // Add entry
    entries[entry_idx].inode_id = new_id;
    strncpy(entries[entry_idx].name, name, FS_MAX_NAME - 1);
    entries[entry_idx].name[FS_MAX_NAME - 1] = '\0';

    write_sector(parent.blocks[block_idx], entries);

    parent.size += sizeof(fs_dirent_t);
    write_inode(parent_id, &parent);

    return new_id;
}

static uint32_t create_file(uint32_t parent_id, const char* name, const char* content) {
    uint32_t len = content ? strlen(content) : 0;
    return create_file_from_buffer(parent_id, name, content, len, 0644);
}

static uint32_t copy_host_file(uint32_t parent_id, const char* dest_name,
                                const char* src_path, uint32_t mode) {
    FILE* f = fopen(src_path, "rb");
    if (!f) {
        fprintf(stderr, "  WARNING: Could not open '%s' - skipping\n", src_path);
        return 0;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > (12 + 128) * SECTOR_SIZE) {  // Max size with 1 indirect block
        fprintf(stderr, "  WARNING: File '%s' too large (%ld bytes) - truncating\n",
                src_path, size);
        size = (12 + 128) * SECTOR_SIZE;
    }

    // Read file content
    uint8_t* buffer = malloc(size);
    if (!buffer) {
        fclose(f);
        die("Failed to allocate memory");
    }

    size_t bytes_read = fread(buffer, 1, size, f);
    fclose(f);

    if (bytes_read != (size_t)size) {
        fprintf(stderr, "  WARNING: Only read %zu of %ld bytes from '%s'\n",
                bytes_read, size, src_path);
        size = bytes_read;
    }

    uint32_t id = create_file_from_buffer(parent_id, dest_name, buffer, size, mode);
    free(buffer);

    return id;
}

// --- Main Program ---

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <disk_image>\n", argv[0]);
        return 1;
    }

    printf("====================================\n");
    printf("PUNIX-FS Host Creator\n");
    printf("Linux-style Directory Layout\n");
    printf("====================================\n\n");

    // Open disk image
    disk_file = fopen(argv[1], "r+b");
    if (!disk_file) {
        fprintf(stderr, "ERROR: Could not open '%s'\n", argv[1]);
        return 1;
    }

    // Check if filesystem already exists
    uint8_t sb_buf[SECTOR_SIZE];
    read_sector(FS_SUPERBLOCK_SECTOR, sb_buf);
    memcpy(&sb, sb_buf, sizeof(superblock_t));

    if (sb.magic == FS_MAGIC) {
        printf("WARNING: Filesystem already exists on this disk!\n");
        printf("Do you want to reformat? (yes/no): ");
        char response[10];
        if (!fgets(response, sizeof(response), stdin) ||
            strcmp(response, "yes\n") != 0) {
            printf("Aborted.\n");
            fclose(disk_file);
            return 0;
        }
    }

    // Format filesystem
    format_filesystem();

    // Create root directory
    printf("\nCreating Linux-style directory structure...\n");
    inode_t root;
    memset(&root, 0, sizeof(inode_t));
    root.id = FS_ROOT_ID;
    root.parent_id = FS_ROOT_ID;
    root.type = FS_TYPE_DIRECTORY;
    root.mode = 0755;
    root.link_count = 1;
    root.size = 0;
    root.atime = root.mtime = root.ctime = (uint32_t)time(NULL);

    write_inode(FS_ROOT_ID, &root);
    bitmap_set(FS_INODE_BITMAP_SECTOR, FS_ROOT_ID, 1);
    sb.free_inodes--;
    write_superblock();

    printf("  Root directory created (inode %d)\n", FS_ROOT_ID);

    // Create standard Unix directories
    uint32_t bin_id     = create_directory(FS_ROOT_ID, "bin", 0755);
    uint32_t boot_id    = create_directory(FS_ROOT_ID, "boot", 0755);
    uint32_t dev_id     = create_directory(FS_ROOT_ID, "dev", 0755);
    uint32_t etc_id     = create_directory(FS_ROOT_ID, "etc", 0755);
    uint32_t home_id    = create_directory(FS_ROOT_ID, "home", 0755);
    uint32_t lib_id     = create_directory(FS_ROOT_ID, "lib", 0755);
    uint32_t mnt_id     = create_directory(FS_ROOT_ID, "mnt", 0755);
    uint32_t opt_id     = create_directory(FS_ROOT_ID, "opt", 0755);
    uint32_t proc_id    = create_directory(FS_ROOT_ID, "proc", 0755);
    uint32_t root_home  = create_directory(FS_ROOT_ID, "root", 0700);
    uint32_t sbin_id    = create_directory(FS_ROOT_ID, "sbin", 0755);
    uint32_t srv_id     = create_directory(FS_ROOT_ID, "srv", 0755);
    uint32_t tmp_id     = create_directory(FS_ROOT_ID, "tmp", 0777);
    uint32_t usr_id     = create_directory(FS_ROOT_ID, "usr", 0755);
    uint32_t var_id     = create_directory(FS_ROOT_ID, "var", 0755);

    // Create /usr subdirectories
    uint32_t usr_bin    = create_directory(usr_id, "bin", 0755);
    uint32_t usr_sbin   = create_directory(usr_id, "sbin", 0755);
    uint32_t usr_lib    = create_directory(usr_id, "lib", 0755);
    uint32_t usr_local  = create_directory(usr_id, "local", 0755);
    uint32_t usr_share  = create_directory(usr_id, "share", 0755);

    // Create /var subdirectories
    uint32_t var_log    = create_directory(var_id, "log", 0755);
    uint32_t var_tmp    = create_directory(var_id, "tmp", 0755);

    // Create /home/user
    uint32_t user_home  = create_directory(home_id, "user", 0755);

    printf("\n");
    printf("Copying system binaries to /boot and /bin...\n");

    // Copy bootloader to /boot
    uint32_t boot_file = copy_host_file(boot_id, "bootloader.bin", "boot.bin", 0644);
    if (boot_file == 0) {
        printf("  WARNING: boot.bin not found - bootloader not copied to /boot\n");
    }

    // Copy kernel to /boot
    uint32_t kernel_file = copy_host_file(boot_id, "kernel.bin", "kernel.bin", 0644);
    if (kernel_file == 0) {
        printf("  WARNING: kernel.bin not found - kernel not copied to /boot\n");
        printf("  Make sure kernel.bin exists in the current directory\n");
    }

    // NOTE: Shell and text editor are currently compiled into kernel
    // For now, create placeholder files to represent them
    printf("  NOTE: Shell and text editor are kernel modules\n");
    printf("  Creating placeholder entries for future user-space versions...\n");

    // Copy Shell and Text Editor to /bin
    uint32_t bash_id = copy_host_file(bin_id, "bash", "pbash.prog", 0755);
    if (bash_id == 0) {
        printf("  WARNING: pbash.prog not found - bash not copied to /bin\n");
    } else {
        // Create 'sh' as a link to 'bash' (by copying the file again for simplicity in mkfs)
        copy_host_file(bin_id, "sh", "pbash.prog", 0755);
    }

    uint32_t text_file = copy_host_file(usr_bin, "edit", "text.prog", 0755);
    if (text_file == 0) {
        printf("  WARNING: text.prog not found - text editor not copied to /usr/bin\n");
    }
    
    uint32_t clock_file = copy_host_file(usr_bin, "clock", "clock.prog", 0755);
    if (clock_file == 0) {
        printf("  WARNING: clock.prog not found - clock not copied to /usr/bin\n");
    }

    copy_host_file(bin_id, "ls", "ls.prog", 0755);
    copy_host_file(bin_id, "mkdir", "mkdir.prog", 0755);
    copy_host_file(bin_id, "rmdir", "rmdir.prog", 0755);
    copy_host_file(bin_id, "cat", "cat.prog", 0755);
    copy_host_file(bin_id, "pwd", "pwd.prog", 0755);
    copy_host_file(bin_id, "clear", "clear.prog", 0755);
    copy_host_file(bin_id, "chmod", "chmod.prog", 0755);
    copy_host_file(bin_id, "echo", "echo.prog", 0755);
    copy_host_file(bin_id, "snake", "snake.prog", 0755);
    copy_host_file(bin_id, "loadbar", "loadbar.prog", 0755);

    copy_host_file(sbin_id, "ps", "ps.prog", 0755);
    copy_host_file(sbin_id, "kill", "kill.prog", 0755);
    copy_host_file(sbin_id, "mem", "mem.prog", 0755);
    copy_host_file(sbin_id, "init", "init.prog", 0755);
    copy_host_file(sbin_id, "zombie_test", "zombie_test.prog", 0755);

    // Copy hello user programs to /usr/bin
    copy_host_file(usr_bin, "hello1", "hello1.bin", 0755);
    copy_host_file(usr_bin, "hello2", "hello2.bin", 0755);

    copy_host_file(usr_bin, "testvga", "test_vga.prog", 0755);

    printf("\n");
    printf("Creating configuration files in /etc...\n");

    // Create /etc/fstab
    create_file(etc_id, "fstab",
        "# /etc/fstab - filesystem table\n"
        "# <device>  <mountpoint>  <type>  <options>\n"
        "/dev/hda1  /             punixfs  defaults\n");

    // Create /etc/hostname
    create_file(etc_id, "hostname", "punix-system\n");

    // Create /etc/passwd (simplified)
    create_file(etc_id, "passwd",
        "root:x:0:0:root:/root:/bin/shell\n"
        "user:x:1000:1000:User:/home/user:/bin/shell\n");

    // Create /etc/motd
    create_file(etc_id, "motd",
        "========================================\n"
        "Welcome to PUNIX Operating System\n"
        "========================================\n"
        "\n"
        "A Unix-like operating system built from scratch\n"
        "\n"
        "Type 'help' for available commands\n"
        "\n");

    printf("\n");
    printf("Creating user files in /home/user...\n");

    // Create sample files in user home
    create_file(user_home, "welcome.txt",
        "Welcome to PUNIX!\n"
        "\n"
        "This is your home directory. You can create and edit files here.\n"
        "\n"
        "Available commands:\n"
        "  ls      - List files\n"
        "  cd      - Change directory\n"
        "  cat     - Display file contents\n"
        "  edit    - Text editor\n"
        "  mkdir   - Create directory\n"
        "  rm      - Remove file\n"
        "  help    - Show all commands\n"
        "\n"
        "Try exploring the filesystem:\n"
        "  cd /\n"
        "  ls\n"
        "  cat /etc/motd\n"
        "\n");

    create_file(user_home, "notes.txt",
        "My Notes\n"
        "========\n"
        "\n"
        "- Learn more about Unix commands\n"
        "- Explore the filesystem structure\n"
        "- Try the text editor with 'edit notes.txt'\n"
        "\n");

    // Create README in root
    create_file(FS_ROOT_ID, "README",
        "PUNIX Operating System\n"
        "======================\n"
        "\n"
        "Directory Structure:\n"
        "-------------------\n"
        "/bin      - Essential user binaries\n"
        "/boot     - Boot loader and kernel files\n"
        "/dev      - Device files (not yet implemented)\n"
        "/etc      - System configuration files\n"
        "/home     - User home directories\n"
        "/lib      - Shared libraries (not yet implemented)\n"
        "/mnt      - Mount points for filesystems\n"
        "/opt      - Optional software packages\n"
        "/proc     - Process information (not yet implemented)\n"
        "/root     - Root user home directory\n"
        "/sbin     - System binaries\n"
        "/tmp      - Temporary files\n"
        "/usr      - User programs and data\n"
        "/var      - Variable data (logs, etc.)\n"
        "\n"
        "System Binaries:\n"
        "---------------\n"
        "/boot/bootloader.bin - First stage bootloader\n"
        "/boot/kernel.bin     - Operating system kernel\n"
        "\n"
        "Note: Shell and text editor are currently kernel modules.\n"
        "Future versions will move them to user space.\n"
        "\n");

    // Create system information file
    create_file(etc_id, "os-release",
        "NAME=\"PUNIX\"\n"
        "VERSION=\"0.1\"\n"
        "ID=punix\n"
        "PRETTY_NAME=\"PUNIX 0.1\"\n"
        "VERSION_ID=\"0.1\"\n"
        "HOME_URL=\"https://github.com/your-repo/punix\"\n");

    // Final statistics
    printf("\n");
    printf("====================================\n");
    printf("Filesystem created successfully!\n");
    printf("====================================\n");
    printf("Total inodes: %d, Free: %d, Used: %d\n",
           sb.total_inodes, sb.free_inodes,
           sb.total_inodes - sb.free_inodes);
    printf("Total blocks: %d, Free: %d, Used: %d\n",
           sb.total_blocks, sb.free_blocks,
           sb.total_blocks - sb.free_blocks);
    printf("Used space: %d KB (%.2f%%)\n",
           ((sb.total_blocks - sb.free_blocks) * SECTOR_SIZE) / 1024,
           (float)(sb.total_blocks - sb.free_blocks) * 100.0 / sb.total_blocks);
    printf("\n");
    printf("Directory structure follows Linux FHS:\n");
    printf("  - 15 root directories created\n");
    printf("  - System binaries stored in /boot\n");
    printf("  - Configuration files in /etc\n");
    printf("  - User files in /home/user\n");
    printf("\n");

    fclose(disk_file);
    return 0;
}
