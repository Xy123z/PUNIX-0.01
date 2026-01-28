#include <punix.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_ARGS 32
#define MAX_PATH 256
#define MAX_CMD_LEN 256

const char* search_paths[] = {
    "/bin/",
    "/sbin/",
    "/usr/bin/",
    "/usr/sbin/",
    NULL
};

void show_prompt() {
    char cwd[MAX_PATH];
    char user[32];
    getcwd(cwd, MAX_PATH);
    sys_get_username(user, 32);
    
    uint32_t uid = getuid();
    
    // Prompt polish: replace /home/user with ~
    char prompt_cwd[MAX_PATH];
    char home_prefix[MAX_PATH];
    sprintf(home_prefix, "/home/%s", user);
    
    if (strncmp(cwd, home_prefix, strlen(home_prefix)) == 0) {
        sprintf(prompt_cwd, "~%s", cwd + strlen(home_prefix));
    } else {
        strcpy(prompt_cwd, cwd);
    }
    
    // Cyan user@punix, Green CWD, White prompt char
    // Now that kernel handles ANSI, we can use standard codes
    printf("\033[1;36m%s@punix\033[0m:\033[1;32m%s\033[0m%s ", 
           user, prompt_cwd, (uid == 0) ? "#" : "$");
}

int find_executable(const char* cmd, char* full_path) {
    if (cmd[0] == '/' || (cmd[0] == '.' && cmd[1] == '/')) {
        struct_stat_t st;
        if (sys_stat(cmd, &st) == 0) {
            strcpy(full_path, cmd);
            return 1;
        }
        return 0;
    }

    for (int i = 0; search_paths[i] != NULL; i++) {
        strcpy(full_path, search_paths[i]);
        strcat(full_path, cmd);
        struct_stat_t st;
        if (sys_stat(full_path, &st) == 0) {
            return 1;
        }
    }
    return 0;
}

void execute_pipeline(char* cmd) {
    char* commands[8];
    int num_cmds = 0;
    
    char* token = cmd;
    char* next_cmd = cmd;
    while ((next_cmd = strchr(token, '|')) != NULL) {
        *next_cmd = '\0';
        commands[num_cmds++] = token;
        token = next_cmd + 1;
    }
    commands[num_cmds++] = token;

    int pids[8];
    int prev_pipe_read = -1;

    for (int i = 0; i < num_cmds; i++) {
        int pipefd[2];
        if (i < num_cmds - 1) {
            if (pipe(pipefd) < 0) {
                printf("bash: pipe failed\n");
                return;
            }
        }

        int pid = fork();
        if (pid == 0) {
            if (prev_pipe_read != -1) {
                dup2(prev_pipe_read, 0);
                close(prev_pipe_read);
            }
            if (i < num_cmds - 1) {
                close(pipefd[0]);
                dup2(pipefd[1], 1);
                close(pipefd[1]);
            }

            char* c = commands[i];
            char* out_file = strchr(c, '>');
            char* in_file = strchr(c, '<');
            int append = 0;

            if (out_file) {
                if (*(out_file + 1) == '>') {
                    append = 1;
                    *out_file = '\0';
                    out_file += 2;
                } else {
                    *out_file = '\0';
                    out_file += 1;
                }
                while (*out_file == ' ') out_file++;
                char* end = out_file;
                while (*end && *end != ' ' && *end != '<') end++;
                char saved = *end; *end = '\0';
                
                int fd = open(out_file, append ? O_WRONLY | 0x08 : O_WRONLY | O_CREAT);
                if (fd >= 0) {
                    dup2(fd, 1);
                    close(fd);
                } else {
                    printf("bash: %s: Cannot open for writing\n", out_file);
                    exit(1);
                }
                *end = saved;
            }

            if (in_file) {
                *in_file = '\0';
                in_file += 1;
                while (*in_file == ' ') in_file++;
                char* end = in_file;
                while (*end && *end != ' ' && *end != '>') end++;
                char saved = *end; *end = '\0';

                int fd = open(in_file, O_RDONLY);
                if (fd >= 0) {
                    dup2(fd, 0);
                    close(fd);
                } else {
                    printf("bash: %s: No such file\n", in_file);
                    exit(1);
                }
                *end = saved;
            }

            char* argv[MAX_ARGS];
            int argc = 0;
            char* arg_token = commands[i];
            while (*arg_token) {
                while (*arg_token == ' ') arg_token++;
                if (*arg_token == '\0') break;
                argv[argc++] = arg_token;
                while (*arg_token && *arg_token != ' ') arg_token++;
                if (*arg_token == ' ') {
                    *arg_token = '\0';
                    arg_token++;
                }
            }
            argv[argc] = NULL;

            if (argc > 0) {
                char path[MAX_PATH];
                if (find_executable(argv[0], path)) {
                    exec(path, argv);
                } else {
                    printf("bash: %s: command not found\n", argv[0]);
                }
            }
            exit(0);
        } else {
            pids[i] = pid;
            if (prev_pipe_read != -1) close(prev_pipe_read);
            if (i < num_cmds - 1) {
                close(pipefd[1]);
                prev_pipe_read = pipefd[0];
            }
        }
    }

    // After starting all commands, wait for them
    for (int i = 0; i < num_cmds; i++) {
        wait(pids[i], NULL);
    }
}

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
        // Don't echo password
    }
    buf[i] = '\0';
    printf("\n");
}

int main() {
    char cmd[MAX_CMD_LEN];
    printf("\033[1;32mPUNIX Bash v1.1\033[0m\n");
    printf("Type 'help' for info.\n\n");

    while (1) {
        show_prompt();
        
        int i = 0;
        while (i < MAX_CMD_LEN - 1) {
            char c = getchar();
            if (c == '\n') { putchar('\n'); break; }
            if (c == '\b') {
                if (i > 0) {
                    i--;
                    putchar('\b'); putchar(' '); putchar('\b');
                }
                continue;
            }
            if (c >= ' ' && c <= '~') {
                cmd[i++] = c;
                putchar(c);
            }
        }
        cmd[i] = '\0';

        if (strlen(cmd) == 0) continue;

        char* cptr = cmd;
        while (*cptr == ' ') cptr++;
        if (*cptr == '\0') continue;

        // Built-ins
        if (strncmp(cptr, "exit", 4) == 0 && (cptr[4] == ' ' || cptr[4] == '\0')) {
            break;
        } else if (strncmp(cptr, "cd", 2) == 0 && (cptr[2] == ' ' || cptr[2] == '\0')) {
            char* path = cptr + 2;
            while (*path == ' ') path++;
            if (*path == '\0') path = "/";
            if (chdir(path) != 0) {
                printf("cd: %s: No such directory\n", path);
            }
            continue;
        } else if (strncmp(cptr, "sudo", 4) == 0 && (cptr[4] == ' ' || cptr[4] == '\0')) {
            char* rest = cptr + 4;
            while (*rest == ' ') rest++;
            
            if (*rest == '\0') {
                printf("usage: sudo COMMAND\n");
                continue;
            }

            uint32_t orig_uid = sys_getuid();
            if (orig_uid != 0) {
                char pass[40];
                printf("[sudo] password for root: ");
                read_password(pass, 40);
                if (sys_authenticate(pass) != 0) {
                    printf("sudo: 3 incorrect password attempts\n"); // Unix-y joke
                    continue;
                }
            }
            
            // Execute the rest as root (UID 0 set by sys_authenticate or already root)
            execute_pipeline(rest);
            
            // Drop back to original UID
            if (orig_uid != 0) {
                setuid(orig_uid);
            }
            continue;
        } else if (strcmp(cptr, "help") == 0) {
            printf("PUNIX Bash v1.1\n");
            printf("Built-ins: cd, exit, help, clear, sudo\n");
            printf("Paths: /bin, /sbin, /usr/bin, /usr/sbin\n");
            printf("Features: pipes (|), redirection (>, >>, <)\n");
            continue;
        } else if (strcmp(cptr, "clear") == 0) {
            sys_clear_screen();
            continue;
        }

        execute_pipeline(cptr);
    }

    return 0;
}
