#include "../include/task.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/gdt.h"
#include "../include/paging.h"
#include "../include/console.h"
#include "../include/vga.h"
#include "../include/interrupt.h"
#include "../include/fs.h"
#include "../include/loader.h"
#include "../include/syscall.h"
#include "../include/pipe.h"

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
    if (!current_task) return;
    memset(current_task, 0, sizeof(task_t));

    current_task->id = 0; // Kernel is PID 0
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
    
    // Allocate TTY for kernel task
    current_task->tty = tty_create();
    if (current_task->tty) {
        extern void* active_tty_ptr;
        active_tty_ptr = current_task->tty;
        
        // Initialize FD table for kernel
        current_task->fd_table[0].in_use = 1;
        current_task->fd_table[0].type = FD_TYPE_TTY;
        current_task->fd_table[0].ptr = current_task->tty;
        current_task->fd_table[0].flags = 0; // O_RDONLY
        
        current_task->fd_table[1].in_use = 1;
        current_task->fd_table[1].type = FD_TYPE_TTY;
        current_task->fd_table[1].ptr = current_task->tty;
        current_task->fd_table[1].flags = 1; // O_WRONLY
        
        current_task->fd_table[2].in_use = 1;
        current_task->fd_table[2].type = FD_TYPE_TTY;
        current_task->fd_table[2].ptr = current_task->tty;
        current_task->fd_table[2].flags = 1; // O_WRONLY
    }

    task_list_head = current_task;
    tss_set_stack(current_task->kernel_stack);
}

/**
 * @brief Create a new task with initialized stack
 */
task_t* task_create(uint32_t parent_id, page_directory_t* dir, void* tty) {
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
    task->video_memory = 0;
    task->console_row = 0;
    task->console_col = 0;
    
    if (tty) {
        task->tty = tty;
        tty_reference((tty_t*)tty);
    } else {
        // Allocate TTY for this task
        task->tty = tty_create();
    }
    
    if (task->tty) {
        // Initialize standard FDs
        task->fd_table[0].in_use = 1;
        task->fd_table[0].type = FD_TYPE_TTY;
        task->fd_table[0].ptr = task->tty;
        task->fd_table[0].flags = 0;
        
        task->fd_table[1].in_use = 1;
        task->fd_table[1].type = FD_TYPE_TTY;
        task->fd_table[1].ptr = task->tty;
        task->fd_table[1].flags = 1;
        
        task->fd_table[2].in_use = 1;
        task->fd_table[2].type = FD_TYPE_TTY;
        task->fd_table[2].ptr = task->tty;
        task->fd_table[2].flags = 1;
    }
    
    if (!task->tty && !tty) {
        kfree(task);
        return 0;
    }
    
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
    
    task_t* child = task_create(current_task->id, new_dir, current_task->tty);
    if (!child) {
        paging_free_directory(new_dir);
        return 0;
    }
    
    child->user_stack_top = current_task->user_stack_top;
    child->uid = current_task->uid;
    child->gid = current_task->gid;
    strcpy(child->name, current_task->name);
    child->state = TASK_READY;
    
    // Copy FD table
    for (int i = 0; i < MAX_FDS; i++) {
        child->fd_table[i] = current_task->fd_table[i];
        
        // Reference counting for pipes
        if (child->fd_table[i].in_use && child->fd_table[i].type == FD_TYPE_PIPE) {
            pipe_t* p = (pipe_t*)child->fd_table[i].ptr;
            if ((child->fd_table[i].flags & 3) == O_RDONLY) p->readers++;
            else if ((child->fd_table[i].flags & 3) == O_WRONLY) p->writers++;
        }
    }
    
    // Copy parent's TTY state to child (task_create already allocated TTY)
    if (current_task->tty && child->tty) {
        tty_t* parent_tty = (tty_t*)current_task->tty;
        tty_t* child_tty = (tty_t*)child->tty;
        memcpy(child_tty->buffer, parent_tty->buffer, CONSOLE_SIZE * sizeof(uint16_t));
        child_tty->cursor_x = parent_tty->cursor_x;
        child_tty->cursor_y = parent_tty->cursor_y;
        child_tty->scroll_offset = parent_tty->scroll_offset;
        child_tty->content_end_y = parent_tty->content_end_y;
    }
    
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
 * @brief Internal helper to clean up a terminated process
 */
static void task_do_cleanup(task_t* t, int status) {
    // PID 1 is the Init process. If it exits or is killed, the system must panic.
    if (t->id == 1) {
        console_print_colored("\n[ FATAL ] Init process (PID 1) terminated!\n", COLOR_LIGHT_RED);
        console_print_colored("System Halted.\n", COLOR_LIGHT_RED);
        while(1) __asm__ volatile("cli; hlt");
    }

    // Restore focus to parent if we were the active terminal
    extern uint32_t active_terminal_pid;
    extern void* active_tty_ptr;
    if (active_terminal_pid == t->id) {
        active_terminal_pid = t->parent_id;
        task_t* parent = task_find(active_terminal_pid);
        if (parent) active_tty_ptr = parent->tty;
    }
    
    // Close all open file descriptors
    syscall_close_all(t);

    // Save status
    t->exit_status = status;

    // Wake up any parent waiting for us
    if (t->parent_id) {
        task_t* parent = task_find(t->parent_id);
        if (parent && (parent->wait_pid == t->id || parent->wait_pid == (uint32_t)-1)) {
            parent->state = TASK_READY;
        }
    }
    
    // Adopt orphans: All children of the exiting process are reparented to PID 1 (Init)
    task_t* scan = task_list_head;
    while (scan) {
        if (scan->parent_id == t->id) {
            scan->parent_id = 1; // Adopted by Init
        }
        scan = scan->next;
    }

    t->state = TASK_ZOMBIE;
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

    task_do_cleanup(current_task, status);
    
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
    while (loops < 100) {
        // Check CURRENT 'next' candidate before advancing
        // ALLOW TASK_BACKGROUND to run! They just don't have focal input.
        if (next->state == TASK_READY || next->state == TASK_NEW || next->state == TASK_BACKGROUND) {
            break;
        }
        
        next = next->next;
        if (!next) next = task_list_head;
        loops++;
    }

    // Skip terminated tasks
    if (next->state == TASK_TERMINATED || next->state == TASK_ZOMBIE) {
        if (next == current_task) {
            // Current task terminated or zombie, must switch
            next = next->next ? next->next : task_list_head;
        }
    }

    // Only switch if we found a different ready task
    if (next != current_task && (next->state == TASK_READY || next->state == TASK_NEW || next->state == TASK_BACKGROUND)) {
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
        
        while (current_task->state == TASK_WAITING) {
            schedule();
            // If we are still waiting after schedule returns, it means no other task ran.
            // We must wait for an interrupt (timer) to wake us.
            if (current_task->state == TASK_WAITING) {
                __asm__ volatile("sti; hlt; cli");
            }
        }
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
    extern uint32_t active_terminal_pid;
    extern void* active_tty_ptr;
    active_terminal_pid = task->id;
    active_tty_ptr = task->tty;
    current_task = task;
    task->state = TASK_RUNNING;
    paging_switch_directory(task->page_directory);
    
    // Refresh screen for new focal task
    if (task->tty) {
        console_update_vga_tty((tty_t*)task->tty);
    }
    
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
        if (t->state != TASK_TERMINATED && t->id != 0) { // Skip kernel task (id 0)
            buf[count].pid = t->id;
            buf[count].ppid = t->parent_id;
            buf[count].state = (uint32_t)t->state;
            strcpy(buf[count].name, t->name);
            count++;
        }
        t = t->next;
    }
    return count;
}

int task_kill(uint32_t pid) {
    if (pid == 0) return -1; // Cannot kill kernel
    
    task_t* t = task_find(pid);
    if (!t || t->state == TASK_ZOMBIE) return -1;

    if (t == current_task) {
        task_exit(0);
    }
    
    task_do_cleanup(t, 0);
    return 0;
}

int task_wait(uint32_t pid, int* status) {
    if (!current_task) return -1;
    
    while (1) {
        task_t* child = 0;
        task_t* prev = 0;
        task_t* t = task_list_head;
        int has_children = 0;
        
        while (t) {
            if (t->parent_id == current_task->id) {
                if (pid == (uint32_t)-1 || t->id == pid) {
                    has_children = 1;
                    if (t->state == TASK_ZOMBIE) {
                        child = t;
                        break;
                    }
                }
            }
            prev = t;
            t = t->next;
        }
        
        if (!has_children) {
            console_print("[DEBUG] task_wait: no children found\n");
            return -1; // No such child or no children at all
        }
        
        if (child) {
            // Found a zombie to reap
            console_print("[DEBUG] task_wait: found zombie to reap, PID=");
            char pidbuf[12];
            int_to_str(child->id, pidbuf);
            console_print(pidbuf);
            console_print("\n");
            
            uint32_t cid = child->id;
            
            // Remove from task list
            if (child == task_list_head) {
                task_list_head = child->next;
            } else if (prev) {
                // We need to re-find prev correctly if child wasn't head
                // For simplicity in this logic, let's just re-find child in list
                task_t* search = task_list_head;
                task_t* s_prev = 0;
                while (search && search != child) {
                    s_prev = search;
                    search = search->next;
                }
                if (s_prev) s_prev->next = child->next;
            }
            
            // Free child resources
            if (child->page_directory && child->page_directory != kernel_page_directory) {
                paging_free_directory(child->page_directory);
            }
            
            // Free kernel stack
            if (child->kernel_stack) {
                pmm_free_page((void*)(child->kernel_stack - PAGE_SIZE));
            }

            // Save status if requested
            if (status) *status = child->exit_status;

            // Cleanup TTY
            if (child->tty) {
                tty_destroy((tty_t*)child->tty);
            }
            
            kfree(child);
            return cid;
        }
        
        // Wait for a child to exit
        current_task->wait_pid = (pid == (uint32_t)-1) ? (uint32_t)-1 : pid;
        current_task->state = TASK_WAITING;
        
        schedule();
        if (current_task->state == TASK_WAITING) {
            __asm__ volatile("sti; hlt; cli");
        }
    }
}




static uint32_t focused_pid = 0;
static int switcher_active = 0;

int task_is_switcher_active() {
    return switcher_active;
}



// Global variable to track which task owns the terminal display
uint32_t active_terminal_pid = 0; // Default to kernel (PID 0)

int task_is_active_terminal(uint32_t pid) {
    return pid == active_terminal_pid;
}

// Modify switch confirm to set active terminal and keep old task running
int task_try_switch_confirm() {
    if (!switcher_active) return 0;
    
    switcher_active = 0;
    
    // Clear box area (redraw clear)
    int box_x = 60;
    int box_y = 1;
    int box_w = 20;
    int box_h = 10;
    for (int y = 0; y < box_h; y++) {
        for (int x = 0; x < box_w; x++) {
            vga_draw_char_at(box_x + x, box_y + y, ' ', 0x07); // Clear to default color
        }
    }
    
    // Perform Switch
    task_t* target = task_find(focused_pid);
    if (!target || target->state == TASK_TERMINATED) {
        outb(0x20, 0x20); return 1;
    }
    
    if (target == current_task) {
        outb(0x20, 0x20); return 1;
    }
    
    task_t* old = current_task;
    
    // Fix: Keep old task RUNNING (READY), don't pause it!
    // This allows background tasks to continue logic.
    if (old->state == TASK_RUNNING) {
        old->state = TASK_READY;
    } else if (old->state != TASK_WAITING) {
        // Only set to READY if it wasn't waiting/sleeping
        old->state = TASK_READY;
    }
    
    // Resume target task
    target->state = TASK_RUNNING;
    current_task = target;
    
    // UPDATE ACTIVE TERMINAL OWNER
    active_terminal_pid = target->id;
    extern void* active_tty_ptr;
    active_tty_ptr = target->tty;
    
    paging_switch_directory(target->page_directory);
    tss_set_stack(target->kernel_stack);
    
    // --- TTY RESTORE ---
    // Restore the target task's TTY to VGA
    if (target->tty) {
        console_update_vga_tty((tty_t*)target->tty);
    } else {
        // If no TTY, clear screen
        for(int i=0; i<80*25; i++) ((uint16_t*)0xB8000)[i] = 0x0720; 
    }
    
    outb(0x20, 0x20); 
    switch_to(&old->kernel_esp, target->kernel_esp);
    
    return 1;
}

void task_cycle_focus() {
    switcher_active = 1;

    // Draw background box for task list (Top Right)
    int box_x = 60;
    int box_y = 1;
    int box_w = 20;
    int box_h = 10;
    
    // Clear box area
    for (int y = 0; y < box_h; y++) {
        for (int x = 0; x < box_w; x++) {
            vga_draw_char_at(box_x + x, box_y + y, ' ', 0x1F); // Blue background
        }
    }
    
    vga_draw_string_at(box_x + 1, box_y, "TASKS (Ctrl+J)", 0x1F);
    
    // --- Selection Logic ---
    
    // Eligibility criteria for switcher listing
    // Hide PID 1 (init) from the switcher
    #define IS_SWITCHABLE(t) (t->state != TASK_TERMINATED && t->state != TASK_ZOMBIE && t->id > 1)

    // Handle initial focus or invalid focus
    task_t* current_focused = task_find(focused_pid);
    if (!current_focused || !IS_SWITCHABLE(current_focused)) {
        focused_pid = current_task->id;
        current_focused = current_task;
    }

    // Find the next task to focus in the cycle
    task_t* next_to_focus = NULL;
    task_t* first_eligible = NULL;
    task_t* scan = task_list_head;
    int found_current = 0;
    
    while (scan) {
        if (IS_SWITCHABLE(scan)) {
            if (!first_eligible) first_eligible = scan;
            
            if (found_current && !next_to_focus) {
                next_to_focus = scan;
            }
            
            if (scan->id == focused_pid) {
                found_current = 1;
            }
        }
        scan = scan->next;
    }
    
    // Rotation: if we didn't find a "next" after current, wrap to first
    if (!next_to_focus) next_to_focus = first_eligible;
    
    if (next_to_focus) {
        focused_pid = next_to_focus->id;
    } else {
        vga_draw_string_at(box_x + 2, box_y + 2, "No tasks", 0x17);
        focused_pid = 0;
        return;
    }

    // --- Rendering Logic ---
    int list_y = box_y + 2;
    int count = 0;
    scan = task_list_head;
    
    while (scan && count < 8) {
        if (IS_SWITCHABLE(scan)) {
            int is_selected = (scan->id == focused_pid);
            
            // Highlight selected entry
            char bg_color = is_selected ? 0x3E : 0x1F;
            char text_color = is_selected ? 0x3E : 0x17;
            
            for (int x = 0; x < box_w - 1; x++) {
                vga_draw_char_at(box_x + 1 + x, list_y, ' ', bg_color);
            }
            
            vga_draw_char_at(box_x + 1, list_y, is_selected ? '>' : ' ', text_color);
            vga_draw_string_at(box_x + 2, list_y, scan->name, text_color);
            
            list_y++;
            count++;
        }
        scan = scan->next;
    }
}
/**
 * @brief Spawns a fresh shell in a NEW independent TTY
 */
void task_spawn_shell() {
    console_print_colored("\nSpawning New Terminal...\n", COLOR_LIGHT_CYAN);
    
    // 1. Create a NEW TTY
    tty_t* new_tty = tty_create();
    if (!new_tty) return;

    // 2. Prepare arguments for bash
    char* argv[] = {"/bin/bash", NULL};
    
    // 3. Find the file
    fs_node_t* node = fs_find_node("/bin/bash", fs_root_id);
    if (!node) {
        console_print_colored("Error: /bin/bash not found\n", COLOR_LIGHT_RED);
        tty_destroy(new_tty);
        return;
    }

    // 4. Create proper address space
    page_directory_t* new_dir = paging_create_directory();
    
    // 5. Create the task with NEW TTY
    task_t* new_task = task_create(1, new_dir, new_tty); // Parent is PID 1 (shared context)
    if (!new_task) {
        paging_free_directory(new_dir);
        tty_destroy(new_tty);
        return;
    }
    
    // 6. Explicitly set names
    strncpy(new_task->name, "bash_new", 31);
    
    // 7. Initialize TTY file descriptors
    new_task->fd_table[0].in_use = 1;
    new_task->fd_table[0].type = FD_TYPE_TTY;
    new_task->fd_table[0].ptr = new_tty;
    
    new_task->fd_table[1].in_use = 1;
    new_task->fd_table[1].type = FD_TYPE_TTY;
    new_task->fd_table[1].ptr = new_tty;
    
    new_task->fd_table[2].in_use = 1;
    new_task->fd_table[2].type = FD_TYPE_TTY;
    new_task->fd_table[2].ptr = new_tty;

    // 8. Load (but don't run yet!)
    load_user_program(new_task, "/bin/bash", 1, argv);
    
    // 9. Mark it READY so the scheduler picks it up
    new_task->state = TASK_READY;
    
    // 10. Bring it to focus immediately
    extern uint32_t active_terminal_pid;
    extern void* active_tty_ptr;
    active_terminal_pid = new_task->id;
    active_tty_ptr = new_task->tty;
    
    // Refresh VGA screen for the new shell
    console_update_vga_tty(new_tty);
}
