// include/syscall.h - System Call Interface (Unix-conforming)

#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "task.h"
#include "tty.h"

extern uint32_t kernel_esp_saved;
#define MAX_PATH 256

// ─── Stat structure ───────────────────────────────────────────────────────
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

// ─── dirent ──────────────────────────────────────────────────────────────
struct dirent {
    uint32_t d_ino;
    uint8_t  d_type;
    char     d_name[64];
};

// ─── Open flags (also in task.h, kept here for userspace include compat) ─
#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR   0x02
#define O_CREAT  0x04
#define O_TRUNC  0x08
#define O_APPEND 0x10

// ─── Syscall numbers ─────────────────────────────────────────────────────
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
// 13 = unused (was SYS_MALLOC)
// 14 = unused (was SYS_FREE)
// 15 = unused (was SYS_PRINT)
#define SYS_CREATE_FILE  16
// 17 = unused (was SYS_GETCHAR)
// 18 = unused (was SYS_PUTCHAR)
// 19 = unused (was SYS_PRINT_COLORED)
// 20 = unused (was SYS_CLEAR_SCREEN)
#define SYS_GET_DISK_STATS  21
#define SYS_GET_CACHE_STATS 22
#define SYS_SYNC         23
#define SYS_CHUSER       24
#define SYS_CHPASS       25
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
// 46,47,48 = unused (were SYS_DRAW_*)

// NEW: Unix-conforming additions
#define SYS_TCGETATTR    49
#define SYS_TCSETATTR    50
#define SYS_IOCTL        51
#define SYS_SIGACTION    52
#define SYS_GETPGRP      54
#define SYS_SETPGID      55
#define SYS_GETSID       56
#define SYS_SETSID       57

// ─── Kernel-side functions ────────────────────────────────────────────────
void      syscall_init(void);
void      syscall_set_cwd(uint32_t id);
void      syscall_free_fd(task_t* task, int fd);
void      syscall_close_all(task_t* task);
uint32_t  syscall_handler(registers_t* regs);
extern void syscall_interrupt_wrapper(void);

#endif // SYSCALL_H
