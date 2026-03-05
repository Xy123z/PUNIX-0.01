// include/shell.h - Shell interface

#ifndef SHELL_H
#define SHELL_H

#define MAX_PASSWORD_LEN 40
#define MAX_USERNAME_LEN 40
#define MAX_RESULT_LEN 128

// Global variables
extern int ROOT_ACCESS_GRANTED;
extern char ROOT_PASSWORD[MAX_PASSWORD_LEN];
extern char USERNAME[MAX_USERNAME_LEN];

// Shell functions
void shell_init();
void shell_run();

// Helper function for input
void read_line_with_display(char* buffer, int max_len);

// Command functions
void cmd_ls();
void cmd_pwd();
void cmd_cd(char* path);
void cmd_mkdir(char* path);
void cmd_rmdir(char* path);
void cmd_add(char* args);
void cmd_su();
void cmd_mem();
void cmd_help();
void cmd_clear();
void cmd_exit();
void cmd_sudo(char* args);
void cmd_shutdown();
void cmd_chuser();
void cmd_chpasswd();

#endif // SHELL_H
