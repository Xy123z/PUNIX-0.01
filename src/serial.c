#include "../include/serial.h"
#include "../include/interrupt.h"
#include "../include/tty.h"

#define COM1_PORT 0x3F8

void serial_init(void) {
    outb(COM1_PORT + 1, 0x01);    // Enable receiver interrupts
    outb(COM1_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1_PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(COM1_PORT + 1, 0x00);    //                  (hi byte)
    outb(COM1_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1_PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(COM1_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

int serial_is_transmit_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

void serial_putchar(char a) {
    while (serial_is_transmit_empty() == 0); // Wait until transmit buffer is empty
    outb(COM1_PORT, a);
}

void serial_print(const char* str) {
    while (*str) {
        serial_putchar(*str++);
    }
}

char serial_read(void) {
    while ((inb(COM1_PORT + 5) & 1) == 0); // Wait until data is available
    return inb(COM1_PORT);
}

// Interupt handler
void serial_handler(void) {
    // Check if interrupt is really for us
    uint8_t iir = inb(COM1_PORT + 2);
    if (!(iir & 1)) {
        if ((iir & 0x06) == 0x04) { // Receiver Data Available
            char c = inb(COM1_PORT);
            // Route to ttyS0 (index 4)
            extern tty_device_t tty_devices[];
            tty_device_t* ttyS0 = &tty_devices[4];
            
            // Note: We bypass keyboard modifiers and hotkeys, as terminal emulators do it for us
            extern void tty_ld_input(tty_device_t* tty, char c);
            tty_ld_input(ttyS0, c);
        }
    }
    // EOI for IRQ4 (COM1)
    outb(0x20, 0x20);
}
