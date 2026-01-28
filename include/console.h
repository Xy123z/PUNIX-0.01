#ifndef CONSOLE_H
#define CONSOLE_H

#include "types.h"

// VGA constants (kept for reference, but now applied to the internal buffer)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

#include "vga.h"
#include "pipe.h"

// Console functions (new buffered implementation)
void console_init();
void console_clear_screen();
void console_putchar(char c, char color);
void console_print(const char* str);
void console_print_colored(const char* str, char color);
void read_line_with_display(char* buffer, int max_len);

// Mouse Scroll Interface
void console_scroll_by(int lines);
int console_get_scroll_offset();

// Internal cursor position (now internal to the console state)
extern int console_cursor_x;
extern int console_cursor_y;

// TTY (Teletype) structure for per-task console isolation
#define CONSOLE_LINES 200
#define CONSOLE_SIZE (VGA_WIDTH * CONSOLE_LINES)

typedef struct tty {
    uint16_t buffer[CONSOLE_SIZE];  // Console buffer (200 lines * 80 chars)
    int cursor_x;                    // Cursor X position
    int cursor_y;                    // Cursor Y position
    int scroll_offset;                // Scroll offset for view window
    int content_end_y;               // Maximum line index with content
    
    // ANSI parser state
    int ansi_state;
    int ansi_params[8];
    int ansi_num_params;
    uint8_t curr_color;

    // Streaming buffers for Unix-like I/O
    pipe_t* stdin_buf;
    pipe_t* stdout_buf;
    int ref_count;                   // Reference count for automatic cleanup
} tty_t;

// TTY management functions
tty_t* tty_create(void);
void tty_reference(tty_t* tty);
void tty_destroy(tty_t* tty);
void tty_init(tty_t* tty);
void tty_clear(tty_t* tty);

// Console functions that work on a specific TTY
void console_putchar_tty(tty_t* tty, char c, char color);
void console_print_tty(tty_t* tty, const char* str);
void console_print_colored_tty(tty_t* tty, const char* str, char color);
void console_clear_screen_tty(tty_t* tty);
void console_update_vga_tty(tty_t* tty);
void console_get_state_tty(tty_t* tty, uint16_t* row, uint16_t* col);
void console_set_state_tty(tty_t* tty, uint16_t row, uint16_t col);

#endif // CONSOLE_H
