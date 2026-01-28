#include <punix.h>
#include <stdio.h>
#include <unistd.h>

/**
 * /sbin/init - The root of the process tree (PID 1).
 * Responsibilities:
 * 1. Spawn the main shell (/bin/bash).
 * 2. Reap zombies and adopt orphans.
 * 3. Respawn the shell if it exits.
 */
/**
 * @brief Count how many shell processes (or processes about to become shells) are running
 */
/**
 * @brief Count how many shell processes (or processes about to become shells) are running.
 * This includes any process named "bash", "sh", or "pbash", 
 * as well as any child of PID 1 named "init" (a process in fork/exec transition).
 */
static int count_active_shells() {
    proc_info_t procs[64];
    int count = sys_get_procs(procs, 64);
    int shells = 0;
    for (int i = 0; i < count; i++) {
        // Any child of PID 1 is considered a potential shell manager if it's "init"
        // or any process explicitly named as a shell.
        if (strcmp(procs[i].name, "bash") == 0 || 
            strcmp(procs[i].name, "sh") == 0 ||
            strcmp(procs[i].name, "pbash") == 0) {
            shells++;
        }
        else if (procs[i].ppid == 1 && strcmp(procs[i].name, "init") == 0) {
            shells++;
        }
    }
    return shells;
}

int main() {
    // /sbin/init should be almost entirely silent to avoid TTY output clutter
    // and input contention.
    
    int last_spawned_pid = -1;

    while (1) {
        int shells = count_active_shells();
        
        // Only spawn if absolutely no shell activity is detected
        if (shells == 0) {
            int pid = fork();
            
            if (pid < 0) {
                sleep(2); // Fork fail, wait and retry
                continue;
            }
            
            if (pid == 0) {
                // Child: immediately attempt to become a shell
                char* argv[] = {"bash", NULL};
                
                // Try several paths in case of disk layout variations
                exec("/bin/bash", argv);
                exec("/bin/pbash", argv);
                exec("/bin/sh", argv);
                
                // If we reach here, exec failed. Die silently but quickly.
                exit(1);
            }
            
            // Parent: track the child and wait for it
            last_spawned_pid = pid;
            
            // Give the child a moment to change its name via sys_exec
            sleep(1);
        }

        // Wait for ANY child (the current shell or adopted orphans)
        // This is a blocking call.
        int status = 0;
        int reaped_pid = wait(-1, &status);
        
        if (reaped_pid > 0) {
            printf("[ init ] Reaped zombie process %d with status %d\n", reaped_pid, status);
        } else {
            // No children left or interrupted? Sleep to avoid busy-wait.
            sleep(1);
        }
        // When we wake up from a reap, we immediately re-evaluate the shell count.
    }

    return 0;
}
