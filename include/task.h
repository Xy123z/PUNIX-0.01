#ifndef TASK_H
#define TASK_H

#include "types.h"
#include "paging.h"

#define MAX_FDS 32
#define FD_TYPE_FILE 0
#define FD_TYPE_PIPE 1
#define FD_TYPE_TTY  2

typedef struct {
    uint8_t  type;           // FD_TYPE_FILE, FD_TYPE_PIPE, or FD_TYPE_TTY
    uint32_t node_id;        // Filesystem node ID (if file)
    void*    ptr;            // Pipe object or TTY object
    uint32_t offset;         // Current read/write position
    uint8_t  flags;          // Open flags
    uint8_t  in_use;         // 1 if FD is allocated
} file_descriptor_t;

#define KERNEL_STACK_SIZE 8192

// Process states

/**
 * @brief Process register state (aligned with interrupt stack frame)
 */
typedef struct {
    uint32_t gs, fs, es, ds;                                     // Data segments (gs at lowest address/stack top)
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;         // Pushed by pusha
    uint32_t eip, cs, eflags, esp, ss;                           // Pushed by CPU
} registers_t;

// Process states
typedef enum {
    TASK_NEW,
    TASK_READY,
    TASK_RUNNING,
    TASK_WAITING,
    TASK_IO,
    TASK_TERMINATED,
    TASK_BACKGROUND,
    TASK_ZOMBIE
} task_state_t;

/**
 * @brief Task structure (Process Control Block)
 */
typedef struct task {
    uint32_t id;
    uint32_t parent_id;  // Parent process ID
    uint32_t uid;
    uint32_t gid;
    task_state_t state;
    char name[32];       // Process name
    page_directory_t* page_directory;
    uint32_t kernel_stack;
    uint32_t kernel_esp; // Saved kernel stack pointer
    uint32_t user_stack_top;
    uint32_t cwd_id;     // Current working directory ID
    uint32_t sleep_ticks; // For sleeping
    uint32_t wait_pid;   // Process we are waiting for
    int exit_status;     // Exit status of the process
    uint16_t* video_memory; // Saved VGA buffer (4KB) - DEPRECATED: use TTY instead
    uint16_t console_row;   // DEPRECATED: use TTY instead
    uint16_t console_col;   // DEPRECATED: use TTY instead
    void* tty;              // Pointer to TTY structure for isolated console buffer
    file_descriptor_t fd_table[MAX_FDS];
    struct task* next;
} task_t;

extern task_t* current_task;
extern uint32_t next_pid;

void task_init();
void enter_user_mode(uint32_t target_eip, uint32_t target_esp);
task_t* task_create(uint32_t parent_id, page_directory_t* dir, void* tty);
task_t* task_fork(registers_t* regs);
task_t* task_find(uint32_t pid);
void task_exit(int status);
void task_switch(task_t* task);
void task_run(task_t* task) __attribute__((noreturn));
void task_replace(task_t* task, uint32_t eip, uint32_t esp);
void schedule();
void task_sleep(uint32_t ticks);

void task_cycle_focus(); // Global focus switcher
int task_try_switch_confirm(); // Confirm switch on Enter
void task_update_sleep();
int task_get_procs(proc_info_t* buf, int max);
int task_kill(uint32_t pid);
int task_wait(uint32_t pid, int* status);

#endif
