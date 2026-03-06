#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <punix_def.h>

// Direct Syscall Wrappers
extern int sys_raw_getchar();
extern void sys_clear_screen();
extern void sys_draw_char_at(int x, int y, char c, char color);
extern void sys_draw_string_at(int x, int y, const char* str, char color);
extern void sys_update_cursor(int x, int y);

#define ROWS 25
#define COLS 80
#define EDIT_ROWS 23

// Buffer
#define BUFFER_SIZE 40960
char buffer[BUFFER_SIZE];
int buffer_len = 0;
int cursor_idx = 0; // Current insertion point in buffer

// Viewport
int scroll_row = 0; // Which text row is at top of screen

// File
char filename[64] = "";
int file_dirty = 0;

// Colors
#define COL_TEXT     0x07 // Light Grey on Black
#define COL_BAR      0x1F // White on Blue
#define COL_STATUS   0x70 // Black on Light Grey

// Helper: Calculate X, Y on screen from cursor_idx
// Returns virtual row/col (relative to start of text)
// We need to re-scan buffer to find this efficiently or maintain it
// For simplicity, we scan from 0
void get_cursor_pos(int* v_row, int* v_col) {
    int r = 0;
    int c = 0;
    for (int i = 0; i < cursor_idx; i++) {
        if (buffer[i] == '\n') {
            r++;
            c = 0;
        } else {
            c++;
            if (c >= COLS) {
                r++;
                c = 0;
            }
        }
    }
    *v_row = r;
    *v_col = c;
}

// Draw entire UI without flickering
void draw_ui() {
    // 1. Draw Title Bar
    char title[81];
    memset(title, ' ', 80);
    title[80] = '\0';
    
    char name_disp[40];
    if (strlen(filename) > 0) strcpy(name_disp, filename);
    else strcpy(name_disp, "[New File]");
    
    if (file_dirty) strcat(name_disp, " *");
    
    // Center title
    int pad = (COLS - strlen(name_disp)) / 2;
    if (pad < 0) pad = 0;
    memcpy(title + pad, name_disp, strlen(name_disp));
    sys_draw_string_at(0, 0, title, COL_BAR);

    // 2. Draw Text Content
    int cr = 0; // Current Row (buffer logic)
    int cc = 0; // Current Col (buffer logic)
    int screen_y = 1; // Start below title
    
    // Prepare a line buffer for rendering to avoid char-by-char flickering on slow serial/drawing
    char line_buf[COLS + 1];
    int line_idx = 0;
    
    // Logic: Iterate buffer, build lines. If line is in viewport, draw it. 
    // This is inefficient for large files but simple for now. 
    // We only need to optimize the *drawing* part.
    
    for (int i = 0; i < buffer_len; i++) {
        char c = buffer[i];
        
        if (c == '\n') {
            // End of line
            if (cr >= scroll_row && screen_y < ROWS - 2) {
                // Determine y position
                int draw_y = screen_y + (cr - scroll_row);
                
                // Pad rest with spaces
                while (line_idx < COLS) line_buf[line_idx++] = ' ';
                line_buf[COLS] = '\0';
                
                sys_draw_string_at(0, draw_y, line_buf, COL_TEXT);
            }
            cr++;
            cc = 0;
            line_idx = 0;
        } else {
            // Character
            if (cr >= scroll_row && screen_y < ROWS - 2) {
                 if (line_idx < COLS) {
                     line_buf[line_idx++] = c;
                 }
            }
            cc++;
            if (cc >= COLS) {
                // Hard wrap logic for display, but buffer is logical. 
                // Wait, if line wraps visually, does it incr 'cr'?
                // My get_cursor_pos does hard wrap. 
                // So here we should too.
                if (cr >= scroll_row && screen_y < ROWS - 2) {
                    int draw_y = screen_y + (cr - scroll_row);
                    line_buf[COLS] = '\0';
                    sys_draw_string_at(0, draw_y, line_buf, COL_TEXT);
                }
                cr++;
                cc = 0;
                line_idx = 0;
            }
        }
        
        if (cr > scroll_row + EDIT_ROWS) break; 
    }
    
    // Draw the last partial line if valid
    if (cr >= scroll_row && screen_y + (cr - scroll_row) < ROWS - 2) {
         int draw_y = screen_y + (cr - scroll_row);
         while (line_idx < COLS) line_buf[line_idx++] = ' ';
         line_buf[COLS] = '\0';
         sys_draw_string_at(0, draw_y, line_buf, COL_TEXT);
         cr++; // Move logic to next
    }
    
    // Clean up empty lines below text
    while (cr - scroll_row < EDIT_ROWS) {
        int draw_y = screen_y + (cr - scroll_row);
        if (draw_y < ROWS - 2) {
            memset(line_buf, ' ', COLS);
            line_buf[COLS] = '\0';
            sys_draw_string_at(0, draw_y, line_buf, COL_TEXT);
        }
        cr++;
    }

    // 3. Status/Help Bar
    char help1[81]; memset(help1, ' ', 80); help1[80] = 0;
    char help2[81]; memset(help2, ' ', 80); help2[80] = 0;
    
    sprintf(help1, "^O WriteOut  ^X Exit");
    sprintf(help2, "^S Save As");

    sys_draw_string_at(0, ROWS-2, help1, COL_STATUS);
    sys_draw_string_at(0, ROWS-1, help2, COL_STATUS);

    // 4. Update Hardware Cursor
    int vr, vc;
    get_cursor_pos(&vr, &vc);
    
    // Scroll logic
    if (vr < scroll_row) scroll_row = vr;
    if (vr >= scroll_row + (ROWS - 3)) scroll_row = vr - (ROWS - 3) + 1;
    
    int screen_r = 1 + (vr - scroll_row);
    sys_update_cursor(vc, screen_r);
}

// Insert char at cursor
void insert_char(char c) {
    if (buffer_len >= BUFFER_SIZE - 1) return;
    
    // Shift right
    for (int i = buffer_len; i > cursor_idx; i--) {
        buffer[i] = buffer[i-1];
    }
    buffer[cursor_idx] = c;
    buffer_len++;
    cursor_idx++;
    file_dirty = 1;
}

// Delete char before cursor (Backspace)
void backspace() {
    if (cursor_idx <= 0) return;
    
    // Shift left
    for (int i = cursor_idx - 1; i < buffer_len - 1; i++) {
        buffer[i] = buffer[i+1];
    }
    buffer_len--;
    cursor_idx--;
    file_dirty = 1;
}

// Prompt Helper
// Returns 1 if input given, 0 if cancelled
int input_prompt(const char* prompt, char* dest, int max_len) {
    char pbuf[81];
    memset(pbuf, ' ', 80);
    pbuf[80] = 0;
    sprintf(pbuf, "%s: ", prompt);
    
    int p_len = strlen(pbuf);
    int input_idx = 0;
    dest[0] = 0;
    
    while(1) {
        // Draw prompt at bottom
        sys_draw_string_at(0, ROWS-2, pbuf, COL_BAR); // Use Bar Color for prompt
        sys_draw_string_at(0, ROWS-1, "Enter: Confirm  ^C: Cancel                    ", COL_STATUS);
        
        // Draw current input
        sys_draw_string_at(strlen(prompt)+2, ROWS-2, dest, COL_BAR);
        
        // Cursor
        sys_update_cursor(strlen(prompt)+2 + input_idx, ROWS-2);
        
        int c = sys_raw_getchar();
        if (c == '\n') {
            return 1;
        } else if (c == 3) { // Ctrl+C Cancel
            return 0;
        } else if (c == '\b' || c == 127) {
            if (input_idx > 0) {
                input_idx--;
                dest[input_idx] = 0;
                // Clear character visually
                sys_draw_char_at(strlen(prompt)+2 + input_idx, ROWS-2, ' ', COL_BAR);
            }
        } else if (c >= 32 && c <= 126) {
            if (input_idx < max_len - 1) {
                dest[input_idx++] = c;
                dest[input_idx] = 0;
            }
        }
    }
}

void save_file() {
    if (strlen(filename) == 0) {
        if (!input_prompt("Filename to write", filename, 63)) return;
    }
    
    int fd = open(filename, O_WRONLY | O_CREAT);
    if (fd < 0) {
        // Error?
        return;
    }
    write(fd, buffer, buffer_len); // Should truncation be handled? 
    // Usually O_TRUNC needed. Let's add O_TRUNC.
    // However, if we don't have O_TRUNC in punix, we rely on FS overwriting?
    // FS implementation in syscall might overwrite or append...
    // Punix FS usually simple. I'll rely on it or add O_TRUNC logic if I can.
    // Step 84 checks flags: O_TRUNC = 0x08. I should add it.
    // Re-opening with O_TRUNC properly:
    close(fd);
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0) {
        write(fd, buffer, buffer_len);
        close(fd);
        file_dirty = 0;
        
        // Show success
        sys_draw_string_at(0, ROWS-2, "File Saved Successfully!                        ", COL_STATUS);
        int i=0; while(i++ < 10000000) __asm__ volatile("nop"); // Small delay
    } else {
        // Show error
        sys_draw_string_at(0, ROWS-2, "Error: Could not save file! (Permission?)       ", COL_STATUS);
        int i=0; while(i++ < 30000000) __asm__ volatile("nop"); // Longer delay
    }
}

void save_as() {
    char new_name[64];
    if (input_prompt("Save As", new_name, 63)) {
        strcpy(filename, new_name);
        save_file();
    }
}

int main(int argc, char** argv) {
    if (argc > 1) {
        strncpy(filename, argv[1], 63);
        int fd = open(filename, O_RDONLY);
        if (fd >= 0) {
            buffer_len = read(fd, buffer, BUFFER_SIZE);
            if (buffer_len < 0) buffer_len = 0; // Error or empty
            close(fd);
            cursor_idx = buffer_len; // Start at end of file
        }
        // If file doesn't exist, we just start empty with that filename
    }

    sys_clear_screen(); // Clear once at start
    while(1) {
        draw_ui();
        
        int c = sys_raw_getchar(); // Assume blocks
        
        if (c == 17) { // Ctrl+Q or something?
            // Ignore
        } else if (c == 24) { // Ctrl+X Exit
            if (file_dirty) {
                // Prompt "Save modified buffer?"
                // Ideally Y/N prompt.
                // For now, simpler: Input Prompt "Save changes? (y/n)"
                char ans[10];
                if (input_prompt("Save modified buffer? (y/n)", ans, 5)) {
                    if (ans[0] == 'y' || ans[0] == 'Y') {
                        save_file();
                        break;
                    } else if (ans[0] == 'n' || ans[0] == 'N') {
                        break;
                    }
                }
            } else {
                break;
            }
        } else if (c == 15) { // Ctrl+O Save
            save_file();
        } else if (c == 19) { // Ctrl+S Save As
            save_as();
        } else if (c == '\b' || c == 127) {
            backspace();
        } else if (c == '\n') {
            insert_char('\n'); 
        } else if (c >= 32 && c <= 126) {
            insert_char(c);
        }
        // TODO: Arrow keys if driver sends them
    }
    
    sys_clear_screen();
    return 0;
}
