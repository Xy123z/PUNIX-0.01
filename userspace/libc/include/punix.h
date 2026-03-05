#ifndef PUNIX_H
#define PUNIX_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include "punix_def.h"

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

// System call declarations (implemented in syscalls.asm)
int sys_read(int fd, void* buf, size_t count);
int sys_write(int fd, const void* buf, size_t count);
int sys_open(const char* path, int flags);
int sys_close(int fd);
int sys_getdents(const char* path, struct dirent* buf, int count);
int sys_chdir(const char* path);
int sys_getcwd(char* buf, size_t size);
int sys_mkdir(const char* path);
int sys_rmdir(const char* path);
int sys_unlink(const char* path);
int sys_exit(int status);
int sys_getpid(void);
void* sys_malloc(size_t size);
void sys_free(void* ptr);
void sys_print(const char* str);
int sys_create_file(const char* path);
char sys_raw_getchar(void);
void sys_putchar(char c);
void sys_print_colored(const char* str, uint8_t color);
void sys_clear_screen(void);
void sys_get_disk_stats(uint32_t* total, uint32_t* used, uint32_t* free);
void sys_get_cache_stats(uint32_t* size, uint32_t* nodes, uint32_t* dirty);
void sys_sync(void);
int sys_chuser(const char* username);
int sys_chpass(const char* password);
uint32_t sys_getuid(void);
int sys_setuid(uint32_t uid);
uint32_t sys_getgid(void);
int sys_setgid(uint32_t gid);
int sys_authenticate(const char* password);
void sys_shutdown(void);
void sys_restart(void);
void sys_get_mem_stats(uint32_t* total, uint32_t* used, uint32_t* free);
int sys_exec(const char* path, char** argv);
int sys_fork(void);
int sys_get_procs(void* buf, int max);
int sys_kill(int pid);
void sys_sleep(uint32_t ticks);
uint32_t sys_get_ticks(void);
int sys_kbhit(void);
int sys_wait(int pid, int* status);
int sys_get_username(char* buf, uint32_t size);
int sys_stat(const char* path, struct_stat_t* buf);
int sys_chmod(const char* path, uint32_t mode);
int sys_dup2(int oldfd, int newfd);
int sys_pipe(int pipefd[2]);
void sys_draw_char_at(int x, int y, char c, uint8_t color);
void sys_draw_string_at(int x, int y, const char* str, uint8_t color);
void sys_update_cursor(int x, int y);

// Compatibility layer
typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t state;
    char name[32];
} proc_info_t;

#endif
