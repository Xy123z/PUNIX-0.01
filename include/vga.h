/* include/vga.h - VGA text mode driver */
#ifndef VGA_H
#define VGA_H

#include "types.h"

#define VGA_MEMORY 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

// Colors
#ifndef COLOR_BLACK
// Colors
#define COLOR_BLACK 0
#define COLOR_BLUE 1
#define COLOR_GREEN 2
#define COLOR_CYAN 3
#define COLOR_RED 4
#define COLOR_MAGENTA 5
#define COLOR_BROWN 6
#define COLOR_LIGHT_GREY 7
#define COLOR_DARK_GREY 8
#define COLOR_LIGHT_BLUE 9
#define COLOR_LIGHT_GREEN 10
#define COLOR_LIGHT_CYAN 11
#define COLOR_LIGHT_RED 12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_YELLOW 14
#define COLOR_WHITE 15

#define COLOR_WHITE_ON_BLACK 0x0F
#define COLOR_GREEN_ON_BLACK 0x0A
#define COLOR_YELLOW_ON_BLACK 0x0E
#define COLOR_RED_ON_BLACK 0x0C
#endif

void vga_init();
void vga_print(const char* str);
void vga_print_colored(const char* str, char color);
void vga_putchar(char c, char color);
void vga_clear_screen();
void vga_update_cursor();

// New Overlay Functions
void vga_draw_char_at(int x, int y, char c, char color);
void vga_draw_string_at(int x, int y, const char* str, char color);

#endif
