#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"

// System call numbers
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
#define SYS_GETGID       44
#define SYS_SETGID       45
#define SYS_DRAW_CHAR_AT 46
#define SYS_DRAW_STRING_AT 47
#define SYS_UPDATE_CURSOR 48
#define SYS_GETUID       26
#define SYS_SETUID       27
#define SYS_AUTHENTICATE 28
#define SYS_SHUTDOWN     29
#define SYS_RESTART      30

// Flags
#define O_CREAT   0x04
#define O_RDONLY  0x00
#define O_WRONLY  0x01
#define O_RDWR    0x02

// Structures
#define FS_MAX_NAME 60
struct dirent {
    uint32_t d_ino;
    uint8_t  d_type;
    char     d_name[64];
};

// Syscall Wrappers
void sys_print(const char* str);
int sys_open(const char* path, int flags);
int sys_read(int fd, void* buf, uint32_t count);
int sys_write(int fd, const void* buf, uint32_t count);
int sys_close(int fd);
int sys_getcwd(char* buf, uint32_t size);
int sys_chdir(const char* path);
int sys_mkdir(const char* path);
int sys_rmdir(const char* path);
int sys_create_file(const char* path);
void sys_print_colored(const char* str, uint8_t color);
void sys_clear_screen();
void sys_get_cache_stats(uint32_t* size, uint32_t* nodes, uint32_t* dirty);
void sys_get_disk_stats(uint32_t* total, uint32_t* used, uint32_t* free);
void sys_sync();
int sys_chuser(const char* username);
int sys_chpass(const char* password);
uint32_t sys_getuid();
int sys_setuid(uint32_t uid);
char sys_getchar();
void sys_putchar(char c);
void sys_shutdown();
void sys_restart();
int sys_authenticate(const char* password);
int sys_getdents(const char* path, struct dirent* buf, int count);
void sys_exit(int code);

#endif
