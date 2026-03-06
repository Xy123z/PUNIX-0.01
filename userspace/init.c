#include <punix.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

/**
 * /sbin/init - Standard Unix-like Init (PID 1)
 *
 * Spawns getty on tty0, tty1, tty2, tty3.
 * Reaps orphans and respawns getty if it exits.
 */

typedef struct {
    int  pid;
    char tty[16];
} getty_info_t;

static getty_info_t gettys[8];

static int fork_getty(int index) {
    int pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // Child
        char tty_path[32];
        if (index >= 4) {
            sprintf(tty_path, "/dev/ttyS%d",index - 4);
        } else {
            sprintf(tty_path, "/dev/tty%d", index);
        }
        char* argv[] = {"getty", tty_path, NULL};
        exec("/sbin/getty", argv);
        
        // If exec fails
        exit(1);
    }
    
    // Parent
    return pid;
}

int main() {
    // init should be quiet
    
    // 1. Launch gettys on tty0 to tty3, and ttyS0
    for (int i = 0; i < 8; i++) {
        if (i >= 4) {
            sprintf(gettys[i].tty, "/dev/ttyS%d",i - 4);
        } else {
            sprintf(gettys[i].tty, "/dev/tty%d", i);
        }
        gettys[i].pid = fork_getty(i);
    }

    // 2. Main loop: reap children and respawn gettys
    while (1) {
        int status = 0;
        int reaped_pid = wait(-1, &status);
        
        if (reaped_pid > 0) {
            printf("[init] Reaped process %d (status 0x%x)\n", reaped_pid, status);
            // Check if it was one of our gettys
            int found = 0;
            for (int i = 0; i < 8; i++) {
                if (gettys[i].pid == reaped_pid) {
                    printf("[init] Respawning getty on %s\n", gettys[i].tty);
                    // Respawn
                    sleep(1); // Anti-respawn-loop throttle
                    gettys[i].pid = fork_getty(i);
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                // Was likely an adopted orphan or a sub-process of a shell
                // No action needed for orphans (they are reaped in the kernel too)
            }
        } else {
            // Error or interrupted? Sleep to avoid busy-wait
            sleep(2);
        }
    }

    return 0;
}
