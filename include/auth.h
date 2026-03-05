// include/auth.h - Authentication and Credential Management

#ifndef AUTH_H
#define AUTH_H

#define MAX_PASSWORD_LEN 40
#define MAX_USERNAME_LEN 40

/**
 * @brief Initializes authentication system
 * Loads credentials from disk or prompts for first-time setup
 * @param read_line_func Function pointer for reading user input
 */
void auth_init(void (*read_line_func)(char*, int));

/**
 * @brief Loads stored credentials from disk
 * @return 1 if successful, 0 if not found
 */
int auth_load_credentials();

/**
 * @brief Saves current credentials to disk
 * @return 1 if successful, 0 if failed
 */
int auth_save_credentials();

/**
 * @brief Changes username (requires authentication)
 * @param read_line_func Function pointer for reading user input
 * @return 1 if successful, 0 if failed
 */
int auth_change_username(void (*read_line_func)(char*, int));

/**
 * @brief Changes password (requires authentication)
 * @param read_line_func Function pointer for reading user input
 * @return 1 if successful, 0 if failed
 */
int auth_change_password(void (*read_line_func)(char*, int));

/**
 * @brief Sets the username directly (no UI)
 */
void auth_set_username(const char* name);

/**
 * @brief Sets the password directly (no UI)
 */
void auth_set_password(const char* pass);

#endif // AUTH_H
