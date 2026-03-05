#include <punix.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

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

static uint32_t rand_seed = 12345;

uint32_t simple_rand() {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed / 65536) % 32768;
}

// ANSI helpers
void gotoxy(int x, int y) {
    printf("\033[%d;%dH", y + 1, x + 1);
}

void clear_screen() {
    printf("\033[2J");
}

void hide_cursor() {
    // printf("\033[?25l"); // Not supported by kernel TTY yet, move off-screen
    gotoxy(0, 26);
}

void set_color_green() {
    printf("\033[32m");
}

void reset_color() {
    printf("\033[0m");
}

void set_char_at(int x, int y, char c) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        gotoxy(x, y);
        putchar(c);
    }
}

int main() {
    Point snake[SNAKE_LENGTH];
    int direction = DIR_RIGHT;
    
    int start_x = SCREEN_WIDTH / 2;
    int start_y = SCREEN_HEIGHT / 2;
    
    for (int i = 0; i < SNAKE_LENGTH; i++) {
        snake[i].x = start_x - i;
        snake[i].y = start_y;
    }
    
    rand_seed = get_ticks();
    
    clear_screen();
    hide_cursor();
    
    printf("Snake ANSI Demo Started! (Random Move Mode)\n");
    sleep(1);
    
    clear_screen();
    
    while (1) {
        if (simple_rand() % 10 == 0) {
            int new_dir = simple_rand() % 4;
            if ((direction == DIR_UP && new_dir != DIR_DOWN) ||
                (direction == DIR_DOWN && new_dir != DIR_UP) ||
                (direction == DIR_LEFT && new_dir != DIR_RIGHT) ||
                (direction == DIR_RIGHT && new_dir != DIR_LEFT)) {
                direction = new_dir;
            }
        }
        
        Point new_head = snake[0];
        switch (direction) {
            case DIR_UP:    new_head.y--; if (new_head.y < 0) new_head.y = SCREEN_HEIGHT - 1; break;
            case DIR_DOWN:  new_head.y++; if (new_head.y >= SCREEN_HEIGHT) new_head.y = 0; break;
            case DIR_LEFT:  new_head.x--; if (new_head.x < 0) new_head.x = SCREEN_WIDTH - 1; break;
            case DIR_RIGHT: new_head.x++; if (new_head.x >= SCREEN_WIDTH) new_head.x = 0; break;
        }
        
        // Erase tail
        reset_color();
        set_char_at(snake[SNAKE_LENGTH - 1].x, snake[SNAKE_LENGTH - 1].y, ' ');
        
        // Move snake body
        for (int i = SNAKE_LENGTH - 1; i > 0; i--) {
            snake[i] = snake[i - 1];
        }
        snake[0] = new_head;
        
        // Draw head
        set_color_green();
        set_char_at(snake[0].x, snake[0].y, '*');
        
        // Flush and hide cursor
        hide_cursor();
        fflush(stdout);

        // Delay: ~10 frames per second
        sleep(10); 
    }
    
    return 0;
}

