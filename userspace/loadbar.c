#include <punix.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define BAR_WIDTH 50
#define SCREEN_WIDTH 80

void set_char_at(int x, int y, char c, uint8_t color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < 25) {
        sys_draw_char_at(x, y, c, color);
    }
}

void draw_loading_bar(int percentage) {
    int bar_y = 12; // Middle of screen
    int start_x = (SCREEN_WIDTH - BAR_WIDTH - 10) / 2; // Center the bar
    
    // Draw the percentage text
    char percent_text[20];
    sprintf(percent_text, "%3d%%", percentage);
    
    // Clear the line first
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        set_char_at(x, bar_y, ' ', 0x07);
    }
    
    // Draw opening bracket
    set_char_at(start_x, bar_y, '[', 0x0F);
    
    // Calculate how many # to draw
    int filled = (percentage * BAR_WIDTH) / 100;
    
    // Draw the bar
    for (int i = 0; i < BAR_WIDTH; i++) {
        char c = (i < filled) ? '#' : ' ';
        uint8_t color = (i < filled) ? 0x0E : 0x08; // Yellow for filled, dark gray for empty
        set_char_at(start_x + 1 + i, bar_y, c, color);
    }
    
    // Draw closing bracket
    set_char_at(start_x + BAR_WIDTH + 1, bar_y, ']', 0x0F);
    
    // Draw percentage
    sys_draw_string_at(start_x + BAR_WIDTH + 3, bar_y, percent_text, 0x0B);
}

void draw_title() {
    const char* title = "LOADING...";
    int title_y = 10;
    int start_x = (SCREEN_WIDTH - strlen(title)) / 2;
    
    sys_draw_string_at(start_x, title_y, title, 0x0F);
}

int main() {
    // Hide cursor
    sys_update_cursor(0, 26);

    printf("Loading Bar Program Started! Press Ctrl+J to background.\n");
    sys_sleep(200); // Brief pause to show message
    
    sys_clear_screen();
    
    draw_title();
    
    while (1) {
        // Loop from 0% to 100%
        for (int progress = 0; progress <= 100; progress++) {
            draw_loading_bar(progress);
            
            // Sleep to make it slow enough to see
            // Adjust this value to control loading speed
            sys_sleep(400); // ~4 seconds per percent at 100Hz
        }
        
        // Brief pause at 100% before resetting
        sys_sleep(200);
        
        // Reset message
        int msg_y = 14;
        const char* reset_msg = "*** COMPLETE! Resetting... ***";
        int start_x = (SCREEN_WIDTH - strlen(reset_msg)) / 2;
        
        for (int i = 0; reset_msg[i] != '\0'; i++) {
            set_char_at(start_x + i, msg_y, reset_msg[i], 0x0A);
        }
        
        sys_sleep(150);
        
        // Clear reset message
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            set_char_at(x, msg_y, ' ', 0x07);
        }
    }
    
    return 0;
}
