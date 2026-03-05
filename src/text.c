#include <punix.h>
#include <string.h>
#include <stdio.h>

#define CTRL_S 0x13
#define CTRL_X 0x18

// Max safe size for the editor buffer
#define MAX_EDITOR_SIZE 511

// --- Helper Functions ---

static void read_line(char* buffer, int max_len) {
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
        } else if ((c >= ' ' && c <= '~')) {
            buffer[i++] = c;
            putchar(c);
        }
    }
    buffer[i] = '\0';
}

// --- Main Editor Function ---

void text_editor(const char* edit_filename) {
    char editor_buffer[512];
    size_t current_len = 0;
    int fd = -1;
    char initial_filename[FS_MAX_NAME] = {0};

    // 0. Initial Loading Logic
    if (edit_filename && strlen(edit_filename) > 0) {
        strncpy(initial_filename, edit_filename, FS_MAX_NAME - 1);
        fd = sys_open(edit_filename, O_RDWR);

        if (fd >= 0) {
            // Read from file using syscall
            int read_count = read(fd, (uint8_t*)editor_buffer, MAX_EDITOR_SIZE);
            if (read_count >= 0) {
                editor_buffer[read_count] = '\0';
                current_len = read_count;
            }
            close(fd);
        }
    }

    // 1. UI Setup
    sys_clear_screen();
    printf("Simple Text Editor (Fixed Block Mode)\n");
    printf("CTRL+S: Save | CTRL+X: Exit\n");
    printf("----------------------------------\n");
    printf("File: %s\n\n", strlen(initial_filename) > 0 ? initial_filename : "[New File]");

    // Print Content
    for (size_t i = 0; i < current_len; ++i) {
        putchar(editor_buffer[i]);
    }

    // 2. Editing Loop
    int done = 0;
    while (!done) {
        char c = getchar();
        if (c == CTRL_S) done = 1;
        else if (c == CTRL_X) done = 2;
        else if (c == '\b') {
            if (current_len > 0) {
                current_len--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else if (c == '\n') {
            if (current_len < MAX_EDITOR_SIZE) {
                editor_buffer[current_len++] = '\n';
                putchar('\n');
            }
        } else if ((c >= ' ' && c <= '~')) {
            if (current_len < MAX_EDITOR_SIZE) {
                editor_buffer[current_len++] = c;
                putchar(c);
            }
        }
    }

    sys_clear_screen();
    if (done == 2) {
        printf("Exited without saving.\n");
        return;
    }

    // 3. Save Logic
    char filename[FS_MAX_NAME];
    if (strlen(initial_filename) > 0) {
        strcpy(filename, initial_filename);
    } else {
        printf("Enter filename: ");
        // Note: Simple read loop here for filename
        int i = 0;
        while(1) {
            char c = getchar();
            if (c == '\n') { filename[i] = '\0'; putchar('\n'); break; }
            else if (c == '\b' && i > 0) { i--; putchar('\b'); putchar(' '); putchar('\b'); }
            else if (c >= ' ' && c <= '~' && i < FS_MAX_NAME - 1) { filename[i++] = c; putchar(c); }
        }
    }

    if (strlen(filename) == 0) {
        printf("Save cancelled.\n");
        return;
    }

    // Try to open/create file
    fd = sys_open(filename, O_RDWR | O_CREAT);
    if (fd >= 0) {
        int written = write(fd, editor_buffer, current_len);
        if (written >= 0) {
            printf("File saved successfully.\n");
        } else {
            printf("Error: Could not write to file.\n");
        }
        close(fd);
    } else {
        printf("Error: Could not open/create file.\n");
    }
}
