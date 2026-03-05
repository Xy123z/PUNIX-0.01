#ifndef TASK_H
#define TASK_H

#include "types.h"
#include "paging.h"

// ─── File Descriptor ─────────────────────────────────────────────────────
#define MAX_FDS      32
#define FD_TYPE_FILE 0
#define FD_TYPE_PIPE 1
#define FD_TYPE_TTY  2

typedef struct {
    uint8_t  type;      // FD_TYPE_FILE / FD_TYPE_PIPE / FD_TYPE_TTY
    uint32_t node_id;   // FS inode ID (if file)
    void*    ptr;       // Pipe or tty_device_t pointer
    uint32_t offset;    // File position
    uint8_t  flags;     // Open flags (O_RDONLY etc.)
    uint8_t  in_use;
} file_descriptor_t;

#define KERNEL_STACK_SIZE 8192

// ─── Register frame (matches interrupt stack layout) ─────────────────────
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags, esp, ss;
} registers_t;

// ─── Process states (Unix standard) ──────────────────────────────────────
typedef enum {
    TASK_NEW,
    TASK_READY,
    TASK_RUNNING,
    TASK_WAITING,      // Sleeping / blocked
    TASK_IO,           // Blocked on I/O
    TASK_TERMINATED,
    TASK_STOPPED,      // Suspended (Ctrl+Z)
    TASK_ZOMBIE
} task_state_t;

// ─── Signal numbers (POSIX subset) ───────────────────────────────────────
#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGKILL  9
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define NSIG     21   // Number of supported signals

// ─── Process Control Block ───────────────────────────────────────────────
typedef struct task {
    uint32_t id;
    uint32_t parent_id;
    uint32_t uid;
    uint32_t gid;

    // Session / job control
    uint32_t pgid;          // Process group ID
    uint32_t sid;           // Session ID
    int      ctrl_tty;      // Index into tty_devices[] (-1 = none)

    // Signals
    uint32_t pending_signals;             // Bitmask of pending signals
    uint32_t signal_mask;                 // Blocked signals bitmask
    void*    signal_handlers[NSIG];       // Handler function pointers (NULL = default)

    task_state_t state;
    char name[32];
    page_directory_t* page_directory;
    uint32_t kernel_stack;
    uint32_t kernel_esp;
    uint32_t user_stack_top;
    uint32_t cwd_id;
    uint32_t sleep_ticks;
    uint32_t wait_pid;
    int      exit_status;

    file_descriptor_t fd_table[MAX_FDS];
    struct task* next;
} task_t;

// ─── Open flags ──────────────────────────────────────────────────────────
#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR   0x02
#define O_CREAT  0x04
#define O_TRUNC  0x08
#define O_APPEND 0x10

// ─── Globals ─────────────────────────────────────────────────────────────
extern task_t* current_task;
extern uint32_t next_pid;

// ─── Kernel API ──────────────────────────────────────────────────────────
void     task_init(void);
task_t*  task_create(uint32_t parent_id, page_directory_t* dir, int tty_index);
task_t*  task_fork(registers_t* regs);
task_t*  task_find(uint32_t pid);
void     task_exit(int status);
void     task_switch(task_t* task);
void     task_run(task_t* task) __attribute__((noreturn));
void     task_replace(task_t* task, uint32_t eip, uint32_t esp);
void     schedule(void);
void     task_sleep(uint32_t ticks);
void     task_update_sleep(void);

int      task_get_procs(proc_info_t* buf, int max);
int      task_kill_with_sig(uint32_t pid, int sig);
int      task_wait(uint32_t pid, int* status);

// Signal API
void     task_send_signal(uint32_t pid, int sig);
void     task_send_signal_pgrp(uint32_t pgid, int sig);
void     task_deliver_signals(void);   // Called at end of every syscall

// Session / process group
int      task_setsid(void);
int      task_setpgid(uint32_t pid, uint32_t pgid);

// TTY switch helper (keyboard IRQ uses this)
void     task_tty_switch(int n);

#endif // TASK_H
