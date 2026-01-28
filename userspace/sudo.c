#include <punix.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void read_password(char* buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c = getchar();
        if (c == '\n') break;
        if (c == '\b') {
            if (i > 0) i--;
            continue;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    printf("\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: sudo COMMAND [ARGS...]\n");
        return 1;
    }

    uint32_t orig_uid = getuid();
    uint32_t orig_gid = getgid();

    if (orig_uid != 0) {
        char pass[40];
        printf("[sudo] password for root: ");
        read_password(pass, 40);
        if (sys_authenticate(pass) != 0) {
            printf("sudo: incorrect password\n");
            return 1;
        }
    }

    // After successful authentication, uid is 0 in kernel for this task
    // We should also set gid to 0 for full root privileges
    setgid(0);

    // Execute the command
    int pid = fork();
    if (pid == 0) {
        // Child
        if (exec(argv[1], &argv[1]) < 0) {
            printf("sudo: %s: command not found\n", argv[1]);
            exit(1);
        }
    } else if (pid > 0) {
        // Parent
        wait(pid, NULL);
    } else {
        printf("sudo: fork failed\n");
    }

    // Drop privileges back (though the process will likely exit)
    if (orig_uid != 0) {
        setuid(orig_uid);
        setgid(orig_gid);
    }

    return 0;
}
