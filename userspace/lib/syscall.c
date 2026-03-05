#include "../include/syscall.h"

static inline int _syscall0(int sys_num) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(sys_num));
    return ret;
}

static inline int _syscall1(int sys_num, int p1) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(sys_num), "b"(p1));
    return ret;
}

static inline int _syscall2(int sys_num, int p1, int p2) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(sys_num), "b"(p1), "c"(p2));
    return ret;
}

static inline int _syscall3(int sys_num, int p1, int p2, int p3) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(sys_num), "b"(p1), "c"(p2), "d"(p3));
    return ret;
}

void sys_print(const char* str) { _syscall1(SYS_PRINT, (int)str); }
int sys_open(const char* path, int flags) { return _syscall2(SYS_OPEN, (int)path, flags); }
int sys_read(int fd, void* buf, uint32_t count) { return _syscall3(SYS_READ, fd, (int)buf, count); }
int sys_write(int fd, const void* buf, uint32_t count) { return _syscall3(SYS_WRITE, fd, (int)buf, count); }
int sys_close(int fd) { return _syscall1(SYS_CLOSE, fd); }
int sys_getcwd(char* buf, uint32_t size) { return _syscall2(SYS_GETCWD, (int)buf, size); }
int sys_chdir(const char* path) { return _syscall1(SYS_CHDIR, (int)path); }
int sys_mkdir(const char* path) { return _syscall1(SYS_MKDIR, (int)path); }
int sys_rmdir(const char* path) { return _syscall1(SYS_RMDIR, (int)path); }
int sys_create_file(const char* path) { return _syscall1(SYS_CREATE_FILE, (int)path); }
void sys_print_colored(const char* str, uint8_t color) { _syscall2(SYS_PRINT_COLORED, (int)str, color); }
void sys_clear_screen() { _syscall0(SYS_CLEAR_SCREEN); }
void sys_get_cache_stats(uint32_t* size, uint32_t* nodes, uint32_t* dirty) { _syscall3(SYS_GET_CACHE_STATS, (int)size, (int)nodes, (int)dirty); }
void sys_get_disk_stats(uint32_t* total, uint32_t* used, uint32_t* free) { _syscall3(SYS_GET_DISK_STATS, (int)total, (int)used, (int)free); }
void sys_sync() { _syscall0(SYS_SYNC); }
int sys_chuser(const char* username) { return _syscall1(SYS_CHUSER, (int)username); }
int sys_chpass(const char* password) { return _syscall1(SYS_CHPASS, (int)password); }
uint32_t sys_getuid() { return _syscall0(SYS_GETUID); }
int sys_setuid(uint32_t uid) { return _syscall1(SYS_SETUID, uid); }
char sys_getchar() { return (char)_syscall0(SYS_GETCHAR); }
void sys_putchar(char c) { _syscall1(SYS_PUTCHAR, (int)(uint32_t)c); } // Cast to avoid unwanted sign extension issues
void sys_shutdown() { _syscall0(SYS_SHUTDOWN); }
void sys_restart() { _syscall0(SYS_RESTART); }
int sys_authenticate(const char* password) { return _syscall1(SYS_AUTHENTICATE, (int)password); }
int sys_getdents(const char* path, struct dirent* buf, int count) { return _syscall3(SYS_GETDENTS, (int)path, (int)buf, count); }
void sys_exit(int code) { _syscall1(SYS_EXIT, code); }
