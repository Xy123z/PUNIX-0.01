/**
 * src/kernel_shell.c - specialized kernel mode shell
 */

#include "../include/types.h"
#include "../include/console.h"
#include "../include/string.h"
#include "../include/fs.h"
#include "../include/loader.h"
#include "../include/interrupt.h" 
#include "../include/syscall.h" // For sys_exit related things if needed

// We will use the existing console_read / keyboard_read functionality if available in headers, 
// otherwise we might need to declare them. 
// Assuming checking include/console.h or similar would reveal input methods.
// For now, I'll look at how shell.c did it -> sys_getchar() via syscalls? 
// But we are in kernel mode, so we should call the keyboard driver directly or console input function.
// shell.c used sys_getchar() which does `int 0x80`. 
// Since we are in kernel, we can't easily do `int 0x80` if interrupts/GDT setup isn't perfect for ring 0->ring 0 syscalls 
// (which usually works but it's overhead). 
// Better to call `keyboard_get_char()` or similar directly.
// Let's assume `char c = keyboard_read();` exists based on `src/keyboard.c` or similar.

// extern char keyboard_read(); // In interrupt.h
// extern void load_user_program(const char* path); // In loader.h
extern uint32_t kernel_esp_saved; // For saving state

// buffer for command
static char input_buffer[128];
static int input_len = 0;

// current working directory details
static uint32_t shell_cwd_id = 1; // Root ID is usually 1

// Helper to read a line
void ks_read_line(char* buf, int max_len) {
    int i = 0;
    while (i < max_len - 1) {
        char c = keyboard_read(); // Direct driver call
        if (c == '\n') {
            buf[i] = '\0';
            console_putchar('\n', COLOR_WHITE_ON_BLACK);
            return;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                console_putchar('\b', COLOR_WHITE_ON_BLACK); 
                console_putchar(' ', COLOR_WHITE_ON_BLACK);
                console_putchar('\b', COLOR_WHITE_ON_BLACK);
            }
        } else if (c >= ' ' && c <= '~') {
            buf[i] = c;
            i++;
            console_putchar(c, COLOR_WHITE_ON_BLACK);
        }
    }
    buf[i] = '\0';
}

void ks_show_prompt() {
    console_print_colored("prog_launcher:", COLOR_LIGHT_CYAN);
    
    char name[FS_MAX_NAME];
    if (shell_cwd_id == 1) { // Root
        console_print_colored("/", COLOR_LIGHT_CYAN);
    } else {
        if (fs_get_inode_name(shell_cwd_id, name)) {
            console_print_colored("/", COLOR_LIGHT_CYAN);
            console_print_colored(name, COLOR_LIGHT_CYAN);
        } else {
            console_print_colored("/?", COLOR_LIGHT_CYAN);
        }
    }
    
    console_print_colored("# ", COLOR_LIGHT_CYAN);
}

void ks_cmd_ls() {
    fs_node_t* dir = fs_get_node(shell_cwd_id);
    if (!dir || dir->type != FS_TYPE_DIRECTORY) {
        console_print("Error: Invalid directory.\n");
        return;
    }

    // Iterate blocks
    for (int i = 0; i < 12; i++) {
        if (dir->blocks[i] == 0) continue;
        
        fs_dirent_t entries[SECTOR_SIZE / sizeof(fs_dirent_t)];
        extern int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);
        ata_read_sectors(dir->blocks[i], 1, entries);

        for (int j = 0; j < (SECTOR_SIZE / sizeof(fs_dirent_t)); j++) {
             if (entries[j].inode_id != 0) {
                 console_print(entries[j].name);
                 
                 fs_node_t* child = fs_get_node(entries[j].inode_id);
                 if (child && child->type == FS_TYPE_DIRECTORY) {
                     console_print("/");
                 }
                 console_print("  ");
             }
        }
    }
    console_print("\n");
}

void ks_cmd_cd(char* path) {
    if (!path || strlen(path) == 0) return;
    
    // Check if absolute
    uint32_t start_node = shell_cwd_id;
    if (path[0] == '/') {
        start_node = 1; // Root
        path++; // skip '/'
        if (strlen(path) == 0) {
             shell_cwd_id = 1;
             return;
        }
    }

    fs_node_t* target = fs_find_node(path, start_node);
    if (target && target->type == FS_TYPE_DIRECTORY) {
        shell_cwd_id = target->id;
        // Optionally update system-wide CWD if needed for syscalls
        syscall_set_cwd(shell_cwd_id);
    } else {
        console_print("cd: Directory not found.\n");
    }
}

// Helper to clear screen
void ks_cmd_clear() {
    console_clear_screen();
}

void ks_cmd_run(char* path) {
    if (!path || strlen(path) == 0) {
        console_print("Usage: run <prog_name>\n");
        return;
    }

    // Extract filename from path (base name)
    // We strictly convert any path "X" to "/bin/X" per instructions, 
    // effectively ignoring directories if typed, OR we handle "bin/prog" -> "prog".
    // The instruction says: "sends the format '/bin/prog' format ... for all kind of paths"
    // Basically extracts the filename and assumes it is in /bin.
    
    char* filename = path;
    int len = strlen(path);
    for (int i = 0; i < len; i++) {
        if (path[i] == '/') {
            filename = &path[i+1];
        }
    }

    if (strlen(filename) == 0) return;

    char full_path[128];
    strcpy(full_path, "/bin/");
    // Overflow check
    if (strlen(filename) > 100) {
        console_print("Error: Filename too long\n");
        return;
    }
    strcpy(full_path + 5, filename);

    // Save ESP so sys_exit can return here
    __asm__ volatile("mov %%esp, %0" : "=m"(kernel_esp_saved));
    
    console_print("Launching program: ");
    console_print(full_path);
    console_print("\n");
    
    char* argv[] = {full_path, NULL};
    task_t* new_task = load_user_program(NULL, full_path, 1, argv);
    
    if (new_task) {
        task_run(new_task);
    }
    
    // If we return here, it might be an error or simple return
}

void ks_cmd_exit() {
    console_print("Halting system.\n");
    __asm__ volatile("cli; hlt");
    while(1);
}

void kern_shell_init() {
    static int initialized = 0;
    if (!initialized) {
        shell_cwd_id = 1; // Root
        syscall_set_cwd(shell_cwd_id);
        initialized = 1;
        console_print_colored("\nKernel Shell Initialized.\n", COLOR_GREEN_ON_BLACK);
    }

    while (1) {
        ks_show_prompt();
        ks_read_line(input_buffer, 128);
        
        if (strlen(input_buffer) == 0) continue;
        
        char cmd[32];
        char args[96];
        
        // Split cmd and args
        int i = 0; 
        while(input_buffer[i] && input_buffer[i] != ' ' && i < 31) {
            cmd[i] = input_buffer[i];
            i++;
        }
        cmd[i] = '\0';
        
        // Skip spaces
        while(input_buffer[i] == ' ') i++;
        
        // Rest is args
        strcpy(args, input_buffer + i);
        
        if (strcmp(cmd, "ls") == 0) {
            ks_cmd_ls();
        } else if (strcmp(cmd, "cd") == 0) {
            ks_cmd_cd(args);
        } else if (strcmp(cmd, "run") == 0) {
            ks_cmd_run(args);
        } else if (strcmp(cmd, "clear") == 0) {
            ks_cmd_clear();
        } else if (strcmp(cmd, "exit") == 0) {
            ks_cmd_exit();
        } else {
            console_print("Unknown command: ");
            console_print(cmd);
            console_print("\n");
        }
    }
}
