// src/console.c - Buffered Console Driver with Mouse Scrolling Support and TTY System
#include "../include/console.h"
#include "../include/string.h" // For memset, memcpy, etc.
#include "../include/types.h"
#include "../include/interrupt.h"
#include "../include/task.h"
#include "../include/memory.h"

// Hardware address
#define VGA_MEMORY 0xB8000

// Global state for backward compatibility (used when no task context)
static uint16_t global_console_buffer[CONSOLE_SIZE];
int console_cursor_x = 0;
int console_cursor_y = 0;
static int console_scroll_offset = 0;
static int console_content_end_y = 0;

// NEW: Global pointer to the TTY currently occupying the VGA hardware
void* active_tty_ptr = NULL;

// Get current task's TTY, or return NULL if no task
static tty_t* get_current_tty(void) {
    extern task_t* current_task;
    if (current_task && current_task->tty) {
        return (tty_t*)current_task->tty;
    }
    return NULL;
}

// TTY Management Functions
tty_t* tty_create(void) {
    tty_t* tty = (tty_t*)kmalloc(sizeof(tty_t));
    if (!tty) return NULL;
    tty_init(tty);
    tty->ref_count = 1;
    return tty;
}

void tty_reference(tty_t* tty) {
    if (tty) {
        tty->ref_count++;
    }
}

void tty_destroy(tty_t* tty) {
    if (tty) {
        tty->ref_count--;
        if (tty->ref_count <= 0) {
            if (tty->stdin_buf) pipe_destroy(tty->stdin_buf);
            if (tty->stdout_buf) pipe_destroy(tty->stdout_buf);
            kfree(tty);
        }
    }
}

void tty_init(tty_t* tty) {
    if (!tty) return;
    
    // CRITICAL: Zero the entire structure first to prevent garbage pointers
    // in members like stdin_buf and stdout_buf.
    memset(tty, 0, sizeof(tty_t));
    
    uint16_t blank_char = 0x20 | (COLOR_WHITE_ON_BLACK << 8);
    for (int i = 0; i < CONSOLE_SIZE; i++) {
        tty->buffer[i] = blank_char;
    }
    tty->cursor_x = 0;
    tty->cursor_y = 0;
    tty->scroll_offset = 0;
    tty->content_end_y = 0;
    tty->ansi_state = 0;
    tty->curr_color = COLOR_WHITE_ON_BLACK;

    if (!tty->stdin_buf) tty->stdin_buf = pipe_create();
    if (!tty->stdout_buf) tty->stdout_buf = pipe_create();
}

void tty_clear(tty_t* tty) {
    if (!tty) return;
    tty_init(tty);
}

// Function to update the hardware cursor position on the screen (works with TTY)
static void console_update_hw_cursor_tty(tty_t* tty) {
    if (!tty) return;
    uint16_t position = 0;

    // Only set cursor position if it is currently visible
    if (tty->cursor_y >= tty->scroll_offset &&
        tty->cursor_y < tty->scroll_offset + VGA_HEIGHT) {

        // Calculate relative position within the 25-line visible window
        position = (tty->cursor_y - tty->scroll_offset) * VGA_WIDTH + tty->cursor_x;

        // Update VGA registers
        outb(0x3D4, 0x0F);
        outb(0x3D5, (uint8_t)(position & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
    }
}

// Legacy function for backward compatibility
static void console_update_hw_cursor() {
    tty_t* tty = get_current_tty();
    if (tty) {
        console_update_hw_cursor_tty(tty);
    } else {
        // Use global state
        uint16_t position = 0;
        if (console_cursor_y >= console_scroll_offset &&
            console_cursor_y < console_scroll_offset + VGA_HEIGHT) {
            position = (console_cursor_y - console_scroll_offset) * VGA_WIDTH + console_cursor_x;
            outb(0x3D4, 0x0F);
            outb(0x3D5, (uint8_t)(position & 0xFF));
            outb(0x3D4, 0x0E);
            outb(0x3D5, (uint8_t)((position >> 8) & 0xFF));
        }
    }
}

// Redraws the visible portion of the screen by copying from TTY buffer to VGA memory.
void console_update_vga_tty(tty_t* tty) {
    if (!tty) return;
    uint16_t* vga_buffer = (uint16_t*)VGA_MEMORY;
    int start_index = tty->scroll_offset * VGA_WIDTH;

    // Loop through the visible VGA rows (0 to 24)
    for (int y = 0; y < VGA_HEIGHT; y++) {
        int source_index = start_index + (y * VGA_WIDTH);

        // Check bounds against the internal buffer size
        if (source_index < CONSOLE_SIZE) {
            // Copy one full line from RAM buffer to VGA memory
            for (int x = 0; x < VGA_WIDTH; x++) {
                vga_buffer[y * VGA_WIDTH + x] = tty->buffer[source_index + x];
            }
        } else {
            // If we scrolled past the end of the conceptual buffer, fill the rest with black space
            uint16_t blank_char = 0x20 | (COLOR_WHITE_ON_BLACK << 8);
            for (int x = 0; x < VGA_WIDTH; x++) {
                vga_buffer[y * VGA_WIDTH + x] = blank_char; // ' '
            }
        }
    }

    console_update_hw_cursor_tty(tty);
}

// Legacy function for backward compatibility
static void console_update_vga() {
    tty_t* tty = get_current_tty();
    if (tty) {
        console_update_vga_tty(tty);
    } else {
        // Use global state
        uint16_t* vga_buffer = (uint16_t*)VGA_MEMORY;
        int start_index = console_scroll_offset * VGA_WIDTH;
        for (int y = 0; y < VGA_HEIGHT; y++) {
            int source_index = start_index + (y * VGA_WIDTH);
            if (source_index < CONSOLE_SIZE) {
                for (int x = 0; x < VGA_WIDTH; x++) {
                    vga_buffer[y * VGA_WIDTH + x] = global_console_buffer[source_index + x];
                }
            } else {
                uint16_t blank_char = 0x20 | (COLOR_WHITE_ON_BLACK << 8);
                for (int x = 0; x < VGA_WIDTH; x++) {
                    vga_buffer[y * VGA_WIDTH + x] = blank_char;
                }
            }
        }
        console_update_hw_cursor();
    }
}

// Handles content scrolling when the TTY buffer fills up
static void console_content_scroll_tty(tty_t* tty) {
    if (!tty) return;
    // If the cursor is at or past the maximum line index (200), shift all content up by one line.
    if (tty->cursor_y >= CONSOLE_LINES) {
        // Move CONSOLE_LINES - 1 lines up
        memcpy(tty->buffer, tty->buffer + VGA_WIDTH, (CONSOLE_LINES - 1) * VGA_WIDTH * sizeof(uint16_t));

        // Zero out the last line (the new blank line at the bottom)
        uint16_t blank_char = 0x20 | (COLOR_WHITE_ON_BLACK << 8);
        for (int i = 0; i < VGA_WIDTH; i++) {
            tty->buffer[(CONSOLE_LINES - 1) * VGA_WIDTH + i] = blank_char;
        }

        tty->cursor_y = CONSOLE_LINES - 1; // Cursor stays at the last line
        tty->content_end_y = CONSOLE_LINES - 1; // Content end stays at the last line
    }
}

// Legacy function for backward compatibility
static void console_content_scroll() {
    tty_t* tty = get_current_tty();
    if (tty) {
        console_content_scroll_tty(tty);
    } else {
        // Use global state
        if (console_cursor_y >= CONSOLE_LINES) {
            memcpy(global_console_buffer, global_console_buffer + VGA_WIDTH, (CONSOLE_LINES - 1) * VGA_WIDTH * sizeof(uint16_t));
            uint16_t blank_char = 0x20 | (COLOR_WHITE_ON_BLACK << 8);
            for (int i = 0; i < VGA_WIDTH; i++) {
                global_console_buffer[(CONSOLE_LINES - 1) * VGA_WIDTH + i] = blank_char;
            }
            console_cursor_y = CONSOLE_LINES - 1;
            console_content_end_y = CONSOLE_LINES - 1;
        }
    }
}


// PUBLIC API: Initializes the console system
void console_init() {
    tty_t* tty = get_current_tty();
    if (tty) {
        tty_init(tty);
        console_update_vga_tty(tty);
    } else {
        // Use global state
        uint16_t blank_char = 0x20 | (COLOR_WHITE_ON_BLACK << 8);
        for (int i = 0; i < CONSOLE_SIZE; i++) {
            global_console_buffer[i] = blank_char;
        }
        console_cursor_x = 0;
        console_cursor_y = 0;
        console_scroll_offset = 0;
        console_content_end_y = 0;
        console_update_vga();
    }
}

// Helper to map ANSI color to VGA color
static uint8_t ansi_to_vga[] = { 0, 4, 2, 6, 1, 5, 3, 7 }; // 30-37
static uint8_t ansi_to_vga_bright[] = { 8, 12, 10, 14, 9, 13, 11, 15 }; // 90-97

static void handle_ansi_sgr(tty_t* tty) {
    for (int i = 0; i < tty->ansi_num_params; i++) {
        int param = tty->ansi_params[i];
        if (param == 0) {
            tty->curr_color = COLOR_WHITE_ON_BLACK;
        } else if (param == 1) {
            // Bold - handled by using bright colors in mapping
        } else if (param >= 30 && param <= 37) {
            tty->curr_color = (tty->curr_color & 0xF0) | ansi_to_vga[param - 30];
        } else if (param >= 40 && param <= 47) {
            tty->curr_color = (tty->curr_color & 0x0F) | (ansi_to_vga[param - 40] << 4);
        } else if (param >= 90 && param <= 97) {
            tty->curr_color = (tty->curr_color & 0xF0) | ansi_to_vga_bright[param - 90];
        } else if (param >= 100 && param <= 107) {
            tty->curr_color = (tty->curr_color & 0x0F) | (ansi_to_vga_bright[param - 100] << 4);
        }
    }
}

// TTY-specific putchar function
void console_putchar_tty(tty_t* tty, char c, char color) {
    if (!tty) return;

    // Use current active color if standard color is passed
    if (color == COLOR_WHITE_ON_BLACK) color = tty->curr_color;

    // ANSI state machine
    if (tty->ansi_state == 0) {
        if (c == 27) { // ESC
            tty->ansi_state = 1;
            return;
        }
    } else if (tty->ansi_state == 1) {
        if (c == '[') {
            tty->ansi_state = 2;
            tty->ansi_num_params = 0;
            tty->ansi_params[0] = 0;
            return;
        } else {
            tty->ansi_state = 0;
        }
    } else if (tty->ansi_state == 2) {
        if (c >= '0' && c <= '9') {
            tty->ansi_params[tty->ansi_num_params] = tty->ansi_params[tty->ansi_num_params] * 10 + (c - '0');
            return;
        } else if (c == ';') {
            if (tty->ansi_num_params < 7) {
                tty->ansi_num_params++;
                tty->ansi_params[tty->ansi_num_params] = 0;
            }
            return;
        } else if (c == 'm') {
            tty->ansi_num_params++;
            handle_ansi_sgr(tty);
            tty->ansi_state = 0;
            return;
        } else {
            // Unhandled ANSI command
            tty->ansi_state = 0;
            return;
        }
    }
    
    if (c == '\n') {
        tty->cursor_x = 0;
        tty->cursor_y++;
    } else if (c == '\r') {
        tty->cursor_x = 0;
    } else if (c == '\b') {
        if (tty->cursor_x > 0) {
            tty->cursor_x--;
        } else if (tty->cursor_y > 0) {
            tty->cursor_y--;
            tty->cursor_x = VGA_WIDTH - 1;
        }
        int index = tty->cursor_y * VGA_WIDTH + tty->cursor_x;
        // Place a blank character at the new cursor position
        tty->buffer[index] = 0x20 | (color << 8);
    } else {
        // Place character in the buffer
        int index = tty->cursor_y * VGA_WIDTH + tty->cursor_x;
        if (index < CONSOLE_SIZE) {
            tty->buffer[index] = c | (color << 8);
        }
        tty->cursor_x++;
    }

    // Handle line wrap
    if (tty->cursor_x >= VGA_WIDTH) {
        tty->cursor_x = 0;
        tty->cursor_y++;
    }

    // Handle full buffer
    if (tty->cursor_y >= CONSOLE_LINES) {
        console_content_scroll_tty(tty); // Shift content up
    }

    // Update content end marker
    if (tty->cursor_y > tty->content_end_y) {
        tty->content_end_y = tty->cursor_y;
    }

    // When new content is written, automatically scroll to the bottom of the content
    int required_offset = tty->cursor_y - (VGA_HEIGHT - 1);
    if (required_offset < 0) required_offset = 0;
    tty->scroll_offset = required_offset;

    console_update_vga_tty(tty); // Redraw the visible screen
}

// PUBLIC API: Prints a single character (uses current task's TTY)
void console_putchar(char c, char color) {
    tty_t* tty = get_current_tty();
    if (tty) {
        console_putchar_tty(tty, c, color);
    } else {
        // Use global state for backward compatibility
        if (c == '\n') {
            console_cursor_x = 0;
            console_cursor_y++;
        } else if (c == '\r') {
            console_cursor_x = 0;
        } else if (c == '\b') {
            if (console_cursor_x > 0) {
                console_cursor_x--;
            } else if (console_cursor_y > 0) {
                console_cursor_y--;
                console_cursor_x = VGA_WIDTH - 1;
            }
            int index = console_cursor_y * VGA_WIDTH + console_cursor_x;
            global_console_buffer[index] = 0x20 | (color << 8);
        } else {
            int index = console_cursor_y * VGA_WIDTH + console_cursor_x;
            if (index < CONSOLE_SIZE) {
                global_console_buffer[index] = c | (color << 8);
            }
            console_cursor_x++;
        }

        if (console_cursor_x >= VGA_WIDTH) {
            console_cursor_x = 0;
            console_cursor_y++;
        }

        if (console_cursor_y >= CONSOLE_LINES) {
            console_content_scroll();
        }

        if (console_cursor_y > console_content_end_y) {
            console_content_end_y = console_cursor_y;
        }

        int required_offset = console_cursor_y - (VGA_HEIGHT - 1);
        if (required_offset < 0) required_offset = 0;
        console_scroll_offset = required_offset;

        console_update_vga();
    }
}

// TTY-specific print functions
void console_print_tty(tty_t* tty, const char* str) {
    if (!tty) return;
    for (int i = 0; str[i] != '\0'; i++) {
        console_putchar_tty(tty, str[i], COLOR_WHITE_ON_BLACK);
    }
}

void console_print_colored_tty(tty_t* tty, const char* str, char color) {
    if (!tty) return;
    for (int i = 0; str[i] != '\0'; i++) {
        console_putchar_tty(tty, str[i], color);
    }
}

// PUBLIC API: Prints a string (uses current task's TTY)
void console_print(const char* str) {
    tty_t* tty = get_current_tty();
    if (tty) {
        console_print_tty(tty, str);
    } else {
        for (int i = 0; str[i] != '\0'; i++) {
            console_putchar(str[i], COLOR_WHITE_ON_BLACK);
        }
    }
}

// PUBLIC API: Prints a colored string (uses current task's TTY)
void console_print_colored(const char* str, char color) {
    tty_t* tty = get_current_tty();
    if (tty) {
        console_print_colored_tty(tty, str, color);
    } else {
        for (int i = 0; str[i] != '\0'; i++) {
            console_putchar(str[i], color);
        }
    }
}

// PUBLIC API: Handles scrolling the view up or down (called by mouse driver)
void console_scroll_by(int lines) {
    tty_t* tty = get_current_tty();
    if (tty) {
        int new_offset = tty->scroll_offset - lines; // Negative lines scrolls down
        int max_scroll = tty->content_end_y - (VGA_HEIGHT - 1);
        if (max_scroll < 0) max_scroll = 0;

        if (new_offset < 0) new_offset = 0;
        if (new_offset > max_scroll) new_offset = max_scroll;

        if (new_offset < tty->scroll_offset) {
            console_update_hw_cursor_tty(tty);
        }

        if (new_offset != tty->scroll_offset) {
            tty->scroll_offset = new_offset;
            console_update_vga_tty(tty);
        }
    } else {
        // Use global state
        int new_offset = console_scroll_offset - lines;
        int max_scroll = console_content_end_y - (VGA_HEIGHT - 1);
        if (max_scroll < 0) max_scroll = 0;

        if (new_offset < 0) new_offset = 0;
        if (new_offset > max_scroll) new_offset = max_scroll;

        if (new_offset < console_scroll_offset) {
            console_update_hw_cursor();
        }

        if (new_offset != console_scroll_offset) {
            console_scroll_offset = new_offset;
            console_update_vga();
        }
    }
}

// PUBLIC API: Get the current line offset
int console_get_scroll_offset() {
    return console_scroll_offset;
}

void read_line_with_display(char* buffer, int max_len) {
    int i = 0;
    while (i < max_len - 1) {
        char c = keyboard_read();
        if (c == '\n') {
            buffer[i] = '\0';
            console_putchar('\n', COLOR_WHITE_ON_BLACK);
            break;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                console_putchar('\b', COLOR_WHITE_ON_BLACK);
                console_putchar(' ', COLOR_WHITE_ON_BLACK);
                console_putchar('\b', COLOR_WHITE_ON_BLACK);
            }
        } else if ((c >= ' ' && c <= '~')) {
            buffer[i++] = c;
            console_putchar(c, COLOR_WHITE_ON_BLACK);
        }
    }
    buffer[i] = '\0';
}

// TTY-specific clear function
void console_clear_screen_tty(tty_t* tty) {
    if (!tty) return;
    tty_clear(tty);
    console_update_vga_tty(tty);
}

// PUBLIC API: Clears the entire buffer (uses current task's TTY)
void console_clear_screen() {
    tty_t* tty = get_current_tty();
    if (tty) {
        console_clear_screen_tty(tty);
    } else {
        console_init(); // Reuse initialization to clear the buffer and reset state
    }
}

// --------------------------------------------------------------------------
// PUBLIC SCROLL WRAPPERS FOR KEYBOARD/MOUSE HANDLERS (Missing functions)
// --------------------------------------------------------------------------

/**
 * @brief Scrolls the view up (toward older content/history) by 3 lines.
 * This increases the scroll offset toward the maximum content end.
 */
void console_scroll_up() {
    // Scroll up 3 lines (view moves up, offset increases).
    // The implementation of console_scroll_by uses subtraction: new_offset = current_offset - lines.
    // To increase offset (scroll up), 'lines' must be negative.
    console_scroll_by(1);
}

/**
 * @brief Scrolls the view down (toward newer content/current prompt) by 3 lines.
 * This decreases the scroll offset toward 0.
 */
void console_scroll_down() {
    // Scroll down 3 lines (view moves down, offset decreases).
    // To decrease offset (scroll down), 'lines' must be positive.
    console_scroll_by(-1);
}

// TTY-specific state management
void console_get_state_tty(tty_t* tty, uint16_t* row, uint16_t* col) {
    if (!tty) return;
    *row = tty->cursor_y;
    *col = tty->cursor_x;
}

void console_set_state_tty(tty_t* tty, uint16_t row, uint16_t col) {
    if (!tty) return;
    tty->cursor_y = row;
    tty->cursor_x = col;
    console_update_hw_cursor_tty(tty);
}

// State Management for Context Switching
void console_get_state(uint16_t* row, uint16_t* col) {
    tty_t* tty = get_current_tty();
    if (tty) {
        console_get_state_tty(tty, row, col);
    } else {
        *row = console_cursor_y;
        *col = console_cursor_x;
    }
}

void console_set_state(uint16_t row, uint16_t col) {
    tty_t* tty = get_current_tty();
    if (tty) {
        console_set_state_tty(tty, row, col);
    } else {
        console_cursor_y = row;
        console_cursor_x = col;
        console_update_hw_cursor();
    }
}

int tty_read(tty_t* tty, char* buf, int count) {
    if (!tty || !tty->stdin_buf) return -1;
    return pipe_read(tty->stdin_buf, (uint8_t*)buf, (uint32_t)count);
}

int tty_write(tty_t* tty, const char* buf, int count) {
    if (!tty) return -1;
    // For now, we immediately render to screen when writing to TTY stdout
    // This maintains responsiveness for the shell/programs
    for (int i = 0; i < count; i++) {
        console_putchar_tty(tty, buf[i], COLOR_WHITE_ON_BLACK);
    }
    // We could also push to stdout_buf if we wanted processes to be able to read back
    // but in Unix TTY stdout usually goes to display.
    return count;
}

// --- Virtual TTY Aware Drawing Functions ---
extern int task_is_active_terminal(uint32_t pid);
extern int task_is_switcher_active();
extern void vga_draw_char_at(int x, int y, char c, char color);

void console_draw_char_at(int x, int y, char c, char color) {
    tty_t* tty = get_current_tty();
    
    // 1. Update Virtual TTY Buffer
    if (tty) {
        if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < CONSOLE_LINES) {
            // If y is beyond buffer, technically we should scroll or ignore. 
            // For draw_at, we assume absolute coordinates within buffer logic?
            // Usually draw_at implies screen coords (0-24). 
            // TTY buffer accounts for scroll_offset? 
            // Let's assume (x,y) are SCREEN coordinates relative to TTY's current view logic?
            // OR absolute TTY coords?
            // syscall says "at x, y". Usually implies screen relative (0-24).
            // So we map to tty->buffer[tty->scroll_offset + y]...
            // But if we scroll, the text moves.
            // Snake/Loadbar use absolute 0-24. 
            
            // Map screen Y to buffer Y
            int buffer_y = tty->scroll_offset + y;
            if (buffer_y < CONSOLE_LINES) {
                int index = buffer_y * VGA_WIDTH + x;
                tty->buffer[index] = c | (color << 8);
            }
            
            // User requested: Update HW cursor after new character
            tty->cursor_x = x + 1;
            tty->cursor_y = buffer_y;
            if (tty->cursor_x >= VGA_WIDTH) {
                tty->cursor_x = 0;
                tty->cursor_y++;
            }
        }
    }
    
    // 2. Update Real VGA ONLY if this specific PID is focal and switcher is closed
    // This fixed "clobbering": background processes only update TTY memory.
    extern uint32_t active_terminal_pid;
    extern task_t* current_task;
    if (current_task && current_task->id == active_terminal_pid && !task_is_switcher_active()) {
        vga_draw_char_at(x, y, c, color);
        
        // Use the centralized cursor update logic
        // We only move the hardware cursor if the TTY logic moved the virtual cursor
        if (tty) {
             uint16_t pos = tty->cursor_y * VGA_WIDTH + tty->cursor_x;
             outb(0x3D4, 0x0F);
             outb(0x3D5, (uint8_t)(pos & 0xFF));
             outb(0x3D4, 0x0E);
             outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
        }
    }
}

void console_update_cursor(int x, int y) {
    tty_t* tty = get_current_tty();
    if (tty) {
        tty->cursor_x = x;
        tty->cursor_y = y;
    }
    
    extern uint32_t active_terminal_pid;
    extern task_t* current_task;
    if (current_task && current_task->id == active_terminal_pid && !task_is_switcher_active()) {
        uint16_t pos = y * VGA_WIDTH + x;
        outb(0x3D4, 0x0F);
        outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    }
}

void console_draw_string_at(int x, int y, const char* str, char color) {
    int cur_x = x;
    int cur_y = y;
    for (int i = 0; str[i] != '\0'; i++) {
        console_draw_char_at(cur_x, cur_y, str[i], color);
        cur_x++;
        if (cur_x >= VGA_WIDTH) {
            cur_x = 0;
            cur_y++;
        }
    }
}
