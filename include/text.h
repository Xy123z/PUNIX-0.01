#ifndef TEXT_H
#define TEXT_H

#include "interrupt.h"
#include "memory.h"
#include "shell.h"
#include "string.h"
#include "types.h"
#include "console.h"
#include "vga.h"
#include "fs.h" // NEW: Include the File System definitions

#define MAX_FILE_SIZE 8192

/**
 * @brief Opens the simple text editor for the given file.
 * The file is searched for within the current working directory.
 * @param edit_filename The name of the file to load for editing (or NULL/empty for new file).
 */
void text_editor(const char* edit_filename);

// Removed the file_entry_t structure as it's replaced by fs_node_t in fs.h

#endif
