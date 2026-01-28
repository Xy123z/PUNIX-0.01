#include "../include/loader.h"
#include "../include/console.h"
#include "../include/fs.h"
#include "../include/memory.h"
#include "../include/paging.h"
#include "../include/string.h"
#include "../include/task.h"

/**
 * @brief Loads a flat binary program from disk sectors into user memory.
 * Addresses architectural faults:
 * 1. Prevents kernel overwrite by specific mapping.
 * 2. Handles multi-page programs safely.
 * 3. Clears memory to prevent data leaks.
 */
/**
 * @brief Loads a flat binary program from disk sectors into user memory.
 * Creates a truly isolated address space with unique page directory.
 */
task_t* load_user_program(task_t* target, const char* path, int argc, char** argv) {
    console_print("LOADING EXTERNAL PROGRAM: ");
    console_print(path);
    console_print("\n");

    // 0. Copy arguments to kernel memory while still in old directory
    char* kargv_strs[16]; // Max 16 args for now
    if (argc > 16) argc = 16;
    
    for (int i = 0; i < argc; i++) {
        kargv_strs[i] = (char*)kmalloc(128); 
        if (argv && argv[i]) strncpy(kargv_strs[i], argv[i], 127);
        else strcpy(kargv_strs[i], "");
    }

    // 1. Find the file
    fs_node_t* node = fs_find_node(path, current_task ? current_task->cwd_id : fs_root_id);
    if (!node) {
        console_print_colored("FATAL: File not found!\n", COLOR_LIGHT_RED);
        for (int i = 0; i < argc; i++) kfree(kargv_strs[i]);
        return NULL;
    }

    // 2. Create NEW page directory for this process
    page_directory_t* new_dir = paging_create_directory();
    if (!new_dir) {
        console_print_colored("FATAL: Failed to create page directory!\n", COLOR_LIGHT_RED);
        for (int i = 0; i < argc; i++) kfree(kargv_strs[i]);
        return NULL;
    }

    uint32_t user_virt_base = 0x80000000;
    uint32_t total_bytes = node->size;
    uint32_t pages_needed = (total_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t total_pages = pages_needed + 32; 

    for (uint32_t i = 0; i < total_pages; i++) {
        void* phys = pmm_alloc_page();
        if (!phys) {
            console_print_colored("FATAL: Out of memory during load!\n", COLOR_LIGHT_RED);
            paging_free_directory(new_dir);
            for (int i = 0; i < argc; i++) kfree(kargv_strs[i]);
            return NULL;
        }
        memset(phys, 0, PAGE_SIZE);
        paging_map_page(new_dir, user_virt_base + (i * PAGE_SIZE),
                        (uint32_t)phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);
    }

    page_directory_t* old_dir = current_page_directory;
    paging_switch_directory(new_dir);
    int bytes_read = fs_read(node, 0, node->size, (uint8_t*)user_virt_base);
    paging_switch_directory(old_dir);

    if (bytes_read != node->size) {
        console_print_colored("FATAL: Failed to read complete file!\n", COLOR_LIGHT_RED);
        paging_free_directory(new_dir);
        for (int i = 0; i < argc; i++) kfree(kargv_strs[i]);
        return NULL;
    }

    uint32_t user_stack_base = 0xC0000000;
    for (int i = 0; i < 4; i++) {
        void* phys = pmm_alloc_page();
        if (phys) memset(phys, 0, PAGE_SIZE);
        paging_map_page(new_dir, user_stack_base + (i * PAGE_SIZE),
                        (uint32_t)phys, PAGE_PRESENT | PAGE_RW | PAGE_USER);
    }

    // 3. TARGETING LOGIC
    task_t* task = target;
    if (!task) {
        // Fallback: If no target, create one if in kernel, or reuse current
        if (!current_task || current_task->id == 0) {
            task = task_create(current_task ? current_task->id : 0, new_dir, NULL);
        } else {
            task = current_task;
            if (task->page_directory && task->page_directory != kernel_page_directory) {
                paging_free_directory(task->page_directory);
            }
            task->page_directory = new_dir;
        }
    } else {
        // Use provided target
        if (task->page_directory && task->page_directory != kernel_page_directory) {
            paging_free_directory(task->page_directory);
        }
        task->page_directory = new_dir;
    }

    if (!task) {
        paging_free_directory(new_dir);
        for (int i = 0; i < argc; i++) kfree(kargv_strs[i]);
        return NULL;
    }

    // Set task name early so it's visible in ps immediately
    const char* task_name = path;
    const char* last_slash = strrchr(path, '/');
    if (last_slash) task_name = last_slash + 1;
    strncpy(task->name, task_name, 31);

    uint32_t stack_virt_top = user_stack_base + (4 * PAGE_SIZE);
    uint32_t* stack_phys_page = (uint32_t*)paging_get_physical(new_dir, stack_virt_top - PAGE_SIZE);
    
    // Note: paging_get_physical returns identity mapped physical address in kernel
    uint32_t offset = PAGE_SIZE;
    uint32_t u_argv_ptrs[16];
    
    for (int i = argc - 1; i >= 0; i--) {
        int len = strlen(kargv_strs[i]) + 1;
        offset -= len;
        memcpy((uint8_t*)stack_phys_page + offset, kargv_strs[i], len);
        u_argv_ptrs[i] = stack_virt_top - (PAGE_SIZE - offset);
        kfree(kargv_strs[i]);
    }
    
    offset &= ~3;
    offset -= 4; // NULL term for argv array
    *(uint32_t*)((uint8_t*)stack_phys_page + offset) = 0;
    
    for (int i = argc - 1; i >= 0; i--) {
        offset -= 4;
        *(uint32_t*)((uint8_t*)stack_phys_page + offset) = u_argv_ptrs[i];
    }
    
    uint32_t u_argv_base = stack_virt_top - (PAGE_SIZE - offset);
    offset -= 12; // argc, argv, envp
    uint32_t* kstack_frame = (uint32_t*)((uint8_t*)stack_phys_page + offset);
    kstack_frame[0] = argc;
    kstack_frame[1] = u_argv_base;
    kstack_frame[2] = 0; // envp
    
    uint32_t esp = stack_virt_top - (PAGE_SIZE - offset);
    task_replace(task, user_virt_base, esp);
    
    // If we are spawning from PID 1 (Init), mark the parent as background
    // to stop it from competing for focal input.
    if (task->parent_id == 1 && task->id != 1) {
        task_t* init = task_find(1);
        if (init) init->state = TASK_BACKGROUND;
    }

    return task;
}
