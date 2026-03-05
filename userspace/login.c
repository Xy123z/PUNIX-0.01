#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: login <username>\n");
        return 1;
    }

    char* username = argv[1];
    char password[64];

    // Disable echo for password entry
    termios_t t;
    tcgetattr(0, &t);
    uint32_t old_lflag = t.c_lflag;
    t.c_lflag &= ~ECHO;
    tcsetattr(0, TCSANOW, &t);

    printf("Password: ");
    fflush(stdout);

    if (fgets(password, sizeof(password), stdin)) {
        // Strip ALL trailing whitespace (newlines, carriage returns, etc.)
        int len = strlen(password);
        while (len > 0 && (password[len - 1] == '\n' || password[len - 1] == '\r' || password[len - 1] == ' ')) {
            password[--len] = '\0';
        }
        printf("\n");

        // Restore echo
        t.c_lflag = old_lflag;
        tcsetattr(0, TCSANOW, &t);

        if (authenticate(password) == 0) {
            // Success! Set UID and launch shell
            // For now we only have root (0)
            setuid(0);
            setgid(0);
            
            char* shell_argv[] = {"sh", NULL};
            printf("Welcome to PUNIX, %s!\n", username);
            exec("/bin/pbash", shell_argv);
            exec("/bin/sh", shell_argv);
            
            printf("login: exec shell failed\n");
            return 1;
        } else {
            printf("Login incorrect\n");
            return 1;
        }
    }

    // Restore echo if fgets failed
    t.c_lflag = old_lflag;
    tcsetattr(0, TCSANOW, &t);
    return 1;
}
