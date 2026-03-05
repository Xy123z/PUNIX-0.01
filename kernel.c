// kernel.c - Main kernel entry point
#include "include/types.h"
#include "include/vga.h"
#include "include/memory.h"
#include "include/paging.h"
#include "include/interrupt.h"
#include "include/shell.h"
#include "include/text.h"
#include "include/fs.h"
#include "include/mouse.h"
#include "include/string.h"
#include "include/console.h"
#include "include/ata.h"
#include "include/auth.h"
#include "include/gdt.h"
#include "include/task.h"
#include "include/task.h"
#include "include/syscall.h"
#include "include/loader.h"
#include "include/serial.h"

// Prototypes
void kernel_main();
void kernel_user_entry();
extern void kernel_after_user(void);
// kernel_main is called by src/boot_entry.asm
void kernel_main() {
    // Initialize serial first so we don't miss any output
    serial_init();
    
    // Initialize VGA
    console_init();
    console_clear_screen();
    
    // 1. Core CPU Architecture
    gdt_init();
    console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
    console_print_colored("GDT and TSS initialized.\n", COLOR_GREEN_ON_BLACK);

    // 2. Memory Management (Identity mapping environment)
    pmm_init();
    heap_init();
    console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
    console_print_colored("Memory manager initialized.\n", COLOR_GREEN_ON_BLACK);

    // 3. System Call & Task Management (Requires Heap)
    syscall_init();
    console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
    console_print_colored("System calls and initial task ready.\n", COLOR_GREEN_ON_BLACK);

    // 4. Paging Setup
    paging_init();
    console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
    console_print_colored("Page tables prepared.\n", COLOR_GREEN_ON_BLACK);

    // 5. Interrupts (MUST be before paging_enable to catch faults)
    idt_init();
    pic_init();
    timer_init(100); // 100 Hz timer
    console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
    console_print_colored("IDT, PIC and Timer configured.\n", COLOR_GREEN_ON_BLACK);

    // 6. Enable Paging
    paging_enable();
    console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
    console_print_colored("Paging enabled.\n", COLOR_GREEN_ON_BLACK);

    // 7. Hardware & Filesystem
    ata_init();
    fs_init();
    syscall_set_cwd(fs_root_id);
    mouse_init();
    console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
    console_print_colored("Hardware and Filesystem ready.\n", COLOR_GREEN_ON_BLACK);

    // ─── TTY Subsystem Initialization ─────────────────────────────────────
    tty_init_all();
    console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
    console_print_colored("TTY devices initialized. /dev/tty0..3 and /dev/ttyS0 ready.\n", COLOR_GREEN_ON_BLACK);

    // Load credentials from disk
    if (auth_load_credentials()) {
        console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
        console_print("Credentials loaded successfully.\n");
    } else {
        console_print_colored("[ !! ] ", COLOR_YELLOW_ON_BLACK);
        console_print("No credentials found, using defaults (root/root).\n");
    }

    // 8. Finalize Kernel Space
    console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
    console_print_colored("Enabling interrupts...\n", COLOR_YELLOW_ON_BLACK);
    __asm__ volatile("sti");

    console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
    console_print_colored("Kernel ready!\n\n", COLOR_GREEN_ON_BLACK);

    // No longer needed: authentication now belongs to userspace login.c
    // auth_init(read_line_with_display);

    kernel_user_entry();
    // Should never reach here
    while(1) {
        __asm__ volatile("hlt");
    }
}
void kernel_user_entry(){
    console_print("Launching system init (/sbin/init)...\n");
    __asm__ volatile("mov %%esp, %0" : "=m"(kernel_esp_saved));
    
    // Launch /sbin/init as the first process (PID 1)
    char* argv[] = {"/sbin/init", NULL};
    task_t* init_task = load_user_program(NULL, "/sbin/init", 1, argv);
    
    if (init_task) {
        // init_task->id = 1; // Handled by next_pid=1 set in task.c
        task_run(init_task);
    }
    
    // Fallback if launch fails
    console_print_colored("Launch failed! falling back to kernel shell.\n", COLOR_LIGHT_RED);
    extern void kern_shell_init();
    kern_shell_init();
}
void kernel_after_user(void) {
    static char* programs[] = {
        "/bin/hello2",
        NULL
    };
    static int current_program = 0;

    if (programs[current_program] != NULL) {
        console_print("Loading program: ");
        console_print(programs[current_program]);
        console_print("\n");

        char* prog = programs[current_program];  // Save pointer
        current_program++;  // Increment before jumping
        char* argv[] = {prog, NULL};
        task_t* next_task = load_user_program(NULL, prog, 1, argv);
        
        if (next_task) {
            task_run(next_task);
        }
    } else {
        console_print_colored("\nAll programs finished. System halting.\n",
                            COLOR_GREEN_ON_BLACK);
        __asm__ volatile("cli");
        while(1) __asm__ volatile("hlt");
    }
}
