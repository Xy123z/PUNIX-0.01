// src/vga.c - VGA text mode driver
#include "../include/vga.h"

static char* vga_buffer = (char*)VGA_MEMORY;
int vga_cursor_x = 0;
int vga_cursor_y = 0;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

void vga_init() {
    vga_cursor_x = 0;
    vga_cursor_y = 0;
}

void vga_update_cursor() {
    uint16_t position = vga_cursor_y * VGA_WIDTH + vga_cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(position & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
}

static void vga_scroll() {
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH * 2; i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH * 2];
    }

    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH * 2; i < VGA_HEIGHT * VGA_WIDTH * 2; i += 2) {
        vga_buffer[i] = ' ';
        vga_buffer[i + 1] = COLOR_WHITE_ON_BLACK;
    }

    vga_cursor_y = VGA_HEIGHT - 1;
    vga_cursor_x = 0;
}

void vga_putchar(char c, char color) {
    if (c == '\n') {
        vga_cursor_x = 0;
        vga_cursor_y++;
    } else if (c == '\b') {
        if (vga_cursor_x > 0) {
            vga_cursor_x--;
            int offset = 2 * (vga_cursor_y * VGA_WIDTH + vga_cursor_x);
            vga_buffer[offset] = ' ';
            vga_buffer[offset + 1] = color;
        }
    } else {
        int offset = 2 * (vga_cursor_y * VGA_WIDTH + vga_cursor_x);
        vga_buffer[offset] = c;
        vga_buffer[offset + 1] = color;
        vga_cursor_x++;

        if (vga_cursor_x >= VGA_WIDTH) {
            vga_cursor_x = 0;
            vga_cursor_y++;
        }
    }

    if (vga_cursor_y >= VGA_HEIGHT) {
        vga_scroll();
    }

    vga_update_cursor();
}

void vga_print(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        vga_putchar(str[i], COLOR_WHITE_ON_BLACK);
    }
}

void vga_print_colored(const char* str, char color) {
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            vga_cursor_x = 0;
            vga_cursor_y++;
            if (vga_cursor_y >= VGA_HEIGHT) vga_scroll();
            vga_update_cursor();
        } else {
            int offset = 2 * (vga_cursor_y * VGA_WIDTH + vga_cursor_x);
            vga_buffer[offset] = str[i];
            vga_buffer[offset + 1] = color;
            vga_cursor_x++;
            if (vga_cursor_x >= VGA_WIDTH) {
                vga_cursor_x = 0;
                vga_cursor_y++;
                if (vga_cursor_y >= VGA_HEIGHT) vga_scroll();
            }
            vga_update_cursor();
        }
    }
}

void vga_clear_screen() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT * 2; i += 2) {
        vga_buffer[i] = ' ';
        vga_buffer[i + 1] = COLOR_WHITE_ON_BLACK;
    }
    vga_cursor_x = 0;
    vga_cursor_y = 0;
}

// --- Overlay Drawing Functions ---
void vga_draw_char_at(int x, int y, char c, char color) {
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT) return;
    int offset = 2 * (y * VGA_WIDTH + x);
    vga_buffer[offset] = c;
    vga_buffer[offset+1] = color;
}

void vga_draw_string_at(int x, int y, const char* str, char color) {
    for (int i = 0; str[i] != '\0'; i++) {
        vga_draw_char_at(x + i, y, str[i], color);
    }
}

