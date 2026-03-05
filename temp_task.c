#include "../include/task.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/gdt.h"
#include "../include/paging.h"
#include "../include/console.h"
#include "../include/vga.h"

task_t* current_task = 0;
static task_t* task_list_head = 0;
uint32_t next_pid = 1;

extern void switch_to(uint32_t* old_esp, uint32_t new_esp);
extern void task_return(void);

/**
 * @brief Initialize the first task (the kernel process)
 */
void task_init() {
    current_task = (task_t*)kmalloc(sizeof(task_t));
    memset(current_task, 0, sizeof(task_t));

    current_task->id = next_pid++;
    current_task->parent_id = 0;
    current_task->uid = 0;
    current_task->gid = 0;
    current_task->state = TASK_RUNNING;
    strcpy(current_task->name, "kernel");
    current_task->page_directory = current_page_directory;
    current_task->kernel_stack = 0x90000;
    current_task->kernel_esp = 0;  // Will be set on first switch
    current_task->cwd_id = 1;
    current_task->sleep_ticks = 0;
    current_task->wait_pid = 0;
    current_task->next = 0;

    task_list_head = current_task;
    tss_set_stack(current_task->kernel_stack);
}

/**
 * @brief Create a new task with initialized stack
 */
task_t* task_create(uint32_t parent_id, page_directory_t* dir) {
    task_t* task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) return 0;
    
    memset(task, 0, sizeof(task_t));
    
    task->id = next_pid++;
    task->parent_id = parent_id;
    task->uid = 1000;
    task->gid = 1000;
    task->state = TASK_NEW;
    strcpy(task->name, "new_process");
    task->page_directory = dir;
    task->cwd_id = current_task ? current_task->cwd_id : 1;
    task->sleep_ticks = 0;
    task->wait_pid = 0;
    
    // Allocate kernel stack
    task->kernel_stack = (uint32_t)pmm_alloc_page();
    if (!task->kernel_stack) {
        kfree(task);
        return 0;
    }
    task->kernel_stack += PAGE_SIZE;
    
    // Initialize kernel_esp to empty stack (will be set up by task_replace)
    task->kernel_esp = task->kernel_stack;
    
    // Add to task list
    task->next = task_list_head;
    task_list_head = task;
    
    return task;
}

/**
 * @brief Fork current task - create exact copy
 */
task_t* task_fork(registers_t* regs) {
    page_directory_t* new_dir = paging_clone_directory(current_task->page_directory);
    if (!new_dir) {
        console_print("fork: Failed to clone page directory\n");
        return 0;
    }
    
    task_t* child = task_create(current_task->id, new_dir);
    if (!child) {
        paging_free_directory(new_dir);
        return 0;
    }
    
    child->user_stack_top = current_task->user_stack_top;
    child->uid = current_task->uid;
    child->gid = current_task->gid;
    strcpy(child->name, current_task->name);
    child->state = TASK_READY;
    
    // Set up child's kernel stack to return from interrupt
    // The stack should look like it was interrupted and saved registers
    uint32_t* stack = (uint32_t*)child->kernel_stack;
    
    // Push interrupt frame (as if CPU pushed it)
    *(--stack) = regs->ss;
    *(--stack) = regs->esp;
    *(--stack) = regs->eflags;
    *(--stack) = regs->cs;
    *(--stack) = regs->eip;
    
    // Push interrupt number and error code (task_return skips these)
    *(--stack) = 0;        // error code
    *(--stack) = 0x80;     // interrupt number
    
    // Push general purpose registers (as if pusha)
    *(--stack) = 0; // eax (child returns 0)
    *(--stack) = regs->ecx;
    *(--stack) = regs->edx;
    *(--stack) = regs->ebx;
    *(--stack) = regs->esp; // original esp
    *(--stack) = regs->ebp;
    *(--stack) = regs->esi;
    *(--stack) = regs->edi;
    
    // Push segment selectors
    *(--stack) = regs->ds;
    *(--stack) = regs->es;
    *(--stack) = regs->fs;
    *(--stack) = regs->gs;
    
    // Now push the switch_to context
    *(--stack) = (uint32_t)task_return; // return address
    *(--stack) = 0; // ebp
    *(--stack) = 0; // ebx
    *(--stack) = 0; // esi
    *(--stack) = 0; // edi
    
    child->kernel_esp = (uint32_t)stack;
    
    return child;
}

/**
 * @brief Replace current task context with new entry point (exec)
 */
void task_replace(task_t* task, uint32_t eip, uint32_t esp) {
    // Set up the kernel stack to look like an interrupt occurred
    uint32_t* stack = (uint32_t*)task->kernel_stack;
    
    // User mode interrupt frame (CPU pushes these on iret)
    *(--stack) = 0x23;     // ss (user data)
    *(--stack) = esp;      // user esp
    *(--stack) = 0x202;    // eflags (IF=1)
    *(--stack) = 0x1B;     // cs (user code)
    *(--stack) = eip;      // eip
    
    // Fake interrupt number and error code (task_return does add esp, 8)
    *(--stack) = 0;        // error code
    *(--stack) = 0x80;     // interrupt number (fake syscall)
    
    // General purpose registers (pusha order)
    *(--stack) = 0;        // eax
    *(--stack) = 0;        // ecx
    *(--stack) = 0;        // edx
    *(--stack) = 0;        // ebx
    *(--stack) = esp;      // original esp
    *(--stack) = 0;        // ebp
    *(--stack) = 0;        // esi
    *(--stack) = 0;        // edi
    
    // Segment selectors
    *(--stack) = 0x23;     // ds
    *(--stack) = 0x23;     // es
    *(--stack) = 0x23;     // fs
    *(--stack) = 0x23;     // gs
    
    // switch_to context
    *(--stack) = (uint32_t)task_return;
    *(--stack) = 0;        // ebp
    *(--stack) = 0;        // ebx
    *(--stack) = 0;        // esi
    *(--stack) = 0;        // edi
    
    task->kernel_esp = (uint32_t)stack;
}

/**
 * @brief Find task by PID
 */
task_t* task_find(uint32_t pid) {
    task_t* task = task_list_head;
    while (task) {
        if (task->id == pid) return task;
        task = task->next;
    }
    return 0;
}

/**
 * @brief Exit current task
 */
void task_exit(int status) {
    if (!current_task) return;
    
    console_print("Process ");
    char num[12];
    int_to_str(current_task->id, num);
    console_print(num);
    console_print(" exiting with status ");
    int_to_str(status, num);
    console_print(num);
    console_print("\n");
    
    // Wake up any parent waiting for us
    if (current_task->parent_id) {
        task_t* parent = task_find(current_task->parent_id);
        if (parent && parent->wait_pid == current_task->id) {
            parent->state = TASK_READY;
            parent->wait_pid = 0;
        }
    }
    
    current_task->state = TASK_TERMINATED;
    
    // Never return
    while(1) {
        schedule();
        __asm__ volatile("hlt");
    }
}

/**
 * @brief Round Robin Scheduler
 */
void schedule() {
    if (!current_task) return;

    task_t* next = current_task->next;
    if (!next) next = task_list_head;

    // Find next ready/new task
    int loops = 0;
    while (next != current_task && loops < 100) {
        if (next->state == TASK_READY || next->state == TASK_NEW) {
            break;
        }
        next = next->next;
        if (!next) next = task_list_head;
        loops++;
    }

    // Skip terminated tasks
    if (next->state == TASK_TERMINATED) {
        if (next == current_task) {
            // Current task terminated, must switch
            next = next->next ? next->next : task_list_head;
        }
    }

    // Only switch if we found a different ready task
    if (next != current_task && (next->state == TASK_READY || next->state == TASK_NEW)) {
        task_t* old = current_task;
        
        if (old->state == TASK_RUNNING) {
            old->state = TASK_READY;
        }
        
        next->state = TASK_RUNNING;
        current_task = next;
        
        // Switch page directory and TSS
        paging_switch_directory(next->page_directory);
        tss_set_stack(next->kernel_stack);
        
        // Perform context switch
        switch_to(&old->kernel_esp, next->kernel_esp);
    }
}

void task_sleep(uint32_t ticks) {
    if (current_task) {
        current_task->sleep_ticks = ticks;
        current_task->state = TASK_WAITING;
        schedule();
    }
}

void task_update_sleep() {
    task_t* t = task_list_head;
    while (t) {
        if (t->state == TASK_WAITING && t->sleep_ticks > 0) {
            t->sleep_ticks--;
            if (t->sleep_ticks == 0) {
                t->state = TASK_READY;
            }
        }
        t = t->next;
    }
}

void task_switch(task_t* task) {
    if (!task || task == current_task) return;
    current_task = task;
    paging_switch_directory(task->page_directory);
    tss_set_stack(task->kernel_stack);
}

void task_run(task_t* task) {
    current_task = task;
    task->state = TASK_RUNNING;
    paging_switch_directory(task->page_directory);
    tss_set_stack(task->kernel_stack);
    
    // Jump to the task's saved context
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

int task_get_procs(proc_info_t* buf, int max) {
    int count = 0;
    task_t* t = task_list_head;
    while (t && count < max) {
        if (t->state != TASK_TERMINATED) {
            buf[count].pid = t->id;
            buf[count].state = (uint32_t)t->state;
            strcpy(buf[count].name, t->name);
            count++;
        }
        t = t->next;
    }
    return count;
}

int task_kill(uint32_t pid) {
    if (pid == 1) return -1;
    task_t* t = task_find(pid);
    if (!t) return -1;
    t->state = TASK_TERMINATED;
    return 0;
}

int task_wait(uint32_t pid) {
    if (!current_task) return -1;
    
    task_t* child = task_find(pid);
    if (!child || child->parent_id != current_task->id) {
        return -1; // Not our child
    }
    
    if (child->state == TASK_TERMINATED) {
        return 0; // Already terminated
    }
    
    // Block until child terminates
    current_task->wait_pid = pid;
    current_task->state = TASK_WAITING;
    schedule();
    
    return 0;
}
