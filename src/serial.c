// src/serial.c - Multi-port Serial Driver (COM1–COM4 → /dev/ttyS0–ttyS3)
//
// COM1: 0x3F8, IRQ4  → ttyS0 (tty_devices[4])
// COM2: 0x2F8, IRQ3  → ttyS1 (tty_devices[5])
// COM3: 0x3E8, IRQ4  → ttyS2 (tty_devices[6])
// COM4: 0x2E8, IRQ3  → ttyS3 (tty_devices[7])
//
// COM1+COM3 share IRQ4; COM2+COM4 share IRQ3.
// Each handler polls both ports on its IRQ to avoid missing characters.

#include "../include/serial.h"
#include "../include/interrupt.h"
#include "../include/tty.h"

// ─── Port base addresses ──────────────────────────────────────────────────
#define COM1_PORT  0x3F8
#define COM2_PORT  0x2F8
#define COM3_PORT  0x3E8
#define COM4_PORT  0x2E8

#define SERIAL_PORT_COUNT  4

// ─── UART register offsets ────────────────────────────────────────────────
#define UART_DATA      0   // RX/TX data (DLAB=0)
#define UART_IER       1   // Interrupt enable register (DLAB=0)
#define UART_BAUD_LO   0   // Baud rate divisor low byte (DLAB=1)
#define UART_BAUD_HI   1   // Baud rate divisor high byte (DLAB=1)
#define UART_IIR       2   // Interrupt identification register (read)
#define UART_FCR       2   // FIFO control register (write)
#define UART_LCR       3   // Line control register
#define UART_MCR       4   // Modem control register
#define UART_LSR       5   // Line status register

#define UART_LSR_DATA_READY   0x01  // Data available to read
#define UART_LSR_TX_EMPTY     0x20  // Transmitter holding register empty
#define UART_IIR_NO_INT       0x01  // Bit 0 = 0 means interrupt pending
#define UART_IIR_RX_DATA      0x04  // Bits 1-2 = 10 → received data available

// ─── Lookup tables ────────────────────────────────────────────────────────

// Base I/O port for each COM port (index 0–3)
static const uint16_t com_ports[SERIAL_PORT_COUNT] = {
    COM1_PORT, COM2_PORT, COM3_PORT, COM4_PORT
};

// Corresponding tty_devices[] index (serial devices start at index 4)
static const int com_tty_index[SERIAL_PORT_COUNT] = { 4, 5, 6, 7 };

// ─── Helpers ─────────────────────────────────────────────────────────────

static inline int serial_tx_ready(uint16_t port) {
    return inb(port + UART_LSR) & UART_LSR_TX_EMPTY;
}

static inline int serial_rx_ready(uint16_t port) {
    return inb(port + UART_LSR) & UART_LSR_DATA_READY;
}

// ─── Init one UART ────────────────────────────────────────────────────────
static void serial_init_port(uint16_t port) {
    outb(port + UART_IER, 0x00);    // Disable interrupts during init
    outb(port + UART_LCR, 0x80);    // Enable DLAB to set baud rate
    outb(port + UART_BAUD_LO, 0x03);// Divisor = 3 → 38400 baud
    outb(port + UART_BAUD_HI, 0x00);
    outb(port + UART_LCR, 0x03);    // 8N1: 8 bits, no parity, 1 stop bit
    outb(port + UART_FCR, 0xC7);    // Enable + clear FIFOs, 14-byte threshold
    outb(port + UART_MCR, 0x0B);    // RTS + DTR + OUT2 (enables IRQ on card)
    outb(port + UART_IER, 0x01);    // Enable Received Data Available interrupt
}

// ─── Public init ─────────────────────────────────────────────────────────
void serial_init(void) {
    for (int i = 0; i < SERIAL_PORT_COUNT; i++) {
        serial_init_port(com_ports[i]);
    }
}

// ─── Output API ───────────────────────────────────────────────────────────

// Write a single character to a specific COM port (used by tty_putchar)
void serial_putchar_port(int com_index, char c) {
    if (com_index < 0 || com_index >= SERIAL_PORT_COUNT) return;
    uint16_t port = com_ports[com_index];
    while (!serial_tx_ready(port));
    outb(port + UART_DATA, (uint8_t)c);
}

// Convenience wrapper: write to COM1 / ttyS0 (keeps callers in tty.c simple)
void serial_putchar(char c) {
    serial_putchar_port(0, c);
}

void serial_print(const char* str) {
    while (*str) serial_putchar(*str++);
}

// ─── Input API (polled, used outside interrupt context) ──────────────────
char serial_read(void) {
    while (!serial_rx_ready(COM1_PORT));
    return (char)inb(COM1_PORT + UART_DATA);
}

// ─── Shared interrupt service routine ────────────────────────────────────
//
// Drain all pending RX bytes from a port and hand them to its tty.
static void serial_rx_port(int com_index) {
    uint16_t port = com_ports[com_index];

    // IIR bit 0 = 0 → interrupt pending on this UART
    uint8_t iir = inb(port + UART_IIR);
    if (iir & UART_IIR_NO_INT) return;                    // No interrupt here
    if ((iir & 0x06) != UART_IIR_RX_DATA) return;        // Not an RX interrupt

    // Drain the FIFO (up to 16 bytes) in one ISR invocation
    while (serial_rx_ready(port)) {
        char c = (char)inb(port + UART_DATA);
        tty_device_t* tty = tty_get(com_tty_index[com_index]);
        if (tty) tty_ld_input(tty, c);
    }
}

// IRQ4 handler → COM1 (ttyS0) and COM3 (ttyS2)
void serial_handler_irq4(void) {
    serial_rx_port(0);   // COM1 → ttyS0
    serial_rx_port(2);   // COM3 → ttyS2
    outb(0x20, 0x20);    // EOI to PIC
}

// IRQ3 handler → COM2 (ttyS1) and COM4 (ttyS3)
void serial_handler_irq3(void) {
    serial_rx_port(1);   // COM2 → ttyS1
    serial_rx_port(3);   // COM4 → ttyS3
    outb(0x20, 0x20);    // EOI to PIC
}

// Legacy alias kept for any existing IDT registration that calls serial_handler()
void serial_handler(void) {
    serial_handler_irq4();
}
