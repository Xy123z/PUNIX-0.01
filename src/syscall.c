/**
 * src/syscall.c - System Call Interface
 * Provides the bridge between user space and kernel space
 */

#include "../include/syscall.h"
#include "../include/types.h"
#include "../include/interrupt.h"
#include "../include/console.h"
#include "../include/fs.h"
#include "../include/string.h"
#include "../include/memory.h"
#include "../include/ata.h"
#include "../include/paging.h"

#include "../include/auth.h"
#include "../include/task.h"
#include "../include/loader.h"
#include "../include/pipe.h"
uint32_t kernel_esp_saved;
extern void kernel_after_user(void);
// File descriptor table is now per-process (current_task->fd_table)

// Current working directory is now per-process (current_task->cwd_id)
__attribute__((noreturn))
void sys_exit_impl(uint32_t status) {
    // Use task subsystem for proper exit handling
    task_exit((int)status);
    
    // Should not reach here, but if it does, halt
    while(1) __asm__ volatile("hlt");
}
/**
 * @brief Initialize file descriptor table and task management
 */
void syscall_init() {
    // Initialize task management
    task_init();
    
    // Default to root if not set
    if (current_task) current_task->cwd_id = fs_root_id;
}

static int check_permission(fs_node_t* node, uint32_t mask) {
    if (!current_task || !node) return 0;
    return fs_check_permission(node, current_task->uid, current_task->gid, mask);
}

void syscall_set_cwd(uint32_t id) {
    if (current_task) current_task->cwd_id = id;
}

/**
 * @brief Allocate a file descriptor
 */
static int allocate_fd(uint32_t node_id, uint8_t flags) {
    if (!current_task) return -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!current_task->fd_table[i].in_use) {
            current_task->fd_table[i].type = FD_TYPE_FILE;
            current_task->fd_table[i].ptr = 0;
            current_task->fd_table[i].node_id = node_id;
            current_task->fd_table[i].offset = 0;
            current_task->fd_table[i].flags = flags;
            current_task->fd_table[i].in_use = 1;
            return i;
        }
    }
    return -1;  // No free FDs
}

/**
 * @brief Free a file descriptor
 */
void syscall_free_fd(task_t* task, int fd) {
    if (task && fd >= 0 && fd < MAX_FDS) {
        if (task->fd_table[fd].in_use) {
            if (task->fd_table[fd].type == FD_TYPE_PIPE) {
                pipe_t* p = (pipe_t*)task->fd_table[fd].ptr;
                if ((task->fd_table[fd].flags & 1) == O_RDONLY) p->readers--;
                else if ((task->fd_table[fd].flags & 1) == O_WRONLY) p->writers--;
                
                if (p->readers == 0 && p->writers == 0) {
                    pipe_destroy(p);
                }
            } else if (task->fd_table[fd].type == FD_TYPE_TTY) {
               // TTYs are usually shared or persistent
            }
            task->fd_table[fd].in_use = 0;
        }
    }
}

void syscall_close_all(task_t* task) {
    if (!task) return;
    for (int i = 0; i < MAX_FDS; i++) {
        syscall_free_fd(task, i);
    }
}

/**
 * @brief System call handler
 * Called when user code executes "int 0x80"
 *
 * Registers on entry:
 *   EAX = syscall number
 *   EBX = arg1
 *   ECX = arg2
 *   EDX = arg3
 *   ESI = arg4
 *   EDI = arg5
 *
 * Return value in EAX
 */
uint32_t syscall_handler(registers_t* regs) {
    uint32_t syscall_num = regs->eax;
    uint32_t ebx = regs->ebx;
    uint32_t ecx = regs->ecx;
    uint32_t edx = regs->edx;
    uint32_t esi = regs->esi;
    uint32_t edi = regs->edi;
    uint32_t ret = 0;

    switch (syscall_num) {
        case SYS_PRINT: {
            // sys_print(const char* str)
            char* str = (char*)ebx;
            console_print(str);
            ret = 0;
            break;
        }

        case SYS_EXIT: {
            // sys_exit(int status)
           sys_exit_impl(ebx);
        }

        case SYS_GETCHAR: {
            ret = keyboard_read();
            break;
        }

        case SYS_PUTCHAR: {
            console_putchar((char)ebx, COLOR_WHITE_ON_BLACK);
            ret = 0;
            break;
        }

        case SYS_PRINT_COLORED: {
            console_print_colored((const char*)ebx, (uint8_t)ecx);
            ret = 0;
            break;
        }

        case SYS_CLEAR_SCREEN: {
            console_clear_screen();
            ret = 0;
            break;
        }

        case SYS_GET_DISK_STATS: {
            fs_get_disk_stats((uint32_t*)ebx, (uint32_t*)ecx, (uint32_t*)edx);
            ret = 0;
            break;
        }

        case SYS_GET_CACHE_STATS: {
            fs_get_cache_stats((uint32_t*)ebx, (uint32_t*)ecx, (uint32_t*)edx);
            ret = 0;
            break;
        }

        case SYS_GET_MEM_STATS: {
            pmm_get_stats((uint32_t*)ebx, (uint32_t*)ecx, (uint32_t*)edx);
            ret = 0;
            break;
        }

        case SYS_STAT: {
            // sys_stat(const char* path, struct stat* buf)
            char* path = (char*)ebx;
            struct_stat_t* buf = (struct_stat_t*)ecx;
            fs_node_t* node = fs_find_node(path, current_task->cwd_id);
            if (!node) {
                ret = -1;
            } else {
                buf->st_ino = node->id;
                buf->st_mode = node->mode;
                buf->st_uid = node->uid;
                buf->st_gid = node->gid;
                buf->st_size = node->size;
                buf->st_atime = node->atime;
                buf->st_mtime = node->mtime;
                buf->st_ctime = node->ctime;
                buf->st_type = node->type;
                ret = 0;
            }
            break;
        }

        case SYS_OPEN: {
            // sys_open(const char* path, int flags)
            char* path = (char*)ebx;
            uint8_t flags = (uint8_t)ecx;

            fs_node_t* node = fs_find_node(path, current_task->cwd_id);
            if (!node) {
                ret = -1;  // File not found
            } else {
                // Permission check
                uint32_t mask = 0;
                if (flags == O_RDONLY) mask = 4;
                else if (flags == O_WRONLY) mask = 2;
                else if (flags == O_RDWR) mask = 6;
                
                if (!check_permission(node, mask)) {
                    ret = -1; // Permission denied
                    break;
                }

                int fd = allocate_fd(node->id, flags);
                ret = fd;
            }
            break;
        }

        case SYS_READ: {
            // sys_read(int fd, void* buf, size_t count)
            int fd = (int)ebx;
            char* buf = (char*)ecx;
            uint32_t count = edx;

            if (fd < 0 || fd >= MAX_FDS || !current_task->fd_table[fd].in_use) {
                ret = -1;
                break;
            }

            if (current_task->fd_table[fd].type == FD_TYPE_TTY) {
                extern int tty_read(void* tty, char* buf, int count);
                ret = tty_read(current_task->fd_table[fd].ptr, buf, count);
                break;
            }

            if (current_task->fd_table[fd].type == FD_TYPE_PIPE) {
                ret = pipe_read((pipe_t*)current_task->fd_table[fd].ptr, (uint8_t*)buf, count);
                break;
            }

            fs_node_t* node = fs_get_node(current_task->fd_table[fd].node_id);
            if (!node) {
                ret = -1;
                break;
            }

            uint32_t offset = current_task->fd_table[fd].offset;
            uint32_t bytes_read = fs_read(node, offset, count, (uint8_t*)buf);

            current_task->fd_table[fd].offset += bytes_read;
            ret = bytes_read;
            break;
        }

        case SYS_WRITE: {
            // sys_write(int fd, const void* buf, size_t count)
            int fd = (int)ebx;
            const char* buf = (const char*)ecx;
            uint32_t count = edx;

            if (fd < 0 || fd >= MAX_FDS || !current_task->fd_table[fd].in_use) {
                ret = -1;
                break;
            }

            if (current_task->fd_table[fd].type == FD_TYPE_TTY) {
                extern int tty_write(void* tty, const char* buf, int count);
                ret = tty_write(current_task->fd_table[fd].ptr, buf, count);
                break;
            }

            if (current_task->fd_table[fd].type == FD_TYPE_PIPE) {
                ret = pipe_write((pipe_t*)current_task->fd_table[fd].ptr, (uint8_t*)buf, count);
                break;
            }

            fs_node_t* node = fs_get_node(current_task->fd_table[fd].node_id);
            if (!node) {
                ret = -1;
                break;
            }

            uint32_t offset = current_task->fd_table[fd].offset;
            uint32_t bytes_written = fs_write(node, offset, count, (uint8_t*)buf);

            current_task->fd_table[fd].offset += bytes_written;
            ret = bytes_written;
            break;
        }

        case SYS_CLOSE: {
            // sys_close(int fd)
            int fd = (int)ebx;
            syscall_free_fd(current_task, fd);
            ret = 0;
            break;
        }

        case SYS_SYNC: {
            fs_sync();
            ret = 0;
            break;
        }

        case SYS_CHUSER: {
            // sys_chuser(const char* username)
            if (current_task->uid != 0) {
                ret = -1; // Permission denied
                break;
            }
            const char* username = (const char*)ebx;
            auth_set_username(username);
            ret = 0;
            break;
        }

        case SYS_CHPASS: {
            // sys_chpass(const char* password)
            if (current_task->uid != 0) {
                ret = -1; // Permission denied
                break;
            }
            const char* password = (const char*)ebx;
            auth_set_password(password);
            ret = 0;
            break;
        }

        case SYS_GETUID: {
            ret = current_task->uid;
            break;
        }

        case SYS_SETUID: {
            // sys_setuid(uint32_t uid)
            // Only root can set uid to something else
            if (current_task->uid != 0) {
                ret = -1;
                break;
            }
            current_task->uid = ebx;
            ret = 0;
            break;
        }

        case SYS_GETGID: {
            ret = current_task->gid;
            break;
        }

        case SYS_SETGID: {
            // sys_setgid(uint32_t gid)
            if (current_task->uid != 0) {
                ret = -1;
                break;
            }
            current_task->gid = ebx;
            ret = 0;
            break;
        }

        case SYS_AUTHENTICATE: {
            // sys_authenticate(const char* password)
            // Verifies password and sets uid to 0 on success
            extern char ROOT_PASSWORD[MAX_PASSWORD_LEN];
            const char* pass = (const char*)ebx;
            if (strcmp(pass, ROOT_PASSWORD) == 0) {
                current_task->uid = 0;
                ret = 0;
            } else {
                ret = -1;
            }
            break;
        }

        case SYS_SHUTDOWN: {
            console_print_colored("\nSHUTTING DOWN SYSTEM...\n", COLOR_YELLOW_ON_BLACK);
            
            // QEMU/Bochs ACPI Shutdown
            __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
            
            // VirtualBox Shutdown (Modern ACPI)
            __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x3400), "Nd"((uint16_t)0x4004));
            
            // QEMU Debug Exit (if enabled)
            __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x0), "Nd"((uint16_t)0x501));

            // If all else fails, halt
            while(1) __asm__ volatile("cli; hlt");
            break;
        }

        case SYS_RESTART: {
            console_print_colored("\nRESTARTING SYSTEM...\n", COLOR_YELLOW_ON_BLACK);
            
            // 8042 Keyboard Controller Reset
            uint8_t good = 0x02;
            while (good & 0x02) {
                __asm__ volatile("inb $0x64, %0" : "=a"(good));
            }
            __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
            
            // Fallback: Triple Fault
            __asm__ volatile("lidt (%0)" : : "r" (0));
            __asm__ volatile("int $3");
            
            while(1) __asm__ volatile("cli; hlt");
            break;
        }

        case SYS_GETCWD: {
            char* buf = (char*)ebx;
            if (!buf) { ret = -1; break; }

            fs_get_full_path(current_task->cwd_id, buf);
            ret = 0;
            break;
        }

        case SYS_CHDIR: {
            // sys_chdir(const char* path)
            char* path = (char*)ebx;

            fs_node_t* target = fs_find_node(path, current_task->cwd_id);
            if (target && target->type == FS_TYPE_DIRECTORY) {
                // Check execute permission for directory
                if (!check_permission(target, 1)) {
                    ret = -1;
                    break;
                }
                current_task->cwd_id = target->id;
                ret = 0;
            } else {
                ret = -1;
            }
            break;
        }

        case SYS_MKDIR: {
            // sys_mkdir(const char* path)
            char* path = (char*)ebx;
            char parent_path[MAX_PATH];
            char name[FS_MAX_NAME];
            
            // Resolve parent and name correctly
            char* last_slash = strrchr(path, '/');
            if (last_slash) {
                if (last_slash == path) { // Root mkdir e.g. /test
                    strcpy(parent_path, "/");
                } else {
                    int len = last_slash - path;
                    strncpy(parent_path, path, len);
                    parent_path[len] = '\0';
                }
                strcpy(name, last_slash + 1);
            } else {
                strcpy(parent_path, ".");
                strcpy(name, path);
            }

            fs_node_t* parent = fs_find_node(parent_path, current_task->cwd_id);
            if (parent && parent->type == FS_TYPE_DIRECTORY) {
                if (!check_permission(parent, 2)) {
                    ret = -1;
                    break;
                }
                if (fs_create_node(parent->id, name, FS_TYPE_DIRECTORY, current_task->uid, 0)) {
                    ret = 0;
                } else {
                    ret = -1;
                }
            } else {
                ret = -1;
            }
            break;
        }

        case SYS_RMDIR: {
            // sys_rmdir(const char* path)
            char* path = (char*)ebx;

            fs_node_t* target = fs_find_node(path, current_task->cwd_id);
            if (target && target->type == FS_TYPE_DIRECTORY) {
                // Ensure we have write permission on the target directory's parent
                fs_node_t* parent = fs_get_node(target->parent_id);
                if (parent && !check_permission(parent, 2)) {
                    ret = -1;
                    break;
                }
                
                if (fs_delete_node(target->id)) {
                    ret = 0;
                } else {
                    ret = -1;
                }
            } else {
                ret = -1;
            }
            break;
        }

        case SYS_UNLINK: {
            char* path = (char*)ebx;
            fs_node_t* target = fs_find_node(path, current_task->cwd_id);
            if (target && target->type == FS_TYPE_FILE) {
                fs_node_t* parent = fs_get_node(target->parent_id);
                if (parent && !check_permission(parent, 2)) {
                    ret = -1;
                    break;
                }
                if (fs_delete_node(target->id)) {
                    ret = 0;
                } else {
                    ret = -1;
                }
            } else {
                ret = -1;
            }
            break;
        }

        case SYS_GETPID: {
            ret = current_task->id;
            break;
        }

        case SYS_CREATE_FILE: {
            // sys_create_file(const char* path)
            char* path = (char*)ebx;
            char parent_path[128];
            char name[FS_MAX_NAME];

            char* last_slash = strrchr(path, '/');
            if (last_slash) {
                int len = last_slash - path;
                if (len == 0) {
                    strcpy(parent_path, "/");
                } else {
                    strncpy(parent_path, path, len);
                    parent_path[len] = '\0';
                }
                strcpy(name, last_slash + 1);
            } else {
                strcpy(parent_path, ".");
                strcpy(name, path);
            }

            fs_node_t* parent = fs_find_node(parent_path, current_task->cwd_id);
            if (parent && parent->type == FS_TYPE_DIRECTORY) {
                // Check write permission on parent directory
                if (!check_permission(parent, 2)) {
                    ret = -1;
                    break;
                }
                if (fs_create_node(parent->id, name, FS_TYPE_FILE, current_task->uid, 0)) {
                    ret = 0;
                } else {
                    ret = -1;
                }
            } else {
                ret = -1;
            }
            break;
        }

        case SYS_GETDENTS: {
            char* path = (char*)ebx;
            struct dirent* dirents = (struct dirent*)ecx;
            int max_count = (int)edx;

            fs_node_t* dir = fs_find_node(path, current_task->cwd_id);
            if (!dir || dir->type != FS_TYPE_DIRECTORY) {
                ret = -1;
                break;
            }

            int count = 0;
            fs_dirent_t entries[SECTOR_SIZE / sizeof(fs_dirent_t)];

            for (int i = 0; i < 12 && count < max_count; i++) {
                if (dir->blocks[i] == 0) continue;
                // Note: In syscall layer, we should probably use a safer read
                // but direct ATA read is used for simplicity in this kernel stage.
                extern int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);
                ata_read_sectors(dir->blocks[i], 1, entries);

                for (int j = 0; j < (SECTOR_SIZE / sizeof(fs_dirent_t)) && count < max_count; j++) {
                    if (entries[j].inode_id != 0) {
                        dirents[count].d_ino = entries[j].inode_id;
                        inode_t* child = fs_get_node(entries[j].inode_id);
                        dirents[count].d_type = child ? child->type : 0;
                        strcpy(dirents[count].d_name, entries[j].name);
                        count++;
                    }
                }
            }

            ret = count;
            break;
        }

        case SYS_MALLOC: {
            // sys_malloc(size_t size)
            uint32_t size = ebx;
            void* ptr = kmalloc(size);
            ret = (uint32_t)ptr;
            break;
        }

        case SYS_FREE: {
            // sys_free(void* ptr)
            void* ptr = (void*)ebx;
            kfree(ptr);
            ret = 0;
            break;
        }

        case SYS_EXEC: {
            console_print("Syscall: EXEC ");
            console_print((const char*)ebx);
            console_print("\n");
            
            char* path = (char*)ebx;
            char** argv = (char**)ecx;

            fs_node_t* node = fs_find_node(path, current_task->cwd_id);
            if (!node || !check_permission(node, 1)) {
                ret = -1; // Permission denied or not found
                break;
            }

            // Count argc (argv is NULL terminated)
            int argc = 0;
            if (argv) {
                while (argv[argc] != NULL) argc++;
            }

            task_t* task = load_user_program(current_task, path, argc, argv);
            if (task) {
                task_run(task);
                ret = 0;
            } else {
                ret = -1;
            }
            break;
        }

        case SYS_FORK: {
            // Fork the current process
            task_t* child = task_fork(regs);
            if (!child) {
                ret = -1;  // Fork failed
            } else {
                // Parent gets child PID
                ret = child->id;
                // Child will get 0 (set in task_fork)
                
                // Don't call task_run here - let scheduler handle it
                child->state = TASK_READY;
            }
            break;
        }

        case SYS_GET_PROCS: {
            ret = task_get_procs((proc_info_t*)ebx, (int)ecx);
            break;
        }

        case SYS_KILL: {
            ret = task_kill((uint32_t)ebx);
            break;
        }

        case SYS_SLEEP: {
            task_sleep(ebx);
            ret = 0;
            break;
        }

        case SYS_GET_TICKS: {
            extern uint32_t timer_ticks;
            ret = timer_ticks;
            break;
        }

        case SYS_KBHIT: {
            extern int keyboard_has_data();
            ret = keyboard_has_data();
            break;
        }

        case SYS_WAIT: {
            ret = task_wait(ebx, (int*)ecx);
            break;
        }

        case SYS_GET_USERNAME: {
            char* buf = (char*)ebx;
            uint32_t size = ecx;
            extern char USERNAME[MAX_USERNAME_LEN];
            strncpy(buf, USERNAME, size - 1);
            buf[size - 1] = '\0';
            ret = 0;
            break;
        }

        case SYS_CHMOD: {
            // sys_chmod(const char* path, uint32_t mode)
            char* path = (char*)ebx;
            uint32_t mode = ecx;
            fs_node_t* node = fs_find_node(path, current_task->cwd_id);
            if (!node) {
                ret = -1;
            } else {
                // Only owner or root can chmod
                if (current_task->uid != 0 && current_task->uid != node->uid) {
                    ret = -1;
                } else {
                    node->mode = mode;
                    fs_update_node(node);
                    ret = 0;
                }
            }
            break;
        }

        case SYS_DUP2: {
            // sys_dup2(int oldfd, int newfd)
            int oldfd = (int)ebx;
            int newfd = (int)ecx;
            if (oldfd < 0 || oldfd >= MAX_FDS || newfd < 0 || newfd >= MAX_FDS) {
                ret = -1;
                break;
            }
            if (!current_task->fd_table[oldfd].in_use) {
                ret = -1;
                break;
            }
            if (oldfd == newfd) {
                ret = newfd;
                break;
            }
            // If newfd is in use, close it (simplified, just mark not in use)
            // Note: In a real dup2, we should call close(newfd) first.
            if (current_task->fd_table[newfd].in_use) {
                syscall_free_fd(current_task, newfd);
            }

            current_task->fd_table[newfd] = current_task->fd_table[oldfd];
            
            // Reference counting for pipes
            if (current_task->fd_table[newfd].type == FD_TYPE_PIPE) {
                pipe_t* p = (pipe_t*)current_task->fd_table[newfd].ptr;
                if ((current_task->fd_table[newfd].flags & 1) == O_RDONLY) p->readers++;
                else if ((current_task->fd_table[newfd].flags & 1) == O_WRONLY) p->writers++;
            }

            ret = newfd;
            break;
        }

        case SYS_PIPE: {
            // sys_pipe(int pipefd[2])
            int* pipefd = (int*)ebx;
            pipe_t* p = pipe_create();
            if (!p) {
                ret = -1;
                break;
            }
            
            int pr = -1, pw = -1;
            for (int i = 0; i < MAX_FDS; i++) {
                if (!current_task->fd_table[i].in_use) {
                    if (pr == -1) pr = i;
                    else if (pw == -1) { pw = i; break; }
                }
            }
            
            if (pr == -1 || pw == -1) {
                pipe_destroy(p);
                ret = -1;
                break;
            }
            
            // Set up read end
            current_task->fd_table[pr].in_use = 1;
            current_task->fd_table[pr].type = FD_TYPE_PIPE;
            current_task->fd_table[pr].ptr = p;
            current_task->fd_table[pr].flags = O_RDONLY;
            
            // Set up write end
            current_task->fd_table[pw].in_use = 1;
            current_task->fd_table[pw].type = FD_TYPE_PIPE;
            current_task->fd_table[pw].ptr = p;
            current_task->fd_table[pw].flags = O_WRONLY;
            
            pipefd[0] = pr;
            pipefd[1] = pw;
            ret = 0;
            break;
        }

        case SYS_DRAW_CHAR_AT: {
            // sys_draw_char_at(int x, int y, char c, char color)
            extern void console_draw_char_at(int x, int y, char c, char color);
            console_draw_char_at((int)ebx, (int)ecx, (char)edx, (char)esi);
            ret = 0;
            break;
        }

        case SYS_DRAW_STRING_AT: {
            // sys_draw_string_at(int x, int y, const char* str, char color)
            extern void console_draw_string_at(int x, int y, const char* str, char color);
            console_draw_string_at((int)ebx, (int)ecx, (const char*)edx, (char)esi);
            ret = 0;
            break;
        }

        case SYS_UPDATE_CURSOR: {
            // sys_update_cursor(int x, int y)
            extern void console_update_cursor(int x, int y);
            console_update_cursor((int)ebx, (int)ecx);
            ret = 0;
            break;
        }

        default:
            console_print("Unknown syscall: ");
            char num[12];
            int_to_str(syscall_num, num);
            console_print(num);
            console_print("\n");
            ret = -1;
            break;
    }

    // Return value goes in EAX naturally via C calling convention
    return ret;
}

// Assembly wrapper for system call interrupt
__asm__(
    ".global syscall_interrupt_wrapper\n"
    "syscall_interrupt_wrapper:\n"
    "   cli\n"
    "   pusha\n"            // Push EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX
    "   push %ds\n"
    "   push %es\n"
    "   push %fs\n"
    "   push %gs\n"
    
    "   mov $0x10, %ax\n"   // Kernel data segment
    "   mov %ax, %ds\n"
    "   mov %ax, %es\n"
    "   mov %ax, %fs\n"
    "   mov %ax, %gs\n"
    
    "   push %esp\n"        // Pass pointer to registers_t
    "   call syscall_handler\n"
    "   add $4, %esp\n"     // Clean up pointer
    
    "   mov %eax, 44(%esp)\n" // Store return value in EAX slot of registers_t on stack
                            // registers_t: gs(0), fs(4), es(8), ds(12), edi(16), esi(20), ebp(24), esp_dummy(28), ebx(32), edx(36), ecx(40), eax(44)
    
    "   pop %gs\n"
    "   pop %fs\n"
    "   pop %es\n"
    "   pop %ds\n"
    "   popa\n"
    "   iret\n"
);
