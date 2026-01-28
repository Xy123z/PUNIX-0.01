/**
 * src/auth.c - Authentication and Credential Management
 * Stores username and password in /etc/credentials file
 */

#include "../include/auth.h"
#include "../include/fs.h"
#include "../include/string.h"
#include "../include/console.h"

#define CREDENTIALS_FILE "credentials"
#define CREDENTIALS_MAGIC 0xC8ED1234  // Magic number for validation

// Credential structure (stored in /etc/credentials file)
typedef struct {
    uint32_t magic;                    // Validation magic
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    uint8_t padding[400];              // Pad to ~512 bytes
} credentials_t;

// Global variables
char ROOT_PASSWORD[MAX_PASSWORD_LEN] = {0};
char USERNAME[MAX_USERNAME_LEN] = {0};

/**
 * @brief Checks if credentials file exists
 */
static int credentials_exist() {
    // Look for /etc directory
    fs_node_t* etc_dir = fs_find_node("etc", fs_root_id);
    if (!etc_dir) {
        return 0;
    }

    // Look for credentials file in /etc
    uint32_t cred_id = fs_find_node_local_id(etc_dir->id, CREDENTIALS_FILE);
    return (cred_id != 0);
}

/**
 * @brief Loads credentials from disk
 * @return 1 if successful, 0 if not found or invalid
 */
int auth_load_credentials() {
    // Find /etc directory
    fs_node_t* etc_dir = fs_find_node("etc", fs_root_id);
    if (!etc_dir) {
        return 0;
    }

    // Find credentials file
    uint32_t cred_id = fs_find_node_local_id(etc_dir->id, CREDENTIALS_FILE);
    if (cred_id == 0) {
        return 0;
    }

    fs_node_t* cred_file = fs_get_node(cred_id);
    if (!cred_file || cred_file->type != FS_TYPE_FILE) {
        return 0;
    }

    // Read credentials via new API
    credentials_t creds;
    if (fs_read(cred_file, 0, sizeof(credentials_t), (uint8_t*)&creds) < sizeof(uint32_t)) {
        return 0;
    }

    // Validate magic number
    if (creds.magic != CREDENTIALS_MAGIC) {
        console_print_colored("Warning: Credentials file corrupted.\n", COLOR_YELLOW_ON_BLACK);
        return 0;
    }

    // Load into global variables
    strcpy(USERNAME, creds.username);
    strcpy(ROOT_PASSWORD, creds.password);

    return 1;
}

/**
 * @brief Saves credentials to disk
 * @return 1 if successful, 0 if failed
 */
int auth_save_credentials() {
    // Ensure /etc directory exists
    fs_node_t* etc_dir = fs_find_node("etc", fs_root_id);
    if (!etc_dir) {
        // Create /etc directory
        if (!fs_create_node(fs_root_id, "etc", FS_TYPE_DIRECTORY, 0, 0)) {
            console_print_colored("Error: Failed to create /etc directory.\n", COLOR_LIGHT_RED);
            return 0;
        }
        etc_dir = fs_find_node("etc", fs_root_id);
        if (!etc_dir) {
            return 0;
        }
    }

    // Check if credentials file exists
    uint32_t cred_id = fs_find_node_local_id(etc_dir->id, CREDENTIALS_FILE);
    fs_node_t* cred_file = 0;

    if (cred_id == 0) {
        // Create credentials file
        if (!fs_create_node(etc_dir->id, CREDENTIALS_FILE, FS_TYPE_FILE, 0, 0)) {
            console_print_colored("Error: Failed to create credentials file.\n", COLOR_LIGHT_RED);
            return 0;
        }
        cred_id = fs_find_node_local_id(etc_dir->id, CREDENTIALS_FILE);
        cred_file = fs_get_node(cred_id);
    } else {
        cred_file = fs_get_node(cred_id);
    }

    if (!cred_file) {
        return 0;
    }

    // Prepare and write credentials via new API
    credentials_t creds;
    memset(&creds, 0, sizeof(credentials_t));
    creds.magic = CREDENTIALS_MAGIC;
    strcpy(creds.username, USERNAME);
    strcpy(creds.password, ROOT_PASSWORD);

    if (fs_write(cred_file, 0, sizeof(credentials_t), (uint8_t*)&creds) < sizeof(credentials_t)) {
        return 0;
    }

    fs_update_node(cred_file);

    return 1;
}

/**
 * @brief Initializes authentication system
 * Called during boot - prompts for credentials if first boot
 * @param read_line_func Pointer to function for reading user input
 */
void auth_init(void (*read_line_func)(char*, int)) {
    if (credentials_exist() && auth_load_credentials()) {
        // Credentials found - load them
        console_print_colored("Welcome back, ", COLOR_GREEN_ON_BLACK);
        console_print(USERNAME);
        console_print("!\n");
    } else {
        // First boot - prompt for credentials
        console_print_colored("=== First Boot Setup ===\n", COLOR_YELLOW_ON_BLACK);
        console_print_colored("Create your account:\n\n", COLOR_GREEN_ON_BLACK);

        char pass[MAX_PASSWORD_LEN];
        char pass_conf[MAX_PASSWORD_LEN];
        char user[MAX_USERNAME_LEN];

        console_print_colored("Enter username (max 39 chars): ", COLOR_GREEN_ON_BLACK);
        read_line_func(user, MAX_USERNAME_LEN);
        strcpy(USERNAME, user);

        do {
            console_print_colored("Enter root password (max 39 chars): ", COLOR_GREEN_ON_BLACK);
            read_line_func(pass, MAX_PASSWORD_LEN);
            console_print_colored("Confirm password: ", COLOR_GREEN_ON_BLACK);
            read_line_func(pass_conf, MAX_PASSWORD_LEN);

            if (strcmp(pass, pass_conf) == 0) {
                strcpy(ROOT_PASSWORD, pass_conf);
                break;
            }
            console_print_colored("Passwords didn't match, please try again.\n", COLOR_YELLOW_ON_BLACK);
        } while (strcmp(pass, pass_conf));

        // Save to disk
        if (auth_save_credentials()) {
            console_print_colored("\nAccount created successfully!\n", COLOR_GREEN_ON_BLACK);
        } else {
            console_print_colored("\nWarning: Failed to save credentials to disk.\n", COLOR_YELLOW_ON_BLACK);
        }
    }
}

/**
 * @brief Changes username (must be authenticated)
 * @param read_line_func Pointer to function for reading user input
 * @return 1 if successful, 0 if failed
 */
int auth_change_username(void (*read_line_func)(char*, int)) {
    char new_username[MAX_USERNAME_LEN];

    console_print_colored("Enter new username (max 39 chars): ", COLOR_GREEN_ON_BLACK);
    read_line_func(new_username, MAX_USERNAME_LEN);

    if (strlen(new_username) == 0) {
        console_print_colored("Error: Username cannot be empty.\n", COLOR_LIGHT_RED);
        return 0;
    }

    // Update global variable
    strcpy(USERNAME, new_username);

    // Save to disk
    if (auth_save_credentials()) {
        console_print_colored("Username changed successfully!\n", COLOR_GREEN_ON_BLACK);
        return 1;
    } else {
        console_print_colored("Error: Failed to save new username.\n", COLOR_LIGHT_RED);
        return 0;
    }
}

/**
 * @brief Changes password (must be authenticated)
 * @param read_line_func Pointer to function for reading user input
 * @return 1 if successful, 0 if failed
 */
int auth_change_password(void (*read_line_func)(char*, int)) {
    char new_pass[MAX_PASSWORD_LEN];
    char new_pass_conf[MAX_PASSWORD_LEN];

    do {
        console_print_colored("Enter new password (max 39 chars): ", COLOR_GREEN_ON_BLACK);
        read_line_func(new_pass, MAX_PASSWORD_LEN);
        console_print_colored("Confirm new password: ", COLOR_GREEN_ON_BLACK);
        read_line_func(new_pass_conf, MAX_PASSWORD_LEN);

        if (strcmp(new_pass, new_pass_conf) == 0) {
            break;
        }
        console_print_colored("Passwords don't match, please try again.\n", COLOR_YELLOW_ON_BLACK);
    } while (1);

    if (strlen(new_pass) == 0) {
        console_print_colored("Error: Password cannot be empty.\n", COLOR_LIGHT_RED);
        return 0;
    }

    // Update global variable
    strcpy(ROOT_PASSWORD, new_pass);

    // Save to disk
    if (auth_save_credentials()) {
        console_print_colored("Password changed successfully!\n", COLOR_GREEN_ON_BLACK);
        return 1;
    } else {
        console_print_colored("Error: Failed to save new password.\n", COLOR_LIGHT_RED);
        return 0;
    }
}

void auth_set_username(const char* name) {
    if (!name) return;
    strncpy(USERNAME, name, MAX_USERNAME_LEN - 1);
    USERNAME[MAX_USERNAME_LEN - 1] = '\0';
    auth_save_credentials();
}

void auth_set_password(const char* pass) {
    if (!pass) return;
    strncpy(ROOT_PASSWORD, pass, MAX_PASSWORD_LEN - 1);
    ROOT_PASSWORD[MAX_PASSWORD_LEN - 1] = '\0';
    auth_save_credentials();
}
