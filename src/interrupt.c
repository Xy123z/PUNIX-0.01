// src/interrupt.c - Interrupt handling and keyboard driver
#include "../include/interrupt.h"
#include "../include/types.h" // For uint8_t, uint16_t, uint32_t
#include "../include/string.h"
#include "../include/console.h"
#include "../include/syscall.h"
// --- Scrolling Scan Codes ---
#define SC_ARROW_UP   0x48
#define SC_ARROW_DOWN 0x50
// ----------------------------

// Scancode Definitions for Control Key and target letters
#define LCTRL_SCANCODE 0x1D
#define LSHIFT_SCANCODE 0x2A
#define RSHIFT_SCANCODE 0x36
#define S_SCANCODE     0x1F  // Scancode for the 'S' key
#define X_SCANCODE     0x2D  // Scancode for the 'X' key
#define J_SCANCODE     0x24  // Scancode for the 'J' key

extern void task_cycle_focus();
extern int task_try_switch_confirm();
extern int task_is_switcher_active();

// ASCII Control Codes (Used by the editor in text.c and system shortcuts)
#define CTRL_S 0x13 // ASCII 19
#define CTRL_X 0x18 // ASCII 24
#define CTRL_N 0x0E // ASCII 14

// IDT
struct idt_entry idt[256];
struct idt_ptr idtp;

// Keyboard buffer (circular)
#define KEYBOARD_BUFFER_SIZE 256
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile int kbd_read_pos = 0;
static volatile int kbd_write_pos = 0;

// NEW: Global state flags to track modifiers
static volatile int ctrl_pressed = 0;
static volatile int shift_pressed = 0;

// --- External Console/VGA Functions (Assumed to be defined elsewhere) ---
// These are needed for scrolling the display
extern void console_scroll_up();
extern void console_scroll_down();
// -------------------------------------------------------------------------

uint32_t timer_ticks = 0;

void timer_init(uint32_t frequency) {
    uint32_t divisor = 1193182 / frequency;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

#include "../include/task.h"

void timer_handler(registers_t* regs) {
    timer_ticks++;
    
    // Send EOI immediately
    outb(0x20, 0x20);

    // Update sleeping tasks
    task_update_sleep();

    // Call scheduler - it uses switch_to which preserves stack
    schedule();
}

// Port I/O
inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// Keyboard scancode to ASCII mapping
static const char scancode_to_ascii[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

static const char scancode_to_ascii_shift[] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~', 0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

// Keyboard interrupt handler (IRQ1)
void keyboard_handler() {
    uint8_t scancode = inb(0x60);

    // --- Control Key State Tracking ---
    if (scancode == LCTRL_SCANCODE) {
        ctrl_pressed = 1;
        goto end_handler;
    }
    // Check for key release (Scancode | 0x80)
    else if (scancode == (LCTRL_SCANCODE | 0x80)) {
        ctrl_pressed = 0;
        goto end_handler;
    }
    // --- Shift Key State Tracking ---
    else if (scancode == LSHIFT_SCANCODE || scancode == RSHIFT_SCANCODE) {
        shift_pressed = 1;
        goto end_handler;
    } else if (scancode == (LSHIFT_SCANCODE | 0x80) || scancode == (RSHIFT_SCANCODE | 0x80)) {
        shift_pressed = 0;
        goto end_handler;
    }
    // ----------------------------------

    // Only handle key presses (not releases)
    if (!(scancode & 0x80)) {

        // --- SCROLLING LOGIC ADDED HERE ---
        if (scancode == SC_ARROW_UP) {
            console_scroll_up();
            goto end_handler;
        } else if (scancode == SC_ARROW_DOWN) {
            console_scroll_down();
            goto end_handler;
        }
        
        // --- Global Task Switching ---
        if (ctrl_pressed && scancode == J_SCANCODE) {
            task_cycle_focus();
            goto end_handler;
        }
        
        if (task_is_switcher_active()) {
            if (scancode == 0x1C) { // Enter Key
                 if (task_try_switch_confirm()) {
                     // The Task Switcher has already sent EOI before switching.
                     // (Because if it didn't, the unblocked task wouldn't receive interrupts).
                     // So we must return immediately to avoid sending Double EOI.
                     return;
                 }
            }
            // Consume ALL other keys when switcher is active
            goto end_handler;
        }
        // -----------------------------

        // ----------------------------------

        if (scancode < sizeof(scancode_to_ascii)) {
            char c = shift_pressed ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];

            // --- Control Character Mapping ---
            if (ctrl_pressed) {
                if (scancode == S_SCANCODE) {
                    c = CTRL_S;
                } else if (scancode == X_SCANCODE) {
                    c = CTRL_X;
                } else if (scancode == 0x31) { // 'N' key
                    c = CTRL_N;
                } else {
                    // Ignore other control key combinations to prevent unhandled control chars
                    goto end_handler;
                }
            }
            // ---------------------------------
            
            // --- Custom System Hotkeys ---
            if (ctrl_pressed && scancode == 0x31) { // Ctrl+N
                extern void task_spawn_shell();
                task_spawn_shell();
                goto end_handler;
            }
            // ------------------------------

            if (c != 0) {
                // Determine which TTY to feed
                extern task_t* current_task;
                extern task_t* task_find(uint32_t pid);
                extern uint32_t active_terminal_pid; // From task.c
                
                task_t* target_task = task_find(active_terminal_pid);
                // Fallback to kernel (PID 0) if active terminal is invalid, dead, or zombie
                if (!target_task || target_task->state == TASK_TERMINATED || target_task->state == TASK_ZOMBIE) {
                    target_task = task_find(0);
                }

                if (target_task && target_task->tty) {
                    tty_t* tty = (tty_t*)target_task->tty;
                    // Ensure we only feed the task if it's the focal one
                    if (tty->stdin_buf) {
                        pipe_write(tty->stdin_buf, (const uint8_t*)&c, 1);
                    }
                }
 else {
                    // Fallback to old keyboard buffer if no TTY (should not happen in new design)
                    keyboard_buffer[kbd_write_pos] = c;
                    kbd_write_pos = (kbd_write_pos + 1) % KEYBOARD_BUFFER_SIZE;
                }
            }
        }
    }

end_handler:
    // Send End of Interrupt (EOI) to PIC
    outb(0x20, 0x20);
}

// Assembly wrapper for keyboard interrupt
extern void keyboard_interrupt_handler();
__asm__(
    ".global keyboard_interrupt_handler\n"
    "keyboard_interrupt_handler:\n"
    "   cli\n"
    "   pusha\n"            // Push EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX
    "   push %ds\n"
    "   push %es\n"
    "   push %fs\n"
    "   push %gs\n"
    "   mov $0x10, %ax\n"   // Kernel data segment
    "   mov %ax, %ds\n"
    "   mov %ax, %es\n"
    "   mov %ax, %fs\n"
    "   mov %ax, %gs\n"
    "   call keyboard_handler\n"
    "   pop %gs\n"
    "   pop %fs\n"
    "   pop %es\n"
    "   pop %ds\n"
    "   popa\n"
    "   iret\n"
);

// Page fault handler wrapper
extern void page_fault_interrupt_handler();
__asm__(
    ".global page_fault_interrupt_handler\n"
    "page_fault_interrupt_handler:\n"
    "   pusha\n"
    "   mov %cr2, %eax\n"  // Get faulting address from CR2
    "   push %eax\n"        // Push faulting address (Arg2)
    "   mov 36(%esp), %eax\n" // Get error code (pushed by CPU, at ESP+32+4)
    "   push %eax\n"        // Push error code (Arg1)
    "   call page_fault_handler\n"
    "   add $8, %esp\n"     // Clean up args
    "   popa\n"
    "   add $4, %esp\n"     // Remove error code pushed by CPU
    "   iret\n"
);

// Timer interrupt wrapper
extern void timer_interrupt_handler();
__asm__(
    ".global timer_interrupt_handler\n"
    "timer_interrupt_handler:\n"
    "   cli\n"
    "   pusha\n"
    "   push %ds\n"
    "   push %es\n"
    "   push %fs\n"
    "   push %gs\n"
    "   mov $0x10, %ax\n"
    "   mov %ax, %ds\n"
    "   mov %ax, %es\n"
    "   mov %ax, %fs\n"
    "   mov %ax, %gs\n"
    "   push %esp\n"
    "   call timer_handler\n"
    "   add $4, %esp\n"
    "   pop %gs\n"
    "   pop %fs\n"
    "   pop %es\n"
    "   pop %ds\n"
    "   popa\n"
    "   iret\n"
);

// Check if keyboard buffer has data
int keyboard_has_data() {
    extern task_t* current_task;
    if (current_task && current_task->tty) {
        tty_t* tty = (tty_t*)current_task->tty;
        if (tty->stdin_buf && tty->stdin_buf->size > 0) return 1;
    }
    return kbd_read_pos != kbd_write_pos;
}

// Read character from keyboard buffer (with HLT for power saving)
char keyboard_read() {
    extern task_t* current_task;
    if (current_task && current_task->tty) {
        char c;
        extern int tty_read(void* tty, char* buf, int count);
        while (tty_read(current_task->tty, &c, 1) <= 0) {
            __asm__ volatile("sti; hlt; cli");
        }
        return c;
    }

    while (!keyboard_has_data()) {
        __asm__ volatile("sti; hlt; cli");
    }

    char c = keyboard_buffer[kbd_read_pos];
    kbd_read_pos = (kbd_read_pos + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

// Read line function (Uses the new C-style name 'keyboard_read_line')
void keyboard_read_line(char* buffer, int max_len) {
    int i = 0;

    while (i < max_len - 1) {
        char c = keyboard_read();

        if (c == '\n') {
            buffer[i] = '\0';
            // Note: putchar needs to be called from caller
            break;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                // Backspace handled by caller
            }
        } else if ((c >= '0' && c <= '9') || c == '-' ||
                   (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   c == ' ' || c == '.') {
            buffer[i++] = c;
        }
        // IMPORTANT: Control characters like CTRL_S and CTRL_X are NOT handled here,
        // as this function is for reading command lines only.
    }

    buffer[i] = '\0';
}

// Set IDT entry
static void idt_set_gate(int num, uint32_t handler, uint16_t selector, uint8_t flags) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = selector;
    idt[num].zero = 0;
    idt[num].type_attr = flags;
    idt[num].offset_high = (handler >> 16) & 0xFFFF;
}

// Initialize IDT
void idt_init() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;

    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    // Set exception handlers
    idt_set_gate(14, (uint32_t)page_fault_interrupt_handler, 0x08, 0x8E);  // Page fault
    
    // Set keyboard interrupt (IRQ1 = interrupt 33)
    idt_set_gate(33, (uint32_t)keyboard_interrupt_handler, 0x08, 0x8E);
    // Set timer interrupt (IRQ0 = interrupt 32)
    idt_set_gate(32, (uint32_t)timer_interrupt_handler, 0x08, 0x8E);
    // Set syscall interrupt (0x80) with DPL 3 to allow user mode calls
    idt_set_gate(0x80, (uint32_t)syscall_interrupt_wrapper, 0x08, 0xEE);
    __asm__ volatile("lidt %0" : : "m"(idtp));
}

// Initialize PIC
void pic_init() {
    outb(0x20, 0x11);
    outb(0x21, 0x20); // Remap IRQ0-7 to 0x20-0x27
    outb(0x21, 0x04);
    outb(0x21, 0x01);

    outb(0xA0, 0x11);
    outb(0xA1, 0x28); // Remap IRQ8-15 to 0x28-0x2F
    outb(0xA1, 0x02);
    outb(0xA1, 0x01);

    // Unmask IRQ0 (timer) and IRQ1 (keyboard)
    outb(0x21, 0xFC); // 1111 1100 -> IRQ0 and IRQ1 enabled
    outb(0xA1, 0xFF);
}

void keyboard_init() {
    kbd_read_pos = 0;
    kbd_write_pos = 0;
}
