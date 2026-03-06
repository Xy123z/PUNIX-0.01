// src/interrupt.c - Interrupt handling, keyboard driver, and timer
//
// Keyboard handler now routes all input through the TTY line discipline
// (tty_ld_input) instead of directly feeding a per-task pipe.

#include "../include/interrupt.h"
#include "../include/types.h"
#include "../include/string.h"
#include "../include/tty.h"
#include "../include/task.h"
#include "../include/syscall.h"

// ─── Modifier state ───────────────────────────────────────────────────────
#define LCTRL_SC  0x1D
#define LSHIFT_SC 0x2A
#define RSHIFT_SC 0x36
#define ALT_SC    0x38

static volatile int ctrl_pressed  = 0;
static volatile int shift_pressed = 0;
static volatile int alt_pressed   = 0;

// ─── Fallback keyboard buffer (pre-TTY, kernel-only) ─────────────────────
#define KBD_BUF 256
static char kbd_buf[KBD_BUF];
static volatile int kbd_rp = 0, kbd_wp = 0;

// ─── Scancode tables ─────────────────────────────────────────────────────
static const char sc_normal[] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,
    '\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};
static const char sc_shift[] = {
    0, 27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',0,
    '|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
};

// ─── Timer ───────────────────────────────────────────────────────────────
uint32_t timer_ticks = 0;

void timer_init(uint32_t freq) {
    uint32_t div = 1193182 / freq;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(div & 0xFF));
    outb(0x40, (uint8_t)(div >> 8));
}

void timer_handler(registers_t* regs) {
    timer_ticks++;
    outb(0x20, 0x20); // EOI
    extern void task_update_sleep(void);
    task_update_sleep();
    schedule();
}

// ─── I/O ─────────────────────────────────────────────────────────────────
inline uint8_t inb(uint16_t port) {
    uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v;
}
inline void outb(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port));
}

// ─── Keyboard handler (IRQ 1) ─────────────────────────────────────────────
void keyboard_handler(void) {
    uint8_t sc = inb(0x60);

    // Track modifier keys
    if (sc == LCTRL_SC)              { ctrl_pressed  = 1; goto eoi; }
    if (sc == (LCTRL_SC  | 0x80))   { ctrl_pressed  = 0; goto eoi; }
    if (sc == LSHIFT_SC || sc == RSHIFT_SC)          { shift_pressed = 1; goto eoi; }
    if (sc == (LSHIFT_SC | 0x80) || sc == (RSHIFT_SC|0x80)) { shift_pressed = 0; goto eoi; }
    if (sc == ALT_SC)                { alt_pressed   = 1; goto eoi; }
    if (sc == (ALT_SC    | 0x80))   { alt_pressed   = 0; goto eoi; }

    if (sc & 0x80) goto eoi; // Key release — ignore

    // ── Scroll hotkeys (arrow keys) ──────────────────────────────────
    if (sc == 0x48) { extern void console_scroll_up(void);   console_scroll_up();   goto eoi; }
    if (sc == 0x50) { extern void console_scroll_down(void); console_scroll_down(); goto eoi; }

    // ── Alt+F1..F4: switch virtual TTY ───────────────────────────────
    if (alt_pressed) {
        if (sc == 0x3B) { tty_switch(0); goto eoi; } // Alt+F1
        if (sc == 0x3C) { tty_switch(1); goto eoi; } // Alt+F2
        if (sc == 0x3D) { tty_switch(2); goto eoi; } // Alt+F3
        if (sc == 0x3E) { tty_switch(3); goto eoi; } // Alt+F4
    }

    // ── Translate to ASCII ────────────────────────────────────────────
    if (sc >= sizeof(sc_normal)) goto eoi;
    char c = shift_pressed ? sc_shift[sc] : sc_normal[sc];
    if (c == 0) goto eoi;

    // Apply Ctrl mask
    if (ctrl_pressed) {
        if (c >= 'a' && c <= 'z') c = c - 'a' + 1;
        else if (c >= 'A' && c <= 'Z') c = c - 'A' + 1;
        else goto eoi;
    }

    // ── Ctrl+N: cycle virtual TTY ─────────────────────────────────────
    if (c == 14) { // Ctrl+N
        static int next_tty_idx = 0;
        next_tty_idx = (next_tty_idx + 1) % TTY_COUNT;
        tty_switch(next_tty_idx);
        goto eoi;
    }

    // ── Route to active TTY line discipline ───────────────────────────
    if (active_tty) {
        tty_ld_input(active_tty, c);
    } else {
        // Pre-TTY fallback: raw keyboard buffer
        kbd_buf[kbd_wp] = c;
        kbd_wp = (kbd_wp + 1) % KBD_BUF;
    }

eoi:
    outb(0x20, 0x20);
}

// ─── keyboard_read: blocking read for kernel-side use ─────────────────────
char keyboard_read(void) {
    // If TTY is up, read from active TTY's input pipe
    if (active_tty && active_tty->input_pipe) {
        char c;
        while (active_tty->input_pipe->size == 0)
            __asm__ volatile("sti; hlt; cli");
        pipe_read(active_tty->input_pipe, (uint8_t*)&c, 1);
        return c;
    }
    // Pre-TTY fallback
    while (kbd_rp == kbd_wp)
        __asm__ volatile("sti; hlt; cli");
    char c = kbd_buf[kbd_rp];
    kbd_rp = (kbd_rp + 1) % KBD_BUF;
    return c;
}

int keyboard_has_data(void) {
    if (active_tty && active_tty->input_pipe)
        return active_tty->input_pipe->size > 0;
    return kbd_rp != kbd_wp;
}

void keyboard_init(void) { kbd_rp = kbd_wp = 0; }

// ─── Assembly wrappers ───────────────────────────────────────────────────
extern void keyboard_interrupt_handler(void);
__asm__(
    ".global keyboard_interrupt_handler\n"
    "keyboard_interrupt_handler:\n"
    "   cli\n"
    "   pusha\n"
    "   push %ds\n" "   push %es\n" "   push %fs\n" "   push %gs\n"
    "   mov $0x10, %ax\n"
    "   mov %ax, %ds\n" "   mov %ax, %es\n"
    "   mov %ax, %fs\n" "   mov %ax, %gs\n"
    "   call keyboard_handler\n"
    "   pop %gs\n" "   pop %fs\n" "   pop %es\n" "   pop %ds\n"
    "   popa\n"
    "   iret\n"
);

extern void page_fault_interrupt_handler(void);
__asm__(
    ".global page_fault_interrupt_handler\n"
    "page_fault_interrupt_handler:\n"
    "   pusha\n"
    "   mov %cr2, %eax\n"
    "   push %eax\n"
    "   mov 36(%esp), %eax\n"
    "   push %eax\n"
    "   call page_fault_handler\n"
    "   add $8, %esp\n"
    "   popa\n"
    "   add $4, %esp\n"
    "   iret\n"
);

extern void timer_interrupt_handler(void);
__asm__(
    ".global timer_interrupt_handler\n"
    "timer_interrupt_handler:\n"
    "   cli\n"
    "   pusha\n"
    "   push %ds\n" "   push %es\n" "   push %fs\n" "   push %gs\n"
    "   mov $0x10, %ax\n"
    "   mov %ax, %ds\n" "   mov %ax, %es\n"
    "   mov %ax, %fs\n" "   mov %ax, %gs\n"
    "   push %esp\n"
    "   call timer_handler\n"
    "   add $4, %esp\n"
    "   pop %gs\n" "   pop %fs\n" "   pop %es\n" "   pop %ds\n"
    "   popa\n"
    "   iret\n"
);

extern void serial_interrupt_handler(void);
__asm__(
    ".global serial_interrupt_handler\n"
    "serial_interrupt_handler:\n"
    "   cli\n"
    "   pusha\n"
    "   push %ds\n" "   push %es\n" "   push %fs\n" "   push %gs\n"
    "   mov $0x10, %ax\n"
    "   mov %ax, %ds\n" "   mov %ax, %es\n"
    "   mov %ax, %fs\n" "   mov %ax, %gs\n"
    "   call serial_handler\n"
    "   pop %gs\n" "   pop %fs\n" "   pop %es\n" "   pop %ds\n"
    "   popa\n"
    "   iret\n"
);

extern void serial_interrupt_handler_irq3(void);
__asm__(
    ".global serial_interrupt_handler_irq3\n"
    "serial_interrupt_handler_irq3:\n"
    "   cli\n"
    "   pusha\n"
    "   push %ds\n" "   push %es\n" "   push %fs\n" "   push %gs\n"
    "   mov $0x10, %ax\n"
    "   mov %ax, %ds\n" "   mov %ax, %es\n"
    "   mov %ax, %fs\n" "   mov %ax, %gs\n"
    "   call serial_handler_irq3\n"      // <-- only difference
    "   pop %gs\n" "   pop %fs\n" "   pop %es\n" "   pop %ds\n"
    "   popa\n"
    "   iret\n"
);

// ─── IDT setup ───────────────────────────────────────────────────────────
struct idt_entry idt[256];
struct idt_ptr   idtp;

static void idt_set_gate(int n, uint32_t handler, uint16_t sel, uint8_t flags) {
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].selector    = sel;
    idt[n].zero        = 0;
    idt[n].type_attr   = flags;
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

void idt_init(void) {
    idtp.limit = sizeof(struct idt_entry) * 256 - 1;
    idtp.base  = (uint32_t)&idt;
    for (int i = 0; i < 256; i++) idt_set_gate(i, 0, 0, 0);
    idt_set_gate(14,   (uint32_t)page_fault_interrupt_handler, 0x08, 0x8E);
    idt_set_gate(32,   (uint32_t)timer_interrupt_handler,      0x08, 0x8E);
    idt_set_gate(33,   (uint32_t)keyboard_interrupt_handler,   0x08, 0x8E);
    idt_set_gate(36,   (uint32_t)serial_interrupt_handler,     0x08, 0x8E); // IRQ4 for COM1
    idt_set_gate(35, (uint32_t)serial_interrupt_handler_irq3, 0x08, 0x8E); // IRQ3 → COM2/COM4
    idt_set_gate(0x80, (uint32_t)syscall_interrupt_wrapper,    0x08, 0xEE);
    __asm__ volatile("lidt %0"::"m"(idtp));
}

void pic_init(void) {
    outb(0x20, 0x11); outb(0x21, 0x20); outb(0x21, 0x04); outb(0x21, 0x01);
    outb(0xA0, 0x11); outb(0xA1, 0x28); outb(0xA1, 0x02); outb(0xA1, 0x01);
    outb(0x21, 0xE4); // 1110 0100 → unmasks IRQ0, IRQ1, IRQ3, IRQ4
    outb(0xA1, 0xFF);
}
