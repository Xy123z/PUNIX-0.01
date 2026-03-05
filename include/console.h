// include/console.h - Kernel console API (thin wrapper over TTY device layer)

#ifndef CONSOLE_H
#define CONSOLE_H

#include "types.h"
#include "tty.h"   // All TTY types are now in tty.h

// ─── Kernel console API ───────────────────────────────────────────────────
void console_init(void);
void console_clear_screen(void);
void console_putchar(char c, char color);
void console_print(const char* str);
void console_print_colored(const char* str, char color);
void read_line_with_display(char* buffer, int max_len);

// Scroll helpers (called by keyboard and mouse handlers)
void console_scroll_up(void);
void console_scroll_down(void);
void console_scroll_by(int lines);
int  console_get_scroll_offset(void);

// Legacy cursor position (used by kernel shell / boot messages)
extern int console_cursor_x;
extern int console_cursor_y;

#endif // CONSOLE_H
