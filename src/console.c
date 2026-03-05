// src/console.c - Kernel console: thin wrapper over the new TTY device layer.
//
// All output is routed through tty_dev_write(active_tty, ...).
// Kernel-early path (before tty_init_all) uses direct VGA writes.

#include "../include/console.h"
#include "../include/tty.h"
#include "../include/string.h"
#include "../include/vga.h"
#include "../include/interrupt.h"
#include "../include/tty.h"
#include "../include/serial.h"
#include "../include/task.h"

// ─── Helper: determine target TTY for kernel logs ────────────────────────
static tty_device_t* get_console_tty(void) {
    if (current_task && current_task->ctrl_tty >= 0) {
        tty_device_t* tty = tty_get(current_task->ctrl_tty);
        if (tty) return tty;
    }
    return active_tty;
}

// ─── Early (pre-TTY) VGA fallback ────────────────────────────────────────
#define VGA_MEM  ((uint16_t*)0xB8000)
static int early_x = 0, early_y = 0;

static void early_putchar(char c, uint8_t color) {
    if (c == '\n') { early_x = 0; early_y++; }
    else if (c == '\r') { early_x = 0; }
    else {
        VGA_MEM[early_y * 80 + early_x] = (uint8_t)c | (color << 8);
        if (++early_x >= 80) { early_x = 0; early_y++; }
    }
    if (early_y >= 25) {
        // Scroll up
        for (int i = 0; i < 24 * 80; i++) VGA_MEM[i] = VGA_MEM[i + 80];
        for (int i = 0; i < 80; i++) VGA_MEM[24 * 80 + i] = ' ' | (color << 8);
        early_y = 24;
    }
    // Update HW cursor
    uint16_t pos = early_y * 80 + early_x;
    outb(0x3D4, 0x0F); outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E); outb(0x3D5, pos >> 8);
}

// ─── console_init ────────────────────────────────────────────────────────
void console_init(void) {
    // Clear VGA memory for early display
    for (int i = 0; i < 80 * 25; i++)
        VGA_MEM[i] = ' ' | (COLOR_WHITE_ON_BLACK << 8);
    early_x = early_y = 0;
}

// ─── console_clear_screen ────────────────────────────────────────────────
void console_clear_screen(void) {
    if (active_tty) {
        uint16_t blank = ' ' | (COLOR_WHITE_ON_BLACK << 8);
        for (int i = 0; i < TTY_BUF_SIZE; i++) active_tty->buffer[i] = blank;
        active_tty->cursor_x = 0;
        active_tty->cursor_y = 0;
        active_tty->scroll_offset = 0;
        active_tty->content_end_y = 0;
        tty_flush_vga(active_tty);
    } else {
        for (int i = 0; i < 80 * 25; i++) VGA_MEM[i] = ' ' | (COLOR_WHITE_ON_BLACK << 8);
        early_x = early_y = 0;
    }
}

// ─── console_putchar ─────────────────────────────────────────────────────
void console_putchar(char c, char color) {
    tty_device_t* tty = get_console_tty();
    if (tty) {
        // color 0 means "use TTY current colour"
        tty_putchar(tty, c, (uint8_t)color);
        if (tty == active_tty) tty_flush_vga(tty);
    } else {
        early_putchar(c, (uint8_t)color);
    }
}

// ─── console_print ───────────────────────────────────────────────────────
void console_print(const char* str) {
    if (!str) return;
    tty_device_t* tty = get_console_tty();
    if (tty) {
        tty_dev_write(tty, str, strlen(str));
    } else {
        for (int i = 0; str[i]; i++) early_putchar(str[i], COLOR_WHITE_ON_BLACK);
    }
}

// ─── console_print_colored ───────────────────────────────────────────────
void console_print_colored(const char* str, char color) {
    if (!str) return;
    tty_device_t* tty = get_console_tty();
    if (tty) {
        uint8_t saved = tty->curr_color;
        tty->curr_color = (uint8_t)color;
        tty_dev_write(tty, str, strlen(str));
        tty->curr_color = saved;
    } else {
        for (int i = 0; str[i]; i++) early_putchar(str[i], (uint8_t)color);
    }
}

// ─── read_line_with_display (kernel-side line input) ─────────────────────
// Used only by kernel auth_init before the TTY subsystem hands off to getty.
void read_line_with_display(char* buffer, int max_len) {
    int i = 0;
    while (i < max_len - 1) {
        // Direct keyboard read (kernel path only, before tty ready)
        extern char keyboard_read(void);
        char c = keyboard_read();
        if (c == '\n') {
            buffer[i] = '\0';
            console_putchar('\n', COLOR_WHITE_ON_BLACK);
            break;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                console_putchar('\b', COLOR_WHITE_ON_BLACK);
                console_putchar(' ',  COLOR_WHITE_ON_BLACK);
                console_putchar('\b', COLOR_WHITE_ON_BLACK);
            }
        } else if (c >= ' ' && c <= '~') {
            buffer[i++] = c;
            console_putchar(c, COLOR_WHITE_ON_BLACK);
        }
    }
    buffer[i] = '\0';
}

// ─── Scroll wrappers (for keyboard scroll hotkeys) ────────────────────────
void console_scroll_up(void) {
    if (!active_tty) return;
    int new_off = active_tty->scroll_offset - 1;
    if (new_off < 0) new_off = 0;
    active_tty->scroll_offset = new_off;
    tty_flush_vga(active_tty);
}

void console_scroll_down(void) {
    if (!active_tty) return;
    int max = active_tty->content_end_y - (TTY_ROWS - 1);
    if (max < 0) max = 0;
    int new_off = active_tty->scroll_offset + 1;
    if (new_off > max) new_off = max;
    active_tty->scroll_offset = new_off;
    tty_flush_vga(active_tty);
}

void console_scroll_by(int lines) {
    if (!active_tty) return;
    int max = active_tty->content_end_y - (TTY_ROWS - 1);
    if (max < 0) max = 0;
    int new_off = active_tty->scroll_offset - lines;
    if (new_off < 0) new_off = 0;
    if (new_off > max) new_off = max;
    active_tty->scroll_offset = new_off;
    tty_flush_vga(active_tty);
}

int console_get_scroll_offset(void) {
    return active_tty ? active_tty->scroll_offset : 0;
}

// ─── State accessors (used by legacy code / gdt etc.) ────────────────────
int console_cursor_x = 0;
int console_cursor_y = 0;
