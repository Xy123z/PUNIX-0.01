/**
 * src/shell.c - Shell implementation (Direct Kernel Calls)
 * Bypasses system calls to avoid Ring 0 interrupt issues
 */
#include "../include/shell.h"
#include "../include/fs.h"   // Direct FS access
#include "../include/console.h"
#include "../include/memory.h"
#include "../include/interrupt.h"
#include "../include/string.h"
#include "../include/text.h"
#include "../include/auth.h"

// --- Shell Globals ---
int ROOT_ACCESS_GRANTED = 0;
char ROOT_PASSWORD[MAX_PASSWORD_LEN] = {0};
char USERNAME[MAX_USERNAME_LEN] = {0};

// History storage
static char** history = 0;
static int history_count = 0;
static int history_capacity = 0;
static char last_result[MAX_RESULT_LEN] = "";
static int has_result = 0;

static const char* current_user = USERNAME;
static const char* kernel_name = "punix-v1.04";

// Prototypes
void history_show();
void history_save();
void history_delete(int index);
void cmd_shutdown();

// Helper: Read line with visual feedback
void read_line_with_display(char* buffer, int max_len) {
    int i = 0;
    while (i < max_len - 1) {
        char c = keyboard_read();
        if (c == '\n') {
            buffer[i] = '\0';
            console_putchar('\n', COLOR_WHITE_ON_BLACK);
            break;
        } else if (c == '\b') {
            if (i > 0) {
                i--;
                console_putchar('\b', COLOR_WHITE_ON_BLACK);
                console_putchar(' ', COLOR_WHITE_ON_BLACK);
                console_putchar('\b', COLOR_WHITE_ON_BLACK);
            }
        } else if ((c >= ' ' && c <= '~')) {
            buffer[i++] = c;
            console_putchar(c, COLOR_WHITE_ON_BLACK);
        }
    }
    buffer[i] = '\0';
}

/**
 * @brief Show shell prompt with current directory
 */
static void show_prompt() {
    console_print_colored(current_user, COLOR_GREEN_ON_BLACK);
    console_print_colored("@", COLOR_WHITE_ON_BLACK);
    console_print_colored(kernel_name, COLOR_GREEN_ON_BLACK);
    console_print_colored(":", COLOR_WHITE_ON_BLACK);

    // Get current directory directly from FS global
    // Note: We don't have a full path walker, so just show current dir name
    // or special case root/a
    
    fs_node_t* cwd_node = fs_get_node(fs_current_dir_id);
    if (cwd_node) {
        if (cwd_node->id == fs_root_id) {
            console_print_colored("/", COLOR_YELLOW_ON_BLACK);
        } else {
             // Simple hack: if parent is root, print /name, else just name
             if (cwd_node->parent_id == fs_root_id) {
                 console_print_colored("/", COLOR_YELLOW_ON_BLACK);
             }
             console_print_colored(cwd_node->name, COLOR_YELLOW_ON_BLACK);
        }
    } else {
        console_print_colored("???", COLOR_YELLOW_ON_BLACK);
    }

    if (ROOT_ACCESS_GRANTED) {
        console_print_colored("# ", COLOR_LIGHT_RED);
    } else {
        console_print_colored("$ ", COLOR_WHITE_ON_BLACK);
    }
}

void shell_init() {
    console_print_colored("+================================================+\n", COLOR_GREEN_ON_BLACK);
    console_print_colored("|          PUNIX: AN EXPERIMENTAL KERNEL         |\n", COLOR_GREEN_ON_BLACK);
    console_print_colored("+================================================+\n", COLOR_GREEN_ON_BLACK);
    console_print("\n");
}

// --- Command Implementations (Direct Kernel Calls) ---

void cmd_pwd() {
    // Simplified: Just print / or /name
    fs_node_t* cwd = fs_get_node(fs_current_dir_id);
    if (!cwd) {
        console_print_colored("Error: Invalid CWD\n", COLOR_LIGHT_RED);
        return;
    }
    
    if (cwd->id == fs_root_id) {
        console_print("/\n");
    } else {
        console_print("/");
        console_print(cwd->name);
        console_print("\n");
    }
}

void cmd_ls() {
    fs_node_t* dir = fs_get_node(fs_current_dir_id);
    if (!dir) {
        console_print_colored("Error: Cannot read directory\n", COLOR_LIGHT_RED);
        return;
    }

    if (dir->child_count == 0) {
        console_print_colored("Directory is empty.\n", COLOR_YELLOW_ON_BLACK);
        return;
    }

    console_print_colored("Contents:\n", COLOR_YELLOW_ON_BLACK);

    for (int i = 0; i < dir->child_count; i++) {
        uint32_t child_id = dir->child_ids[i];
        fs_node_t* child = fs_get_node(child_id);
        
        if (child) {
            if (child->type == FS_TYPE_DIRECTORY) {
                console_print_colored(child->name, COLOR_YELLOW_ON_BLACK);
                console_print_colored("/", COLOR_YELLOW_ON_BLACK);
            } else {
                console_print_colored(child->name, COLOR_WHITE_ON_BLACK);
                console_print(" (file)");
            }
            console_print("\n");
        }
    }
}

void cmd_cd(char* path) {
    if (strlen(path) == 0) return;

    // Find node
    fs_node_t* target = fs_find_node(path, fs_current_dir_id);
    
    // Check for root directory access
    if (target && target->id == fs_root_id && !ROOT_ACCESS_GRANTED) {
        console_print_colored("root access denied\n", COLOR_LIGHT_RED);
        return;
    }

    if (target && target->type == FS_TYPE_DIRECTORY) {
        fs_current_dir_id = target->id;
        console_print_colored("Changed directory.\n", COLOR_GREEN_ON_BLACK);
    } else {
        console_print_colored("cd: Directory not found or invalid.\n", COLOR_YELLOW_ON_BLACK);
    }
}

// Helper: Parse path into parent ID and leaf name
// Returns 1 on success, 0 on failure (parent not found)
int resolve_create_path(char* path, uint32_t* parent_id_out, char* name_out) {
    int last_slash = -1;
    int len = strlen(path);

    // Find the last slash
    for (int i = 0; i < len; i++) {
        if (path[i] == '/') last_slash = i;
    }

    if (last_slash == -1) {
        // No slash: create in current dir
        *parent_id_out = fs_current_dir_id;
        strcpy(name_out, path);
        return 1;
    }

    // Handle root case if slash is at index 0 (e.g. "/tmp")
    if (last_slash == 0) {
        *parent_id_out = fs_root_id;
        strcpy(name_out, path + 1);
        return 1;
    }

    // Split path
    char dir_path[128];
    // Copy up to last slash
    strncpy(dir_path, path, last_slash);
    dir_path[last_slash] = '\0';
    
    // Name is after last slash
    strcpy(name_out, path + last_slash + 1);

    // Find parent directory
    fs_node_t* parent = fs_find_node(dir_path, fs_current_dir_id);
    if (parent && parent->type == FS_TYPE_DIRECTORY) {
        *parent_id_out = parent->id;
        return 1;
    }

    return 0; // Parent not found
}

void cmd_mkdir(char* path) {
    if (strlen(path) == 0) {
        console_print_colored("Usage: mkdir <name>\n", COLOR_YELLOW_ON_BLACK);
        return;
    }

    uint32_t parent_id;
    char name[64];
    
    if (!resolve_create_path(path, &parent_id, name)) {
        console_print_colored("mkdir: Parent directory not found.\n", COLOR_LIGHT_RED);
        return;
    }
    
    // Check if name is valid
    if (strlen(name) == 0) {
         console_print_colored("mkdir: Invalid name.\n", COLOR_LIGHT_RED);
         return;
    }

    if (fs_create_node(parent_id, name, FS_TYPE_DIRECTORY)) {
        console_print_colored("Directory created.\n", COLOR_GREEN_ON_BLACK);
    } else {
        console_print_colored("mkdir: Failed to create directory.\n", COLOR_LIGHT_RED);
    }
}

void cmd_rmdir(char* path) {
    if (strlen(path) == 0) {
        console_print_colored("Usage: rmdir <name>\n", COLOR_YELLOW_ON_BLACK);
        return;
    }

    fs_node_t* target = fs_find_node(path, fs_current_dir_id);
    if (!target) {
        console_print_colored("rmdir: Not found.\n", COLOR_LIGHT_RED);
        return;
    }

    // Protect non-empty directories checked in fs_delete_node, but good to check here too
    if (fs_delete_node(target->id)) {
        console_print_colored("Directory removed.\n", COLOR_GREEN_ON_BLACK);
    } else {
        console_print_colored("rmdir: Failed (is it empty?).\n", COLOR_LIGHT_RED);
    }
}

void cmd_cat(char* filename) {
    if (strlen(filename) == 0) {
        console_print_colored("Usage: cat <filename>\n", COLOR_YELLOW_ON_BLACK);
        return;
    }

    fs_node_t* file = fs_find_node(filename, fs_current_dir_id);
    if (!file || file->type != FS_TYPE_FILE) {
        console_print_colored("cat: Cannot open file '", COLOR_LIGHT_RED);
        console_print(filename);
        console_print("'\n");
        return;
    }

    if (file->size == 0) {
        console_print_colored("(empty file)\n", COLOR_YELLOW_ON_BLACK);
    } else {
        // Read directly from padding
        char buffer[401];
        uint32_t len = file->size;
        if (len > 364) len = 364; // Limit to new padding size
        
        memcpy(buffer, file->padding, len);
        buffer[len] = '\0';
        
        console_print(buffer);
        if (buffer[len - 1] != '\n') {
            console_print("\n");
        }
    }
}

void cmd_touch(char* filename) {
    if (strlen(filename) == 0) {
        console_print_colored("Usage: touch <filename>\n", COLOR_YELLOW_ON_BLACK);
        return;
    }

    uint32_t parent_id;
    char name[64];

    if (!resolve_create_path(filename, &parent_id, name)) {
        console_print_colored("touch: Parent directory not found.\n", COLOR_LIGHT_RED);
        return;
    }
    
    if (strlen(name) == 0) return;

    if (fs_create_node(parent_id, name, FS_TYPE_FILE)) {
        console_print_colored("File created: ", COLOR_GREEN_ON_BLACK);
        console_print(name);
        console_print("\n");
    } else {
        console_print_colored("touch: Failed to create file\n", COLOR_LIGHT_RED);
    }
}

void cmd_echo(char* args) {
    if (strlen(args) == 0) {
        console_print("\n");
        return;
    }

    // Parse: echo text [filename]
    char text[128];
    char filename[64];
    int i = 0, j = 0;

    int last_space = -1;
    for (int k = 0; args[k] != '\0'; k++) {
        if (args[k] == ' ') last_space = k;
    }

    if (last_space == -1) {
        console_print(args);
        console_print("\n");
        return;
    }

    for (i = 0; i < last_space; i++) text[i] = args[i];
    text[i] = '\0';

    i = last_space + 1;
    j = 0;
    while (args[i] != '\0') filename[j++] = args[i++];
    filename[j] = '\0';

    if (strlen(filename) == 0) {
        console_print(text);
        console_print("\n");
        return;
    }

    fs_node_t* file = fs_find_node(filename, fs_current_dir_id);
    if (!file) {
        // Does not exist, create it properly
        uint32_t parent_id;
        char name[64];
        if (resolve_create_path(filename, &parent_id, name)) {
             fs_create_node(parent_id, name, FS_TYPE_FILE);
             // Re-find to get pointer
             file = fs_find_node_local_id(parent_id, name) ? fs_get_node(fs_find_node_local_id(parent_id, name)) : 0;
        }
    }

    if (!file || file->type != FS_TYPE_FILE) {
        console_print_colored("echo: Cannot write to file\n", COLOR_LIGHT_RED);
        return;
    }

    // Write directly to padding
    int len = strlen(text);
    if (len > 363) len = 363; // Leave room for newline
    
    memcpy(file->padding, text, len);
    file->padding[len] = '\n';
    file->size = len + 1;
    
    fs_update_node(file);

    console_print_colored("Text written to ", COLOR_GREEN_ON_BLACK);
    console_print(filename);
    console_print("\n");
}

void cmd_cp(char* args) {
    char source[64];
    char dest[64];
    int i = 0, j = 0;

    while (args[i] && args[i] != ' ') source[j++] = args[i++];
    source[j] = '\0';
    if (args[i] == ' ') i++;
    j = 0;
    while (args[i]) dest[j++] = args[i++];
    dest[j] = '\0';

    if (strlen(source) == 0 || strlen(dest) == 0) {
        console_print_colored("Usage: cp <source> <dest>\n", COLOR_YELLOW_ON_BLACK);
        return;
    }

    fs_node_t* src_node = fs_find_node(source, fs_current_dir_id);
    if (!src_node || src_node->type != FS_TYPE_FILE) {
        console_print_colored("cp: Cannot open source file\n", COLOR_LIGHT_RED);
        return;
    }

    // Create dest
    uint32_t parent_id;
    char name[64];
    if (!resolve_create_path(dest, &parent_id, name)) {
        console_print_colored("cp: Dest parent not found\n", COLOR_LIGHT_RED);
        return;
    }
    
    fs_create_node(parent_id, name, FS_TYPE_FILE);
    
    // Find newly created node
    // We cannot trust fs_find_node(dest) immediately if cache is weird, but we removed cache logic in fs.c? 
    // No, fs.c still uses cache. 
    // fs_find_node_local_id should work.
    
    uint32_t dest_id = fs_find_node_local_id(parent_id, name);
    fs_node_t* dest_node = fs_get_node(dest_id);
    
    if (!dest_node) {
        console_print_colored("cp: Cannot create destination file\n", COLOR_LIGHT_RED);
        return;
    }

    memcpy(dest_node->padding, src_node->padding, src_node->size);
    dest_node->size = src_node->size;
    fs_update_node(dest_node);

    console_print_colored("Copied ", COLOR_GREEN_ON_BLACK);
    char num[12];
    int_to_str(dest_node->size, num);
    console_print(num);
    console_print(" bytes\n");
}

void cmd_add(char* args) {
    char cwd[128];
    // Simple verification
    if (fs_current_dir_id != fs_find_node("a", fs_root_id)->id) {
         console_print_colored("Mount /a for executing this command\n", COLOR_YELLOW_ON_BLACK);
         return;
    }

    char input[40];
    console_print_colored("Enter first number: ", COLOR_YELLOW_ON_BLACK);
    read_line_with_display(input, 40);
    int num1 = str_to_int(input);

    console_print_colored("Enter second number: ", COLOR_YELLOW_ON_BLACK);
    read_line_with_display(input, 40);
    int num2 = str_to_int(input);

    int sum = num1 + num2;

    last_result[0] = '\0';
    char temp[12];
    int_to_str(num1, temp);
    strcpy(last_result, temp);
    strcpy(last_result + strlen(last_result), " + ");
    int_to_str(num2, temp);
    strcpy(last_result + strlen(last_result), temp);
    strcpy(last_result + strlen(last_result), " = ");
    int_to_str(sum, temp);
    strcpy(last_result + strlen(last_result), temp);
    has_result = 1;

    console_print(last_result);
    console_print("\n");

    if (args && strlen(args) > 0 && args[0] == 's') {
        console_print_colored("Saving result to disk...\n", COLOR_YELLOW_ON_BLACK);
        
        char* filename = "results.txt";
        fs_node_t* file = fs_find_node(filename, fs_current_dir_id);
        if (!file) {
            fs_create_node(fs_current_dir_id, filename, FS_TYPE_FILE);
            file = fs_find_node(filename, fs_current_dir_id);
        }
        
        if (file) {
            int len = strlen(last_result);
            if (len > 363) len = 363;
            memcpy(file->padding, last_result, len);
            file->padding[len] = '\n';
            file->size = len + 1;
            fs_update_node(file);
            console_print_colored("Result saved to results.txt\n", COLOR_GREEN_ON_BLACK);
        } else {
            console_print_colored("Error: Could not save result\n", COLOR_LIGHT_RED);
        }
    }
}

void cmd_su() {
    if (ROOT_ACCESS_GRANTED) {
        console_print_colored("already in root mode\n", COLOR_GREEN_ON_BLACK);
        return;
    }
    char pass[MAX_PASSWORD_LEN];
    console_print_colored("enter root password: ", COLOR_GREEN_ON_BLACK);
    read_line_with_display(pass, MAX_PASSWORD_LEN);
    if (strcmp(pass, ROOT_PASSWORD) == 0) {
        console_print_colored("root access granted\n", COLOR_GREEN_ON_BLACK);
        ROOT_ACCESS_GRANTED = 1;
        fs_current_dir_id = fs_root_id; // Go to root directly
    } else {
        console_print_colored("root access denied\n", COLOR_YELLOW_ON_BLACK);
    }
}

void cmd_mem() {
    console_print_colored("=== Memory Statistics ===\n", COLOR_GREEN_ON_BLACK);

    uint32_t total, used, free;
    pmm_get_stats(&total, &used, &free);

    uint32_t total_kb = (total * PAGE_SIZE) / 1024;
    uint32_t used_kb = (used * PAGE_SIZE) / 1024;
    uint32_t free_kb = (free * PAGE_SIZE) / 1024;

    char num[16];
    console_print("Total RAM: "); int_to_str(total_kb, num); console_print(num); console_print(" KB\n");
    console_print("Used RAM:  "); int_to_str(used_kb, num); console_print(num); console_print(" KB\n");
    console_print("Free RAM:  "); int_to_str(free_kb, num); console_print(num); console_print(" KB\n");

    console_print("\n");
    console_print_colored("=== Disk Statistics ===\n", COLOR_GREEN_ON_BLACK);

    uint32_t total_disk_kb, used_disk_kb, free_disk_kb;
    fs_get_disk_stats(&total_disk_kb, &used_disk_kb, &free_disk_kb);

    console_print("Total Disk: "); int_to_str(total_disk_kb, num); console_print(num); console_print(" KB\n");
    console_print("Used Disk:  "); int_to_str(used_disk_kb, num); console_print(num); console_print(" KB\n");
    console_print("Free Disk:  "); int_to_str(free_disk_kb, num); console_print(num); console_print(" KB\n");

    console_print("\n");
    console_print_colored("=== Filesystem Cache ===\n", COLOR_GREEN_ON_BLACK);

    uint32_t cache_size, cached_nodes, dirty_nodes;
    fs_get_cache_stats(&cache_size, &cached_nodes, &dirty_nodes);

    console_print("Cache Size:    "); int_to_str(cache_size, num); console_print(num); console_print(" slots\n");
    console_print("Cached Nodes:  "); int_to_str(cached_nodes, num); console_print(num); console_print("\n");
    console_print("Dirty Nodes:   "); int_to_str(dirty_nodes, num); console_print(num); console_print(" (pending write)\n");

    uint32_t cache_usage = (cached_nodes * 100) / cache_size;
    console_print("Cache Usage:   "); int_to_str(cache_usage, num); console_print(num); console_print("%\n");
}

void cmd_sysinfo() {
    console_print_colored("=== PUNIX System Information ===\n", COLOR_GREEN_ON_BLACK);
    console_print("\n");

    // Try to read version from /boot/version
    fs_node_t* boot = fs_find_node("boot", fs_root_id);
    fs_node_t* version = 0;
    if (boot) version = fs_find_node_local_id(boot->id, "version") ? fs_get_node(fs_find_node_local_id(boot->id, "version")) : 0;
    
    if (version) {
        char buffer[128];
        uint32_t len = version->size;
        if (len > 127) len = 127;
        memcpy(buffer, version->padding, len);
        buffer[len] = '\0';
        console_print(buffer);
        console_print("\n");
    } else {
        console_print("PUNIX Kernel v1.04\n\n");
    }

    console_print_colored("Disk Layout:\n", COLOR_YELLOW_ON_BLACK);
    console_print("  Sector 0:       Bootloader (512 bytes)\n");
    console_print("  Sectors 1-60:   Kernel binary (~30 KB)\n");
    console_print("  Sector 61:      Filesystem superblock\n");
    console_print("  Sectors 62+:    Filesystem data\n");
    console_print("\n");

    console_print_colored("Current User: ", COLOR_YELLOW_ON_BLACK);
    console_print(USERNAME);
    console_print("\n");
}

void cmd_motd() {
    fs_node_t* etc = fs_find_node("etc", fs_root_id);
    if (!etc) return;
    
    fs_node_t* motd = fs_find_node_local_id(etc->id, "motd") ? fs_get_node(fs_find_node_local_id(etc->id, "motd")) : 0;
    
    if (motd) {
        char buffer[256];
        uint32_t len = motd->size;
        if (len > 255) len = 255;
        memcpy(buffer, motd->padding, len);
        buffer[len] = '\0';
        console_print_colored(buffer, COLOR_GREEN_ON_BLACK);
    } else {
        console_print_colored("No message of the day.\n", COLOR_YELLOW_ON_BLACK);
    }
}

void cmd_sudo(char* args) {
    if (strlen(args) == 0) {
        console_print_colored("Usage: sudo <command>\n", COLOR_YELLOW_ON_BLACK);
        return;
    }

    // Parse command
    char cmd[40];
    char cmd_args[40];
    int i = 0;

    while (args[i] && args[i] != ' ') {
        cmd[i] = args[i];
        i++;
    }
    cmd[i] = '\0';

    int j = 0;
    if (args[i] == ' ') {
        i++;
        while (args[i] == ' ') i++;
        while (args[i]) {
            cmd_args[j++] = args[i++];
        }
    }
    cmd_args[j] = '\0';

    if (strcmp(cmd, "shutdown") != 0 &&
        strcmp(cmd, "chuser") != 0 &&
        strcmp(cmd, "chpasswd") != 0) {
        console_print_colored("sudo: only 'shutdown', 'chuser', and 'chpasswd' are supported\n", COLOR_YELLOW_ON_BLACK);
        return;
    }

    char pass[MAX_PASSWORD_LEN];
    console_print_colored("[sudo] password for ", COLOR_YELLOW_ON_BLACK);
    console_print(current_user);
    console_print(": ");
    read_line_with_display(pass, MAX_PASSWORD_LEN);

    if (strcmp(pass, ROOT_PASSWORD) == 0) {
        if (strcmp(cmd, "shutdown") == 0) {
            cmd_shutdown();
        } else if (strcmp(cmd, "chuser") == 0) {
            auth_change_username(read_line_with_display);
        } else if (strcmp(cmd, "chpasswd") == 0) {
            auth_change_password(read_line_with_display);
        }
    } else {
        console_print_colored("sudo: authentication failed\n", COLOR_LIGHT_RED);
    }
}

void cmd_shutdown() {
    if (!ROOT_ACCESS_GRANTED) {
        console_print_colored("shutdown: permission denied (try 'sudo shutdown')\n", COLOR_LIGHT_RED);
        return;
    }

    console_clear_screen();
    console_print_colored("SHUTTING DOWN SYSTEM...\n", COLOR_LIGHT_RED);
    console_print_colored("Goodbye!\n", COLOR_GREEN_ON_BLACK);

    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x00), "Nd"((uint16_t)0x604));
    while(1) __asm__ volatile("hlt");
}

void cmd_chuser() {
    if (!ROOT_ACCESS_GRANTED) {
        console_print_colored("chuser: permission denied (try 'sudo chuser')\n", COLOR_LIGHT_RED);
        return;
    }
    auth_change_username(read_line_with_display);
}

void cmd_chpasswd() {
    if (!ROOT_ACCESS_GRANTED) {
        console_print_colored("chpasswd: permission denied (try 'sudo chpasswd')\n", COLOR_LIGHT_RED);
        return;
    }
    auth_change_password(read_line_with_display);
}

void cmd_help() {
    console_clear_screen();
    console_print_colored("+================================================+\n", COLOR_GREEN_ON_BLACK);
    console_print_colored("|       PUNIX: LIST OF AVAILABLE COMMANDS        |\n", COLOR_GREEN_ON_BLACK);
    console_print_colored("+================================================+\n", COLOR_GREEN_ON_BLACK);
    console_print("\n");

    console_print_colored("Filesystem Commands (Direct Kernel Mode):\n", COLOR_YELLOW_ON_BLACK);
    console_print("  ls            - List directory contents\n");
    console_print("  cd [dir]      - Change directory\n");
    console_print("  pwd           - Show current path\n");
    console_print("  mkdir [name]  - Create directory\n");
    console_print("  rmdir [name]  - Remove empty directory\n");
    console_print("  cat [file]    - Display file contents\n");
    console_print("  touch [file]  - Create empty file\n");
    console_print("  echo [text] [file] - Write text to file\n");
    console_print("  cp [src] [dst] - Copy file\n");
    console_print("  text [file]   - Open text editor\n");
    console_print("  sync          - Flush cache to disk\n");
    console_print("\n");

    console_print_colored("System Commands:\n", COLOR_YELLOW_ON_BLACK);
    console_print("  mem           - Show memory, disk, and cache stats\n");
    console_print("  sysinfo       - Show system information\n");
    console_print("  motd          - Show message of the day\n");
    console_print("  clear         - Clear screen\n");
    console_print("  help          - Show this help\n");
    console_print("\n");

    console_print_colored("Privilege Commands:\n", COLOR_YELLOW_ON_BLACK);
    console_print("  root          - Switch to root mode\n");
    console_print("  exit          - Exit root mode\n");
    console_print("  sudo [cmd]    - Execute command with root privilege\n");
    console_print("  shutdown      - Shutdown system (requires root)\n");
    console_print("  chuser        - Change username (requires root)\n");
    console_print("  chpasswd      - Change password (requires root)\n");
}

void cmd_clear() {
    console_clear_screen();
    shell_init();
}

void cmd_exit() {
    if (ROOT_ACCESS_GRANTED) {
        ROOT_ACCESS_GRANTED = 0;
        console_print_colored("Exited root mode\n", COLOR_GREEN_ON_BLACK);
        // Find /a
        fs_current_dir_id = fs_find_node("a", fs_root_id)->id;
    } else {
        console_print_colored("Not in root mode. Use 'shutdown' to power off.\n", COLOR_YELLOW_ON_BLACK);
    }
}

// History Functions (Stubs)
void history_save() {
    console_print_colored("History save not available.\n", COLOR_YELLOW_ON_BLACK);
}
void history_delete(int index) { console_print("Not implemented.\n"); }
void history_show() { console_print("Not implemented.\n"); }

// Main Loop
void shell_run() {
    while (1) {
        char input[40];
        show_prompt();
        read_line_with_display(input, 40);
        if (strlen(input) == 0) continue;

        char cmd[40];
        char args[40];
        int i = 0;
        while (input[i] && input[i] != ' ') { cmd[i] = input[i]; i++; }
        cmd[i] = '\0';
        int j = 0;
        if (input[i] == ' ') {
            i++;
            while (input[i] == ' ') i++;
            while (input[i]) { args[j++] = input[i++]; }
        }
        args[j] = '\0';

        // Command routing
        if (strcmp(cmd, "ls") == 0) cmd_ls();
        else if (strcmp(cmd, "pwd") == 0) cmd_pwd();
        else if (strcmp(cmd, "cd") == 0) cmd_cd(args);
        else if (strcmp(cmd, "mkdir") == 0) cmd_mkdir(args);
        else if (strcmp(cmd, "rmdir") == 0) cmd_rmdir(args);
        else if (strcmp(cmd, "cat") == 0) cmd_cat(args);
        else if (strcmp(cmd, "touch") == 0) cmd_touch(args);
        else if (strcmp(cmd, "echo") == 0) cmd_echo(args);
        else if (strcmp(cmd, "cp") == 0) cmd_cp(args);
        else if (strcmp(cmd, "help") == 0) cmd_help();
        else if (strcmp(cmd, "clear") == 0) cmd_clear();
        else if (strcmp(cmd, "mem") == 0) cmd_mem();
        else if (strcmp(cmd, "sysinfo") == 0) cmd_sysinfo();
        else if (strcmp(cmd, "motd") == 0) cmd_motd();
        else if (strcmp(cmd, "root") == 0) cmd_su();
        else if (strcmp(cmd, "exit") == 0) cmd_exit();
        else if (strcmp(cmd, "add") == 0) cmd_add(args);
        else if (strcmp(cmd, "text") == 0) text_editor(args);
        else if (strcmp(cmd, "sudo") == 0) cmd_sudo(args);
        else if (strcmp(cmd, "shutdown") == 0) cmd_shutdown();
        else if (strcmp(cmd, "sync") == 0) fs_sync();
        else if (strcmp(cmd, "chuser") == 0) cmd_chuser();
        else if (strcmp(cmd, "chpasswd") == 0) cmd_chpasswd();
        else {
            console_print(cmd);
            console_print_colored(": command not found\n", COLOR_YELLOW_ON_BLACK);
        }
    }
}
