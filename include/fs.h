#ifndef FS_H
#define FS_H
#include "types.h"

// --- Constants ---
#define FS_TYPE_FILE        0
#define FS_TYPE_DIRECTORY   1
#define FS_TYPE_CHARDEV     2
#define FS_MAX_NAME         60
#define FS_MAX_INODES       256
#define SECTOR_SIZE         512

// Permission bits & File Types
#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFCHR 0020000

#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001

// --- New Disk Layout ---
#define FS_SUPERBLOCK_SECTOR    256
#define FS_INODE_BITMAP_SECTOR  257
#define FS_BLOCK_BITMAP_START   258
#define FS_BLOCK_BITMAP_COUNT   25
#define FS_INODE_TABLE_START    283
#define FS_INODE_TABLE_COUNT    64
#define FS_DATA_BLOCKS_START    347
// -----------------------

// --- Data Structures ---

/**
 * @brief 128-byte Inode structure
 */
typedef struct inode {
    uint32_t id;                // 0 if free
    uint32_t parent_id;
    uint8_t  type;              // FILE or DIRECTORY or CHARDEV
    uint32_t mode;              // Mode bits (rwxrwxrwx)
    uint16_t link_count;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;              // File size in bytes
    uint32_t block_count;       // Number of blocks allocated
    uint32_t blocks[12];        // Direct pointers
    uint32_t indirect_block;    // Indirect pointer / Char dev major/minor
    uint32_t atime;
    uint32_t mtime;
    uint32_t ctime;
    uint8_t  padding[32];       // Pad to 128 bytes
} inode_t;

typedef inode_t fs_node_t;      // Compatibility

/**
 * @brief 64-byte Directory Entry
 */
typedef struct {
    uint32_t inode_id;
    char     name[FS_MAX_NAME];
} fs_dirent_t;

// --- Global State ---
// These allow the shell to know "where" it is globally
extern uint32_t fs_root_id;
extern uint32_t fs_current_dir_id;

// --- Function Prototypes ---
/**
 * @brief Initializes the file system.
 * Reads the Superblock from Disk. If invalid, formats the drive.
 */
void fs_init();

/**
 * @brief Retrieves a node pointer by its unique ID.
 * @return Pointer to the node in memory, or 0 if invalid.
 */
fs_node_t* fs_get_node(uint32_t id);

/**
 * @brief Resolves a path string to a node.
 * Supports absolute paths ("/") and relative paths from start_id.
 * Handles "." and "..".
 */
fs_node_t* fs_find_node(const char* path, uint32_t start_id);

/**
 * @brief Updates a node on disk.
 */
int fs_update_node(fs_node_t* node);

/**
 * @brief Helper to find a child by name within a specific parent ID.
 * @return The ID of the child, or 0 if not found.
 */
uint32_t fs_find_node_local_id(uint32_t parent_id, char* name);

/**
 * @brief Creates a new node (File or Directory).
 * Automatically persists changes to disk.
 * @return 1 on success, 0 on failure.
 */
int fs_create_node(uint32_t parent_id, char* name, uint8_t type, uint32_t uid, uint32_t gid);

/**
 * @brief Checks if the given uid/gid has the requested permissions on a node.
 * @param node Pointer to the inode
 * @param uid User ID to check
 * @param gid Group ID to check
 * @param mask Permission mask (4=read, 2=write, 1=execute)
 * @return 1 if allowed, 0 if denied
 */
int fs_check_permission(inode_t* node, uint32_t uid, uint32_t gid, uint32_t mask);

/**
 * @brief Deletes a node by ID.
 * Updates the parent directory to remove the link.
 * @return 1 on success, 0 on failure.
 */
int fs_delete_node(uint32_t id);

/**
 * @brief Gets current disk usage statistics.
 * @param total_kb Pointer to store total disk space in KB
 * @param used_kb Pointer to store used disk space in KB
 * @param free_kb Pointer to store free disk space in KB
 */
void fs_get_disk_stats(uint32_t* total_kb, uint32_t* used_kb, uint32_t* free_kb);

/**
 * @brief Gets cache statistics for monitoring performance.
 * @param cache_size Total cache size
 * @param cached_nodes Number of nodes currently in cache
 * @param dirty_nodes Number of nodes pending write-back
 */
void fs_get_cache_stats(uint32_t* cache_size, uint32_t* cached_nodes, uint32_t* dirty_nodes);

/**
 * @brief Retrieves the name of an inode by looking it up in its parent.
 * @return 1 on success, 0 on failure.
 */
int fs_get_inode_name(uint32_t id, char* buffer);

/**
 * @brief Reads data from a node's data blocks.
 */
int fs_read(inode_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);

/**
 * @brief Writes data to a node's data blocks.
 * Automatically allocates blocks as needed.
 */
int fs_write(inode_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);

/**
 * @brief Flushes all dirty cache entries to disk.
 */
void fs_sync();

/**
 * @brief Resolves the full path of an inode.
 */
void fs_get_full_path(uint32_t id, char* buffer);

#endif // FS_H
