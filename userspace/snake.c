#include <punix.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define SNAKE_LENGTH 20

// Direction constants
#define DIR_UP 0
#define DIR_DOWN 1
#define DIR_LEFT 2
#define DIR_RIGHT 3

typedef struct {
    int x;
    int y;
} Point;

// Simple pseudo-random number generator
static uint32_t rand_seed = 12345;

uint32_t simple_rand() {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed / 65536) % 32768;
}

void set_char_at(int x, int y, char c, uint8_t color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        sys_draw_char_at(x, y, c, color);
    }
}

int main() {
    Point snake[SNAKE_LENGTH];
    int direction = DIR_RIGHT;
    
    // Initialize snake in the middle of the screen
    int start_x = SCREEN_WIDTH / 2;
    int start_y = SCREEN_HEIGHT / 2;
    
    for (int i = 0; i < SNAKE_LENGTH; i++) {
        snake[i].x = start_x - i;
        snake[i].y = start_y;
    }
    
    // Seed random with current ticks
    rand_seed = sys_get_ticks();
    
    // Hide cursor (move off-screen)
    sys_update_cursor(0, 26);
    
    printf("Snake Program Started! Press Ctrl+J to background.\n");
    sys_sleep(200); // Brief pause to show message
    
    sys_clear_screen();
    
    while (1) {
        // Randomly change direction occasionally
        if (simple_rand() % 10 == 0) {
            int new_dir = simple_rand() % 4;
            // Prevent 180-degree turns
            if ((direction == DIR_UP && new_dir != DIR_DOWN) ||
                (direction == DIR_DOWN && new_dir != DIR_UP) ||
                (direction == DIR_LEFT && new_dir != DIR_RIGHT) ||
                (direction == DIR_RIGHT && new_dir != DIR_LEFT)) {
                direction = new_dir;
            }
        }
        
        // Calculate new head position
        Point new_head = snake[0];
        switch (direction) {
            case DIR_UP:
                new_head.y--;
                if (new_head.y < 0) new_head.y = SCREEN_HEIGHT - 1;
                break;
            case DIR_DOWN:
                new_head.y++;
                if (new_head.y >= SCREEN_HEIGHT) new_head.y = 0;
                break;
            case DIR_LEFT:
                new_head.x--;
                if (new_head.x < 0) new_head.x = SCREEN_WIDTH - 1;
                break;
            case DIR_RIGHT:
                new_head.x++;
                if (new_head.x >= SCREEN_WIDTH) new_head.x = 0;
                break;
        }
        
        // Erase tail
        set_char_at(snake[SNAKE_LENGTH - 1].x, snake[SNAKE_LENGTH - 1].y, ' ', 0x07);
        
        // Move snake body
        for (int i = SNAKE_LENGTH - 1; i > 0; i--) {
            snake[i] = snake[i - 1];
        }
        
        // Update head
        snake[0] = new_head;
        
        // Draw new head
        set_char_at(snake[0].x, snake[0].y, '*', 0x0A); // Green on black
        
        // Sleep to control speed
        sys_sleep(50); // 0.5 second delay (100Hz timer)
    }
    
    return 0;
}
