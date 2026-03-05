#include <punix.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define BAR_WIDTH 50
#define SCREEN_WIDTH 80

// ANSI helpers
void gotoxy(int x, int y) {
    printf("\033[%d;%dH", y + 1, x + 1);
}

void clear_screen() {
    printf("\033[2J");
}

void hide_cursor() {
    gotoxy(0, 26);
}

void set_color_yellow() {
    printf("\033[33m");
}

void set_color_cyan() {
    printf("\033[36m");
}

void set_color_green() {
    printf("\033[32m");
}

void reset_color() {
    printf("\033[0m");
}

void draw_loading_bar(int percentage) {
    int bar_y = 12; // Middle of screen
    int start_x = (SCREEN_WIDTH - BAR_WIDTH - 10) / 2; // Center the bar
    
    // Position cursor for the bar
    gotoxy(start_x, bar_y);
    reset_color();
    printf("[");
    
    int filled = (percentage * BAR_WIDTH) / 100;
    set_color_yellow();
    for (int i = 0; i < filled; i++) putchar('#');
    
    reset_color(); // Light grey for empty part
    for (int i = filled; i < BAR_WIDTH; i++) putchar(' ');
    
    printf("] ");
    
    set_color_cyan();
    printf("%3d%%", percentage);
    reset_color();
    fflush(stdout);
}

void draw_title() {
    const char* title = "LOADING...";
    int title_y = 10;
    int start_x = (SCREEN_WIDTH - strlen(title)) / 2;
    
    gotoxy(start_x, title_y);
    reset_color();
    printf("%s", title);
    fflush(stdout);
}

int main() {
    printf("Loading Bar ANSI Demo Started!\n");
    sleep(1);
    
    clear_screen();
    hide_cursor();
    draw_title();
    
    while (1) {
        for (int progress = 0; progress <= 100; progress++) {
            draw_loading_bar(progress);
            // Delay
            for (volatile int i = 0; i < 500000; i++);
        }
        
        sleep(1);
        
        int msg_y = 14;
        const char* reset_msg = "*** COMPLETE! Resetting... ***";
        int start_x = (SCREEN_WIDTH - strlen(reset_msg)) / 2;
        
        gotoxy(start_x, msg_y);
        set_color_green();
        printf("%s", reset_msg);
        reset_color();
        fflush(stdout);
        
        sleep(2);
        
        // Clear message
        gotoxy(start_x, msg_y);
        for (int i = 0; i < strlen(reset_msg); i++) putchar(' ');
        fflush(stdout);
    }
    
    return 0;
}

