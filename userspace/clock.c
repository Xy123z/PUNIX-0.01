#include <punix.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main() {
    uint32_t start_ticks = sys_get_ticks();

    while (1) {
        uint32_t current_ticks = sys_get_ticks();
        uint32_t elapsed_seconds = (current_ticks - start_ticks) / 100;

        int hours = (elapsed_seconds / 3600) % 24;
        int minutes = (elapsed_seconds / 60) % 60;
        int seconds = elapsed_seconds % 60;

        char buf[32];
        sprintf(buf, "%02d:%02d:%02d\n", hours, minutes, seconds);

        // Clear screen before printing new time
        sys_clear_screen();

        // Print using system call
        puts(buf);

        // Sleep for a short interval to avoid busy-waiting
        sys_sleep(1000); // 0.01s or adjust to your tick rate
    }

    return 0;
}
