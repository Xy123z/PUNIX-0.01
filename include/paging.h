/**
 * include/paging.h - Paging and Virtual Memory Management
 * Implements 2-level paging (Page Directory + Page Tables)
 */

#ifndef PAGING_H
#define PAGING_H

#include "types.h"

// Page size: 4KB (standard x86 page size)
#define PAGE_SIZE 4096

// Number of entries in page directory and page tables
#define PAGE_ENTRIES 1024

// Page flags
#define PAGE_PRESENT    0x001  // Page is present in memory
#define PAGE_RW         0x002  // Read/Write (1 = writable, 0 = read-only)
#define PAGE_USER       0x004  // User/Supervisor (1 = user, 0 = supervisor)
#define PAGE_ACCESSED   0x020  // Page has been accessed
#define PAGE_DIRTY      0x040  // Page has been written to
#define PAGE_COW        0x200  // Copy-On-Write (bit 9 is available for OS)

// Page directory entry and page table entry are both 32-bit values
typedef uint32_t page_t;

// Page directory - contains pointers to page tables
typedef struct page_directory {
    page_t entries[PAGE_ENTRIES];  // 1024 entries
} __attribute__((aligned(PAGE_SIZE))) page_directory_t;

// Page table - contains pointers to physical pages
typedef struct page_table {
    page_t entries[PAGE_ENTRIES];  // 1024 entries
} __attribute__((aligned(PAGE_SIZE))) page_table_t;

// Current page directory (one per process, but we start with kernel's)
extern page_directory_t* kernel_page_directory;
extern page_directory_t* current_page_directory;

/**
 * @brief Initialize paging subsystem
 * Sets up kernel page directory with identity mapping for kernel code/data
 */
void paging_init();

/**
 * @brief Enable paging by setting CR0 and CR3 registers
 */
void paging_enable();

/**
 * @brief Create a new page directory (for a new process)
 * @return Pointer to new page directory
 */
page_directory_t* paging_create_directory();

/**
 * @brief Clone a page directory (for fork)
 * @param src Source page directory to clone
 * @return Pointer to cloned page directory
 */
page_directory_t* paging_clone_directory(page_directory_t* src);

/**
 * @brief Free a page directory and all its page tables
 * @param dir Page directory to free
 */
void paging_free_directory(page_directory_t* dir);

/**
 * @brief Map a virtual address to a physical address
 * @param dir Page directory to use
 * @param virt Virtual address
 * @param phys Physical address
 * @param flags Page flags (PAGE_PRESENT | PAGE_RW | PAGE_USER, etc.)
 */
void paging_map_page(page_directory_t* dir, uint32_t virt, uint32_t phys, uint32_t flags);

/**
 * @brief Unmap a virtual address
 * @param dir Page directory to use
 * @param virt Virtual address to unmap
 */
void paging_unmap_page(page_directory_t* dir, uint32_t virt);

/**
 * @brief Get the physical address for a virtual address
 * @param dir Page directory to use
 * @param virt Virtual address
 * @return Physical address, or 0 if not mapped
 */
uint32_t paging_get_physical(page_directory_t* dir, uint32_t virt);

/**
 * @brief Switch to a different page directory
 * @param dir Page directory to switch to
 */
void paging_switch_directory(page_directory_t* dir);

void paging_set_user_access(page_directory_t* dir, uint32_t virt_start, uint32_t size, int user);
void page_fault_handler(uint32_t error_code, uint32_t fault_addr);
page_t* paging_get_page(uint32_t address, int make, page_directory_t* dir);

/**
 * @brief Map a range of memory (multiple pages)
 * @param dir Page directory
 * @param virt_start Virtual address start
 * @param phys_start Physical address start
 * @param size Size in bytes
 * @param flags Page flags
 */
void paging_map_range(page_directory_t* dir, uint32_t virt_start, 
                      uint32_t phys_start, uint32_t size, uint32_t flags);

/**
 * @brief Allocate a page for user space
 * @param dir Page directory
 * @param virt Virtual address to allocate at
 * @param flags Page flags
 * @return 1 on success, 0 on failure
 */
int paging_alloc_page(page_directory_t* dir, uint32_t virt, uint32_t flags);

/**
 * @brief Free a page from user space
 * @param dir Page directory
 * @param virt Virtual address to free
 */
void paging_free_page(page_directory_t* dir, uint32_t virt);

/**
 * @brief Checks if a virtual address range is mapped and has user access.
 * @return 1 if user-accessible, 0 otherwise.
 */
int paging_is_user_range(page_directory_t* dir, uint32_t virt, uint32_t size);

#endif // PAGING_H
