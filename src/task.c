// src/task.c - Process Management (Unix-style)
// Process groups, sessions, signals, zombie reaping.
// TTY allocation is removed — TTY devices live in tty.c.

#include "../include/task.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/gdt.h"
#include "../include/paging.h"
#include "../include/tty.h"
#include "../include/interrupt.h"
#include "../include/fs.h"
#include "../include/loader.h"
#include "../include/syscall.h"
#include "../include/pipe.h"

task_t*  current_task   = 0;
static task_t* task_list_head = 0;
uint32_t next_pid       = 1;

extern void switch_to(uint32_t* old_esp, uint32_t new_esp);
extern void task_return(void);

static void int_to_str_local(int v, char* buf) {
    if (v == 0) { buf[0]='0'; buf[1]='\0'; return; }
    char tmp[12]; int i=0;
    int neg = (v < 0); if (neg) v = -v;
    while (v) { tmp[i++] = '0' + (v % 10); v /= 10; }
    if (neg) tmp[i++] = '-';
    for (int j=0; j<i; j++) buf[j] = tmp[i-1-j];
    buf[i] = '\0';
}

static void task_default_signal_action(int sig);

// ─── task_init ───────────────────────────────────────────────────────────
void task_init(void) {
    current_task = (task_t*)kmalloc(sizeof(task_t));
    if (!current_task) return;
    memset(current_task, 0, sizeof(task_t));

    current_task->id         = 0;  // PID 0 = kernel swapper
    current_task->parent_id  = 0;
    current_task->uid        = 0;
    current_task->gid        = 0;
    current_task->pgid       = 0;
    current_task->sid        = 0;
    current_task->ctrl_tty   = -1;
    current_task->state      = TASK_RUNNING;
    strcpy(current_task->name, "swapper");
    current_task->page_directory = current_page_directory;
    current_task->kernel_stack   = 0x90000;
    current_task->kernel_esp     = 0;
    current_task->cwd_id         = 1;

    // Stdin/stdout/stderr → tty0
    tty_device_t* tty0 = tty_get(0);
    if (tty0) {
        current_task->ctrl_tty = 0;
        current_task->fd_table[0].in_use = 1;
        current_task->fd_table[0].type   = FD_TYPE_TTY;
        current_task->fd_table[0].ptr    = tty0;
        current_task->fd_table[0].flags  = O_RDONLY;

        current_task->fd_table[1].in_use = 1;
        current_task->fd_table[1].type   = FD_TYPE_TTY;
        current_task->fd_table[1].ptr    = tty0;
        current_task->fd_table[1].flags  = O_WRONLY;

        current_task->fd_table[2].in_use = 1;
        current_task->fd_table[2].type   = FD_TYPE_TTY;
        current_task->fd_table[2].ptr    = tty0;
        current_task->fd_table[2].flags  = O_WRONLY;
    }

    task_list_head = current_task;
    tss_set_stack(current_task->kernel_stack);
}

// ─── task_create ─────────────────────────────────────────────────────────
// tty_index: which tty_device_t to use for stdin/stdout/stderr (-1 = inherit from parent)
task_t* task_create(uint32_t parent_id, page_directory_t* dir, int tty_index) {
    task_t* task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) return 0;
    memset(task, 0, sizeof(task_t));

    task->id        = next_pid++;
    task->parent_id = parent_id;
    task->uid       = current_task ? current_task->uid : 1000;
    task->gid       = current_task ? current_task->gid : 1000;
    task->pgid      = current_task ? current_task->pgid : task->id;
    task->sid       = current_task ? current_task->sid  : task->id;
    task->ctrl_tty  = -1;
    task->state     = TASK_NEW;
    strcpy(task->name, "new_process");
    task->page_directory = dir;
    task->cwd_id = current_task ? current_task->cwd_id : 1;

    // Determine which TTY to attach
    int ti = tty_index;
    if (ti < 0 && current_task) ti = current_task->ctrl_tty;

    tty_device_t* tty = (ti >= 0) ? tty_get(ti) : 0;
    if (tty) {
        task->ctrl_tty = ti;
        task->fd_table[0].in_use = 1;
        task->fd_table[0].type   = FD_TYPE_TTY;
        task->fd_table[0].ptr    = tty;
        task->fd_table[0].flags  = O_RDONLY;

        task->fd_table[1].in_use = 1;
        task->fd_table[1].type   = FD_TYPE_TTY;
        task->fd_table[1].ptr    = tty;
        task->fd_table[1].flags  = O_WRONLY;

        task->fd_table[2].in_use = 1;
        task->fd_table[2].type   = FD_TYPE_TTY;
        task->fd_table[2].ptr    = tty;
        task->fd_table[2].flags  = O_WRONLY;
    }

    // Allocate kernel stack
    task->kernel_stack = (uint32_t)pmm_alloc_page();
    if (!task->kernel_stack) { kfree(task); return 0; }
    task->kernel_stack += PAGE_SIZE;
    task->kernel_esp    = task->kernel_stack;

    // Prepend to task list
    task->next       = task_list_head;
    task_list_head   = task;
    return task;
}

// ─── task_fork ───────────────────────────────────────────────────────────
task_t* task_fork(registers_t* regs) {
    page_directory_t* new_dir = paging_clone_directory(current_task->page_directory);
    if (!new_dir) return 0;

    task_t* child = task_create(current_task->id, new_dir, current_task->ctrl_tty);
    if (!child) { paging_free_directory(new_dir); return 0; }

    child->user_stack_top = current_task->user_stack_top;
    child->uid  = current_task->uid;
    child->gid  = current_task->gid;
    child->pgid = current_task->pgid;
    child->sid  = current_task->sid;
    strcpy(child->name, current_task->name);
    child->state = TASK_READY;

    // Copy FD table
    for (int i = 0; i < MAX_FDS; i++) {
        child->fd_table[i] = current_task->fd_table[i];
        if (child->fd_table[i].in_use && child->fd_table[i].type == FD_TYPE_PIPE) {
            pipe_t* p = (pipe_t*)child->fd_table[i].ptr;
            if ((child->fd_table[i].flags & 3) == O_RDONLY) p->readers++;
            else if ((child->fd_table[i].flags & 3) == O_WRONLY) p->writers++;
        }
    }

    // Copy signal handlers
    for (int i = 0; i < NSIG; i++)
        child->signal_handlers[i] = current_task->signal_handlers[i];

    // Set up kernel stack so child returns from the syscall with eax=0
    uint32_t* stack = (uint32_t*)child->kernel_stack;
    *(--stack) = regs->ss;
    *(--stack) = regs->esp;
    *(--stack) = regs->eflags;
    *(--stack) = regs->cs;
    *(--stack) = regs->eip;
    *(--stack) = 0;         // error code
    *(--stack) = 0x80;      // interrupt number
    *(--stack) = 0;         // eax = 0 (child)
    *(--stack) = regs->ecx;
    *(--stack) = regs->edx;
    *(--stack) = regs->ebx;
    *(--stack) = regs->esp;
    *(--stack) = regs->ebp;
    *(--stack) = regs->esi;
    *(--stack) = regs->edi;
    *(--stack) = regs->ds;
    *(--stack) = regs->es;
    *(--stack) = regs->fs;
    *(--stack) = regs->gs;
    *(--stack) = (uint32_t)task_return;
    *(--stack) = 0; *(--stack) = 0; *(--stack) = 0; *(--stack) = 0;
    child->kernel_esp = (uint32_t)stack;

    return child;
}

// ─── task_replace (exec) ─────────────────────────────────────────────────
void task_replace(task_t* task, uint32_t eip, uint32_t esp) {
    uint32_t* stack = (uint32_t*)task->kernel_stack;
    *(--stack) = 0x23;   // ss (user data)
    *(--stack) = esp;
    *(--stack) = 0x202;  // eflags
    *(--stack) = 0x1B;   // cs (user code)
    *(--stack) = eip;
    *(--stack) = 0;      // error code
    *(--stack) = 0x80;   // interrupt number
    *(--stack) = 0;      // eax
    *(--stack) = 0; *(--stack) = 0; *(--stack) = 0;
    *(--stack) = esp;
    *(--stack) = 0; *(--stack) = 0; *(--stack) = 0;
    *(--stack) = 0x23;   // ds
    *(--stack) = 0x23;   // es
    *(--stack) = 0x23;   // fs
    *(--stack) = 0x23;   // gs
    *(--stack) = (uint32_t)task_return;
    *(--stack) = 0; *(--stack) = 0; *(--stack) = 0; *(--stack) = 0;
    task->kernel_esp = (uint32_t)stack;
}

// ─── task_find ───────────────────────────────────────────────────────────
task_t* task_find(uint32_t pid) {
    task_t* t = task_list_head;
    while (t) { if (t->id == pid) return t; t = t->next; }
    return 0;
}

// ─── task_do_cleanup ─────────────────────────────────────────────────────
static void task_do_cleanup(task_t* t, int status) {
    if (t->id == 1) {
        // Init died — system halts
        extern int tty_dev_write(tty_device_t*, const char*, int);
        if (active_tty)
            tty_dev_write(active_tty, "\n[ FATAL ] Init (PID 1) terminated! Halted.\n", 44);
        while(1) __asm__ volatile("cli; hlt");
    }

    // If this session leader owned a TTY, send SIGHUP to foreground group
    if (t->sid == t->id && t->ctrl_tty >= 0) {
        tty_device_t* tty = tty_get(t->ctrl_tty);
        if (tty && tty->foreground_pgid)
            task_send_signal_pgrp(tty->foreground_pgid, SIGHUP);
    }

    // Close all FDs
    extern void syscall_close_all(task_t*);
    syscall_close_all(t);

    t->exit_status = status;

    // Wake parent waiting for us
    if (t->parent_id) {
        task_t* parent = task_find(t->parent_id);
        if (parent && (parent->wait_pid == t->id || parent->wait_pid == (uint32_t)-1))
            parent->state = TASK_READY;
        // Send SIGCHLD to parent
        task_send_signal(t->parent_id, SIGCHLD);
    }

    // Orphan adoption: reparent children to PID 1
    task_t* scan = task_list_head;
    while (scan) {
        if (scan->parent_id == t->id) scan->parent_id = 1;
        scan = scan->next;
    }

    t->state = TASK_ZOMBIE;
}

// ─── task_exit ───────────────────────────────────────────────────────────
void task_exit(int status) {
    if (!current_task) return;
    task_do_cleanup(current_task, status);
    while (1) { schedule(); __asm__ volatile("hlt"); }
}

// ─── schedule (round-robin) ───────────────────────────────────────────────
void schedule(void) {
    if (!current_task) return;

    task_t* next = current_task->next;
    if (!next) next = task_list_head;

    int loops = 0;
    while (loops < 100) {
        if (next->state == TASK_READY || next->state == TASK_NEW) break;
        next = next->next;
        if (!next) next = task_list_head;
        loops++;
    }

    if (next->state == TASK_ZOMBIE || next->state == TASK_TERMINATED) {
        next = task_list_head;
    }

    if (next != current_task &&
        (next->state == TASK_READY || next->state == TASK_NEW)) {
        task_t* old = current_task;
        if (old->state == TASK_RUNNING) old->state = TASK_READY;
        next->state   = TASK_RUNNING;
        current_task  = next;
        paging_switch_directory(next->page_directory);
        tss_set_stack(next->kernel_stack);
        switch_to(&old->kernel_esp, next->kernel_esp);
    }
}

// ─── task_sleep ──────────────────────────────────────────────────────────
void task_sleep(uint32_t ticks) {
    if (!current_task) return;
    current_task->sleep_ticks = ticks;
    current_task->state = TASK_WAITING;
    while (current_task->state == TASK_WAITING) {
        schedule();
        if (current_task->state == TASK_WAITING)
            __asm__ volatile("sti; hlt; cli");
    }
}

void task_update_sleep(void) {
    task_t* t = task_list_head;
    while (t) {
        if (t->state == TASK_WAITING) {
            if (t->sleep_ticks > 0) {
                if (--t->sleep_ticks == 0) t->state = TASK_READY;
            } else {
                // If sleep_ticks is 0, it was likely a sleep(0) yield
                t->state = TASK_READY;
            }
        }
        t = t->next;
    }
}

// ─── task_switch / task_run ──────────────────────────────────────────────
void task_switch(task_t* task) {
    if (!task || task == current_task) return;
    current_task = task;
    paging_switch_directory(task->page_directory);
    tss_set_stack(task->kernel_stack);
}

void task_run(task_t* task) {
    // Update active TTY so keyboard goes to this session's TTY
    // REMOVED: if (task->ctrl_tty >= 0) tty_switch(task->ctrl_tty); 
    // This was causing the VGA hardware to flip when switching to a background task.

    current_task = task;
    task->state  = TASK_RUNNING;
    paging_switch_directory(task->page_directory);
    tss_set_stack(task->kernel_stack);

    __asm__ volatile(
        "mov %0, %%esp\n"
        "pop %%edi\n"
        "pop %%esi\n"
        "pop %%ebx\n"
        "pop %%ebp\n"
        "ret\n"
        : : "r"(task->kernel_esp)
    );
    __builtin_unreachable();
}

// ─── task_get_procs ──────────────────────────────────────────────────────
int task_get_procs(proc_info_t* buf, int max) {
    int count = 0;
    task_t* t = task_list_head;
    while (t && count < max) {
        if (t->state != TASK_TERMINATED && t->id != 0) {
            buf[count].pid   = t->id;
            buf[count].ppid  = t->parent_id;
            buf[count].state = (uint32_t)t->state;
            strcpy(buf[count].name, t->name);
            count++;
        }
        t = t->next;
    }
    return count;
}

// ─── task_kill ───────────────────────────────────────────────────────────
int task_kill_with_sig(uint32_t pid, int sig) {
    if (pid <= 0) return -1;
    task_t* t = task_find(pid);
    if (!t || t->state == TASK_ZOMBIE) return -1;

    if (sig == SIGKILL || sig == SIGTERM || sig == SIGINT || sig == SIGQUIT) {
        if (t == current_task) task_exit(128 + sig);
        task_do_cleanup(t, 128 + sig);
        return 0;
    }

    task_send_signal(pid, sig);
    return 0;
}

// ─── task_wait ───────────────────────────────────────────────────────────
// deliver pending signals for current_task — called at end of every syscall
void task_deliver_signals(void) {
    if (!current_task) return;
    uint32_t pending = current_task->pending_signals & ~current_task->signal_mask;
    if (!pending) return;

    for (int sig = 1; sig < NSIG; sig++) {
        if (!(pending & (1u << sig))) continue;
        current_task->pending_signals &= ~(1u << sig);

        void* handler = current_task->signal_handlers[sig];
        if (handler == (void*)1) {
            // SIG_IGN
            continue;
        } else if (handler != 0) {
            // User-space handler: TODO — set up signal trampoline on user stack
            // For now, fall through to default (sufficient for Ctrl+C behaviour)
            task_default_signal_action(sig);
        } else {
            task_default_signal_action(sig);
        }
    }
}

// Update task_send_signal to handle SIGCONT
void task_send_signal(uint32_t pid, int sig) {
    if (sig <= 0 || sig >= NSIG) return;
    task_t* t = task_find(pid);
    if (!t || t->state == TASK_ZOMBIE) return;

    if (sig == SIGCONT) {
        if (t->state == TASK_STOPPED) t->state = TASK_READY;
        // Also wake if WAITING
        if (t->state == TASK_WAITING) t->state = TASK_READY;
        return; 
    }

    t->pending_signals |= (1u << sig);
    // Wake sleeping task so it can handle the signal
    if (t->state == TASK_WAITING) t->state = TASK_READY;
}

void task_send_signal_pgrp(uint32_t pgid, int sig) {
    task_t* t = task_list_head;
    while (t) {
        if (t->pgid == pgid) task_send_signal(t->id, sig);
        t = t->next;
    }
}

// Update task_default_signal_action
static void task_default_signal_action(int sig) {
    switch (sig) {
        case SIGINT:
        case SIGTERM:
        case SIGKILL:
        case SIGQUIT:
            task_exit(128 + sig);
            break;
        case SIGTSTP:
        case SIGSTOP:
            if (current_task) {
                current_task->state = TASK_STOPPED;
                // Wake parent so wait() returns status change
                if (current_task->parent_id) {
                    task_t* p = task_find(current_task->parent_id);
                    if (p && p->state == TASK_WAITING) p->state = TASK_READY;
                }
                schedule();
            }
            break;
        case SIGCHLD:
        case SIGCONT:
        default:
            break; // Ignore
    }
}

int task_wait(uint32_t pid, int* status) {
    if (!current_task) return -1;

    while (1) {
        task_t* child = 0;
        int has_children = 0;
        task_t* scan = task_list_head;

        while (scan) {
            if (scan->parent_id == current_task->id) {
                if (pid == (uint32_t)-1 || scan->id == pid) {
                    has_children = 1;
                    if (scan->state == TASK_ZOMBIE || scan->state == TASK_STOPPED) {
                        child = scan;
                        break;
                    }
                }
            }
            scan = scan->next;
        }

        if (!has_children) return -1;

        if (child) {
            uint32_t cid = child->id;
            if (child->state == TASK_STOPPED) {
                if (status) *status = (20 << 8) | 0x7F; // 20 = SIGTSTP, 0x7F = stopped flag
                // We DON'T remove stopped children from list
                return (int)cid;
            }

            // Zombie cleanup
            task_t* prev = 0, *cur = task_list_head;
            while (cur && cur != child) { prev = cur; cur = cur->next; }
            if (prev) prev->next = child->next;
            else task_list_head = child->next;

            if (child->page_directory && child->page_directory != kernel_page_directory)
                paging_free_directory(child->page_directory);
            if (child->kernel_stack)
                pmm_free_page((void*)(child->kernel_stack - PAGE_SIZE));
            if (status) *status = child->exit_status << 8; // Standard exit status in high byte
            kfree(child);
            return (int)cid;
        }

        current_task->wait_pid = (pid == (uint32_t)-1) ? (uint32_t)-1 : pid;
        current_task->state = TASK_WAITING;
        schedule();
        if (current_task->state == TASK_WAITING)
            __asm__ volatile("sti; hlt; cli");
    }
}

// ─── Session / Process group ──────────────────────────────────────────────
int task_setsid(void) {
    if (!current_task) return -1;
    // Cannot create a new session if already a process group leader
    task_t* scan = task_list_head;
    while (scan) {
        if (scan != current_task && scan->pgid == current_task->id) return -1;
        scan = scan->next;
    }
    current_task->sid      = current_task->id;
    current_task->pgid     = current_task->id;
    current_task->ctrl_tty = -1;  // Detach from controlling TTY
    return (int)current_task->sid;
}

int task_setpgid(uint32_t pid, uint32_t pgid) {
    task_t* t = (pid == 0) ? current_task : task_find(pid);
    if (!t) return -1;
    t->pgid = (pgid == 0) ? t->id : pgid;
    return 0;
}

// ─── TTY switch helper (keyboard hotkey) ─────────────────────────────────
void task_tty_switch(int n) {
    tty_switch(n);
}
