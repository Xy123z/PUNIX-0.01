#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>

int main(int argc, char* argv[]) {
    char* tty_path = "/dev/tty0";
    if (argc > 1) {
        tty_path = argv[1];
    }

    while (1) {
        int fd = open(tty_path, O_RDWR);
        if (fd < 0) {
            // Cannot open TTY, wait and retry
            sleep(2);
            continue;
        }

        // Set up standard FDs
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2) close(fd);

        // Create new session and set controlling TTY
        setsid();
        ioctl(0, TIOCSCTTY, 0);

        // Reset termios to defaults
        termios_t t;
        tcgetattr(0, &t);
        t.c_iflag = ICRNL;
        t.c_oflag = ONLCR;
        t.c_lflag = ICANON | ECHO | ECHOE | ISIG;
        tcsetattr(0, TCSANOW, &t);

        printf("\n\nPUNIX (v0.01) %s\n\n", tty_path);
        printf("%s login: ", "punix");
        
        char username[64];
        if (fgets(username, sizeof(username), stdin)) {
            // Remove newline
            char* nl = strchr(username, '\n');
            if (nl) *nl = '\0';

            if (strlen(username) > 0) {
                char* login_argv[] = {"login", username, NULL};
                exec("/sbin/login", login_argv);
                // If exec fails
                printf("getty: exec /sbin/login failed\n");
            }
        }
        
        // If we reach here, restart the loop (either fgets failed or exec failed)
        sleep(1);
    }

    return 0;
}
