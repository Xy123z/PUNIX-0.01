 
// include/interrupt.h - Interrupt handling

#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "types.h"

 uint8_t inb(uint16_t port);
 void outb(uint16_t port, uint8_t value);
// IDT structures
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// Interrupt initialization
void idt_init();
void pic_init();
void timer_init(uint32_t frequency);

// Keyboard
#define KEYBOARD_BUFFER_SIZE 256

void keyboard_init();
char keyboard_read();
void keyboard_read_line(char* buffer, int max_len);
int keyboard_has_data();

#endif // INTERRUPT_H
