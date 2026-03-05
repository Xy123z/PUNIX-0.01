#ifndef LOADER_H
#define LOADER_H

#include "types.h"

#include "task.h"

/**
 * @brief Loads a flat binary program from the filesystem into a task.
 * 
 * @param target The task to load the program into. If NULL, a new task is created.
 * @param path The absolute path to the program executable.
 * @param argc Argument count.
 * @param argv Argument array.
 * @return Pointer to the task that was loaded, or NULL on error.
 */
task_t* load_user_program(task_t* target, const char* path, int argc, char** argv);

#endif // LOADER_H
