#include <punix.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void read_line_with_display(char* buffer, int max_len) {
    int i = 0;
    while (i < max_len - 1) {
        char c = getchar();
        if (c == '\n') {
            buffer[i] = '\0';
            putchar('\n');
            break;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else if (c >= ' ' && c <= '~') {
            buffer[i++] = c;
            putchar(c);
        }
    }
    buffer[i] = '\0';
}

void show_prompt() {
    char cwd[256];
    getcwd(cwd, 256);
    char buf[32];
    sys_get_username(buf, 32);
    
    if (sys_getuid() == 0) {
        printf("root@punix:%s# ", cwd);
    } else {
        printf("%s@punix:%s$ ", buf, cwd);
    }
}

int main() {
    printf("PUNIX Modular Shell v0.2\n");
    printf("Commands are executed as separate processes from /bin\n\n");

    while (1) {
        show_prompt();
        char input[128];
        read_line_with_display(input, 128);
        if (strlen(input) == 0) continue;

        // Simple tokenizer
        char* argv[16];
        int argc = 0;
        char* token = input;
        
        while (*token && argc < 15) {
            while (*token == ' ') token++;
            if (*token == '\0') break;
            argv[argc++] = token;
            while (*token && *token != ' ') token++;
            if (*token == ' ') {
                *token = '\0';
                token++;
            }
        }
        argv[argc] = NULL;

        if (argc == 0) continue;

        // Built-ins
        if (strcmp(argv[0], "exit") == 0) {
            break;
        } else if (strcmp(argv[0], "cd") == 0) {
            if (argc > 1) {
                if (chdir(argv[1]) != 0) {
                    printf("cd: %s: No such directory\n", argv[1]);
                }
            } else {
                chdir("/");
            }
            continue;
        } else if (strcmp(argv[0], "help") == 0) {
            printf("Built-ins: cd, exit, help\n");
            printf("External utilities in /bin: ls, cat, mkdir, rmdir, ps, kill, mem, clear, etc.\n");
            continue;
        }

        // External execution
        int pid = fork();
        if (pid == 0) {
            // Child
            char full_path[128];
            if (argv[0][0] == '/' || (argv[0][0] == '.' && argv[0][1] == '/')) {
                strcpy(full_path, argv[0]);
            } else {
                strcpy(full_path, "/bin/");
                strcat(full_path, argv[0]);
            }
            
            exec(full_path, argv);
            printf("%s: command not found\n", argv[0]);
            exit(127);
        } else if (pid > 0) {
            // Parent
            wait(pid, NULL);
        } else {
            printf("Error: fork failed\n");
        }
    }

    return 0;
}
