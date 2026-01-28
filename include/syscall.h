// include/syscall.h - System Call Interface

#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "task.h"
extern uint32_t kernel_esp_saved;

#define MAX_PATH 256

// Directory entry structure
struct dirent {
    uint32_t d_ino;           // Inode number
    uint8_t  d_type;          // File type
    char     d_name[64];      // Filename
};

typedef struct {
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_size;
    uint32_t st_atime;
    uint32_t st_mtime;
    uint32_t st_ctime;
    uint8_t  st_type;
} struct_stat_t;

// System call numbers (for reference)
#define SYS_READ         0
#define SYS_WRITE        1
#define SYS_OPEN         2
#define SYS_CLOSE        3
#define SYS_GETDENTS     4
#define SYS_CHDIR        5
#define SYS_GETCWD       6
#define SYS_MKDIR        7
#define SYS_RMDIR        8
#define SYS_UNLINK       9
#define SYS_STAT         10
#define SYS_EXIT         11
#define SYS_GETPID       12
#define SYS_MALLOC       13
#define SYS_FREE         14
#define SYS_PRINT        15
#define SYS_CREATE_FILE  16
#define SYS_GETCHAR      17
#define SYS_PUTCHAR      18
#define SYS_PRINT_COLORED 19
#define SYS_CLEAR_SCREEN 20
#define SYS_GET_DISK_STATS 21
#define SYS_GET_CACHE_STATS 22
#define SYS_SYNC         23
#define SYS_CHUSER       24
#define SYS_CHPASS       25
#define O_CREAT   0x04
#define O_RDONLY  0x00
#define O_WRONLY  0x01
#define O_RDWR    0x02

#define SYS_GETUID       26
#define SYS_SETUID       27
#define SYS_AUTHENTICATE 28
#define SYS_SHUTDOWN     29
#define SYS_RESTART      30
#define SYS_GET_MEM_STATS 31
#define SYS_EXEC         32
#define SYS_FORK         33
#define SYS_GET_PROCS    34
#define SYS_KILL         35
#define SYS_SLEEP        36
#define SYS_GET_TICKS    37
#define SYS_KBHIT        38
#define SYS_WAIT         39
#define SYS_GET_USERNAME 40
#define SYS_CHMOD        41
#define SYS_DUP2         42
#define SYS_PIPE         43
#define SYS_GETGID       44
#define SYS_SETGID       45
#define SYS_DRAW_CHAR_AT 46
#define SYS_DRAW_STRING_AT 47
#define SYS_UPDATE_CURSOR 48

// Kernel-side functions
void syscall_init();
void syscall_set_cwd(uint32_t id);
void syscall_free_fd(task_t* task, int fd);
void syscall_close_all(task_t* task);
uint32_t syscall_handler(registers_t* regs);
extern void syscall_interrupt_wrapper();

// User-space system call wrappers
// These functions can be called from user programs

static inline int sys_print(const char* str) {
    int ret;
    __asm__ volatile(
        "mov $15, %%eax\n"      // SYS_PRINT
        "mov %1, %%ebx\n"       // str
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(str)
        : "eax", "ebx"
    );
    return ret;
}

static inline int sys_open(const char* path, int flags) {
    int ret;
    __asm__ volatile(
        "mov $2, %%eax\n"       // SYS_OPEN
        "mov %1, %%ebx\n"       // path
        "mov %2, %%ecx\n"       // flags
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"(flags)
        : "eax", "ebx", "ecx"
    );
    return ret;
}

static inline int sys_read(int fd, void* buf, uint32_t count) {
    int ret;
    __asm__ volatile(
        "mov $0, %%eax\n"       // SYS_READ
        "mov %1, %%ebx\n"       // fd
        "mov %2, %%ecx\n"       // buf
        "mov %3, %%edx\n"       // count
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(fd), "r"(buf), "r"(count)
        : "eax", "ebx", "ecx", "edx"
    );
    return ret;
}

static inline int sys_write(int fd, const void* buf, uint32_t count) {
    int ret;
    __asm__ volatile(
        "mov $1, %%eax\n"       // SYS_WRITE
        "mov %1, %%ebx\n"       // fd
        "mov %2, %%ecx\n"       // buf
        "mov %3, %%edx\n"       // count
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(fd), "r"(buf), "r"(count)
        : "eax", "ebx", "ecx", "edx"
    );
    return ret;
}

static inline int sys_close(int fd) {
    int ret;
    __asm__ volatile(
        "mov $3, %%eax\n"       // SYS_CLOSE
        "mov %1, %%ebx\n"       // fd
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(fd)
        : "eax", "ebx"
    );
    return ret;
}

static inline int sys_getcwd(char* buf, uint32_t size) {
    int ret;
    __asm__ volatile(
        "mov $6, %%eax\n"       // SYS_GETCWD
        "mov %1, %%ebx\n"       // buf
        "mov %2, %%ecx\n"       // size
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(buf), "r"(size)
        : "eax", "ebx", "ecx"
    );
    return ret;
}

static inline int sys_chdir(const char* path) {
    int ret;
    __asm__ volatile(
        "mov $5, %%eax\n"       // SYS_CHDIR
        "mov %1, %%ebx\n"       // path
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(path)
        : "eax", "ebx"
    );
    return ret;
}

static inline int sys_mkdir(const char* path) {
    int ret;
    __asm__ volatile(
        "mov $7, %%eax\n"       // SYS_MKDIR
        "mov %1, %%ebx\n"       // path
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(path)
        : "eax", "ebx"
    );
    return ret;
}

static inline int sys_rmdir(const char* path) {
    int ret;
    __asm__ volatile(
        "mov $8, %%eax\n"       // SYS_RMDIR
        "mov %1, %%ebx\n"       // path
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(path)
        : "eax", "ebx"
    );
    return ret;
}

static inline int sys_create_file(const char* path) {
    int ret;
    __asm__ volatile(
        "mov $16, %%eax\n"      // SYS_CREATE_FILE
        "mov %1, %%ebx\n"       // path
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(path)
        : "eax", "ebx"
    );
    return ret;
}

static inline void sys_print_colored(const char* str, uint8_t color) {
    __asm__ volatile(
        "mov $19, %%eax\n"
        "mov %0, %%ebx\n"
        "mov %1, %%ecx\n"
        "int $0x80\n"
        :
        : "r"(str), "r"((uint32_t)color)
        : "eax", "ebx", "ecx"
    );
}

static inline void sys_clear_screen() {
    __asm__ volatile(
        "mov $20, %%eax\n"
        "int $0x80\n"
        :
        :
        : "eax"
    );
}

static inline void sys_get_cache_stats(uint32_t* size, uint32_t* nodes, uint32_t* dirty) {
    __asm__ volatile(
        "mov $22, %%eax\n"
        "mov %0, %%ebx\n"
        "mov %1, %%ecx\n"
        "mov %2, %%edx\n"
        "int $0x80\n"
        :
        : "r"(size), "r"(nodes), "r"(dirty)
        : "eax", "ebx", "ecx", "edx"
    );
}

static inline void sys_get_disk_stats(uint32_t* total, uint32_t* used, uint32_t* free) {
    __asm__ volatile(
        "mov $21, %%eax\n"
        "mov %0, %%ebx\n"
        "mov %1, %%ecx\n"
        "mov %2, %%edx\n"
        "int $0x80\n"
        :
        : "r"(total), "r"(used), "r"(free)
        : "eax", "ebx", "ecx", "edx"
    );
}

static inline void sys_sync() {
    __asm__ volatile(
        "mov $23, %%eax\n"
        "int $0x80\n"
        :
        :
        : "eax"
    );
}

static inline int sys_chuser(const char* username) {
    int ret;
    __asm__ volatile(
        "mov $24, %%eax\n"
        "mov %1, %%ebx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(username)
        : "eax", "ebx"
    );
    return ret;
}

static inline int sys_chpass(const char* password) {
    int ret;
    __asm__ volatile(
        "mov $25, %%eax\n"
        "mov %1, %%ebx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(password)
        : "eax", "ebx"
    );
    return ret;
}

static inline uint32_t sys_getuid() {
    uint32_t ret;
    __asm__ volatile(
        "mov $26, %%eax\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : : "eax"
    );
    return ret;
}

static inline int sys_setuid(uint32_t uid) {
    int ret;
    __asm__ volatile(
        "mov $27, %%eax\n"
        "mov %1, %%ebx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(uid)
        : "eax", "ebx"
    );
    return ret;
}

static inline uint32_t sys_getgid() {
    uint32_t ret;
    __asm__ volatile(
        "mov $44, %%eax\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : : "eax"
    );
    return ret;
}

static inline int sys_setgid(uint32_t gid) {
    int ret;
    __asm__ volatile(
        "mov $45, %%eax\n"
        "mov %1, %%ebx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(gid)
        : "eax", "ebx"
    );
    return ret;
}

static inline char sys_getchar() {
    char ret;
    __asm__ volatile(
        "mov $17, %%eax\n"
        "int $0x80\n"
        "mov %%al, %0\n"
        : "=r"(ret)
        :
        : "eax"
    );
    return ret;
}

static inline void sys_putchar(char c) {
    __asm__ volatile(
        "mov $18, %%eax\n"
        "mov %0, %%ebx\n"
        "int $0x80\n"
        :
        : "r"((uint32_t)c)
        : "eax", "ebx"
    );
}

static inline void sys_shutdown() {
    __asm__ volatile(
        "mov $29, %%eax\n"
        "int $0x80\n"
        : : : "eax"
    );
}

static inline void sys_restart() {
    __asm__ volatile(
        "mov $30, %%eax\n"
        "int $0x80\n"
        : : : "eax"
    );
}

static inline void sys_get_mem_stats(uint32_t* total, uint32_t* used, uint32_t* free) {
    __asm__ volatile(
        "mov $31, %%eax\n"
        "mov %0, %%ebx\n"
        "mov %1, %%ecx\n"
        "mov %2, %%edx\n"
        "int $0x80\n"
        :
        : "r"(total), "r"(used), "r"(free)
        : "eax", "ebx", "ecx", "edx"
    );
}

static inline int sys_authenticate(const char* password) {
    int ret;
    __asm__ volatile(
        "mov $28, %%eax\n"
        "mov %1, %%ebx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(password)
        : "eax", "ebx"
    );
    return ret;
}

static inline int sys_getdents(const char* path, struct dirent* buf, int count) {
    int ret;
    __asm__ volatile(
        "mov $4, %%eax\n"       // SYS_GETDENTS
        "mov %1, %%ebx\n"       // path
        "mov %2, %%ecx\n"       // buf
        "mov %3, %%edx\n"       // count
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"(buf), "r"(count)
        : "eax", "ebx", "ecx", "edx"
    );
    return ret;
}

static inline int sys_exec(const char* path, char** argv) {
    int ret;
    __asm__ volatile(
        "mov $32, %%eax\n"
        "mov %1, %%ebx\n"
        "mov %2, %%ecx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"(argv)
        : "eax", "ebx", "ecx"
    );
    return ret;
}

static inline int sys_fork() {
    int ret;
    __asm__ volatile(
        "mov $33, %%eax\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        :
        : "eax"
    );
    return ret;
}

static inline void sys_exit(int status) {
    __asm__ volatile(
        "mov $11, %%eax\n"      // SYS_EXIT
        "mov %0, %%ebx\n"       // status
        "int $0x80\n"
        : : "r"(status) : "eax", "ebx"
    );
}

static inline int sys_get_procs(proc_info_t* buf, int max) {
    int ret;
    __asm__ volatile(
        "mov $34, %%eax\n"
        "mov %1, %%ebx\n"
        "mov %2, %%ecx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(buf), "r"(max)
        : "eax", "ebx", "ecx"
    );
    return ret;
}

static inline int sys_kill(uint32_t pid) {
    int ret;
    __asm__ volatile(
        "mov $35, %%eax\n"
        "mov %1, %%ebx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(pid)
        : "eax", "ebx"
    );
    return ret;
}

static inline void sys_sleep(uint32_t ticks) {
    __asm__ volatile(
        "mov $36, %%eax\n"
        "mov %0, %%ebx\n"
        "int $0x80\n"
        : : "r"(ticks) : "eax", "ebx"
    );
}

static inline uint32_t sys_get_ticks() {
    uint32_t ret;
    __asm__ volatile(
        "mov $37, %%eax\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret) : : "eax"
    );
    return ret;
}

static inline int sys_kbhit() {
    int ret;
    __asm__ volatile(
        "mov $38, %%eax\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret) : : "eax"
    );
    return ret;
}

static inline int sys_wait(uint32_t pid) {
    int ret;
    __asm__ volatile(
        "mov $39, %%eax\n"
        "mov %1, %%ebx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(pid)
        : "eax", "ebx"
    );
    return ret;
}

static inline int sys_get_username(char* buf, uint32_t size) {
    int ret;
    __asm__ volatile(
        "mov $40, %%eax\n"
        "mov %1, %%ebx\n"
        "mov %2, %%ecx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(buf), "r"(size)
        : "eax", "ebx", "ecx"
    );
    return ret;
}

static inline int sys_stat(const char* path, struct_stat_t* buf) {
    int ret;
    __asm__ volatile(
        "mov $10, %%eax\n"
        "mov %1, %%ebx\n"
        "mov %2, %%ecx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"(buf)
        : "eax", "ebx", "ecx"
    );
    return ret;
}

static inline int sys_chmod(const char* path, uint32_t mode) {
    int ret;
    __asm__ volatile(
        "mov $41, %%eax\n"
        "mov %1, %%ebx\n"
        "mov %2, %%ecx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(path), "r"(mode)
        : "eax", "ebx", "ecx"
    );
    return ret;
}

static inline int sys_dup2(int oldfd, int newfd) {
    int ret;
    __asm__ volatile(
        "mov $42, %%eax\n"
        "mov %1, %%ebx\n"
        "mov %2, %%ecx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(oldfd), "r"(newfd)
        : "eax", "ebx", "ecx"
    );
    return ret;
}

static inline int sys_pipe(int pipefd[2]) {
    int ret;
    __asm__ volatile(
        "mov $43, %%eax\n"
        "mov %1, %%ebx\n"
        "int $0x80\n"
        "mov %%eax, %0\n"
        : "=r"(ret)
        : "r"(pipefd)
        : "eax", "ebx"
    );
    return ret;
}

#endif // SYSCALL_H
