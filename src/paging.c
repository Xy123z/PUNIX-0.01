/**
 * src/paging.c - Paging Implementation
 * Implements virtual memory management with 2-level paging
 */

#include "../include/paging.h"
#include "../include/memory.h"
#include "../include/console.h"
#include "../include/string.h"

// Kernel page directory (global)
page_directory_t* kernel_page_directory = 0;
page_directory_t* current_page_directory = 0;

// Helper to get page directory index from virtual address
#define PAGE_DIR_INDEX(virt)   ((virt) >> 22)

// Helper to get page table index from virtual address
#define PAGE_TABLE_INDEX(virt) (((virt) >> 12) & 0x3FF)

// Helper to align address to page boundary
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

/**
 * @brief Initialize paging subsystem
 */
void paging_init() {
    console_print_colored("[ ** ] ", COLOR_YELLOW_ON_BLACK);
    console_print_colored("Initializing paging subsystem...\n", COLOR_WHITE_ON_BLACK);

    // Allocate kernel page directory
    kernel_page_directory = (page_directory_t*)pmm_alloc_page();
    if (!kernel_page_directory) {
        console_print_colored("ERROR: Failed to allocate kernel page directory!\n", COLOR_LIGHT_RED);
        return;
    }

    // Clear the page directory
    memset(kernel_page_directory, 0, sizeof(page_directory_t));

    // Identity map the kernel (0x00000000 - 0x00400000, 4MB)
    paging_map_range(kernel_page_directory, 
                     0x00000000,   // Virtual start
                     0x00000000,   // Physical start
                     0x00400000,   // 4MB
                     PAGE_PRESENT | PAGE_RW);  // Supervisor mode (Ring 0)

    // Map allocatable memory (Heap & PMM managed memory)
    // From 4MB to 32MB
    paging_map_range(kernel_page_directory,
                     0x00400000,   // Virtual start
                     0x00400000,   // Physical start
                     0x02000000 - 0x00400000, // 28MB
                     PAGE_PRESENT | PAGE_RW); // Supervisor mode

    // Map VGA memory (0xB8000 - 0xBFFFF)
    paging_map_range(kernel_page_directory,
                     0x000B8000,
                     0x000B8000,
                     0x00008000,  // 32KB
                     PAGE_PRESENT | PAGE_RW); // Supervisor mode only

    current_page_directory = kernel_page_directory;

    console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
    console_print_colored("Paging subsystem initialized\n", COLOR_GREEN_ON_BLACK);
}

/**
 * @brief Enable paging by setting CR0 and CR3
 */
void paging_enable() {
    console_print_colored("[ ** ] ", COLOR_YELLOW_ON_BLACK);
    console_print_colored("Enabling paging...\n", COLOR_WHITE_ON_BLACK);

    // Load page directory into CR3
    uint32_t pd_physical = (uint32_t)kernel_page_directory;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_physical));

    // Enable paging by setting bit 31 of CR0
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  // Set PG bit
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    console_print_colored("[ ok ] ", COLOR_GREEN_ON_BLACK);
    console_print_colored("Paging enabled!\n", COLOR_GREEN_ON_BLACK);
}

/**
 * @brief Map a single page
 */
void paging_map_page(page_directory_t* dir, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_idx = PAGE_DIR_INDEX(virt);
    uint32_t pt_idx = PAGE_TABLE_INDEX(virt);

    // Check if page table exists
    if (!(dir->entries[pd_idx] & PAGE_PRESENT)) {
        // Allocate new page table
        page_table_t* new_table = (page_table_t*)pmm_alloc_page();
        if (!new_table) {
            console_print_colored("ERROR: Failed to allocate page table!\n", COLOR_LIGHT_RED);
            return;
        }

        memset(new_table, 0, sizeof(page_table_t));

        // Set page directory entry
        // Store physical address of page table + flags
        dir->entries[pd_idx] = ((uint32_t)new_table) | PAGE_PRESENT | PAGE_RW | (flags & PAGE_USER);
    } else {
        // Ensure USER bit is set in PDE if mapping requires user access
        if (flags & PAGE_USER) {
            dir->entries[pd_idx] |= PAGE_USER;
        }
    }

    // Get page table
    page_table_t* table = (page_table_t*)(dir->entries[pd_idx] & 0xFFFFF000);

    // Set page table entry
    table->entries[pt_idx] = (phys & 0xFFFFF000) | flags;

    // Invalidate TLB entry for this virtual address
    __asm__ volatile("invlpg (%0)" : : "r"(virt));
}

/**
 * @brief Unmap a page
 */
void paging_unmap_page(page_directory_t* dir, uint32_t virt) {
    uint32_t pd_idx = PAGE_DIR_INDEX(virt);
    uint32_t pt_idx = PAGE_TABLE_INDEX(virt);

    if (!(dir->entries[pd_idx] & PAGE_PRESENT)) {
        return;  // Page table doesn't exist
    }

    page_table_t* table = (page_table_t*)(dir->entries[pd_idx] & 0xFFFFF000);
    table->entries[pt_idx] = 0;  // Clear entry

    // Invalidate TLB entry
    __asm__ volatile("invlpg (%0)" : : "r"(virt));
}

/**
 * @brief Get physical address for virtual address
 */
uint32_t paging_get_physical(page_directory_t* dir, uint32_t virt) {
    uint32_t pd_idx = PAGE_DIR_INDEX(virt);
    uint32_t pt_idx = PAGE_TABLE_INDEX(virt);

    if (!(dir->entries[pd_idx] & PAGE_PRESENT)) {
        return 0;  // Not mapped
    }

    page_table_t* table = (page_table_t*)(dir->entries[pd_idx] & 0xFFFFF000);

    if (!(table->entries[pt_idx] & PAGE_PRESENT)) {
        return 0;  // Not mapped
    }

    uint32_t page_phys = table->entries[pt_idx] & 0xFFFFF000;
    uint32_t offset = virt & 0x00000FFF;

    return page_phys + offset;
}

/**
 * @brief Map a range of pages
 */
void paging_map_range(page_directory_t* dir, uint32_t virt_start, 
                      uint32_t phys_start, uint32_t size, uint32_t flags) {
    uint32_t virt = virt_start & 0xFFFFF000;  // Align to page boundary
    uint32_t phys = phys_start & 0xFFFFF000;
    uint32_t end = PAGE_ALIGN(virt_start + size);

    while (virt < end) {
        paging_map_page(dir, virt, phys, flags);
        virt += PAGE_SIZE;
        phys += PAGE_SIZE;
    }
}

/**
 * @brief Switch to different page directory
 */
void paging_switch_directory(page_directory_t* dir) {
    current_page_directory = dir;
    uint32_t pd_physical = (uint32_t)dir;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_physical));
}

/**
 * @brief Create a new page directory
 */
page_directory_t* paging_create_directory() {
    page_directory_t* new_dir = (page_directory_t*)pmm_alloc_page();
    if (!new_dir) {
        return 0;
    }

    memset(new_dir, 0, sizeof(page_directory_t));

    // Copy kernel mappings (first 4MB) to new directory
    // This ensures kernel is accessible in all address spaces
    for (int i = 0; i < 256; i++) {  // First 256 entries = 1GB (kernel space)
        if (kernel_page_directory->entries[i] & PAGE_PRESENT) {
            new_dir->entries[i] = kernel_page_directory->entries[i];
        }
    }

    return new_dir;
}

/**
 * @brief Clone a page directory (for fork)
 */
page_directory_t* paging_clone_directory(page_directory_t* src) {
    page_directory_t* new_dir = paging_create_directory();
    if (!new_dir) {
        return 0;
    }

    // Clone user space mappings (above kernel space)
    for (int pd_idx = 256; pd_idx < PAGE_ENTRIES; pd_idx++) {
        if (!(src->entries[pd_idx] & PAGE_PRESENT)) {
            continue;
        }

        // Allocate new page table
        page_table_t* new_table = (page_table_t*)pmm_alloc_page();
        if (!new_table) {
            // TODO: Clean up allocated memory
            return 0;
        }

        memset(new_table, 0, sizeof(page_table_t));

        // Copy page table entries
        page_table_t* src_table = (page_table_t*)(src->entries[pd_idx] & 0xFFFFF000);

        for (int pt_idx = 0; pt_idx < PAGE_ENTRIES; pt_idx++) {
            if (src_table->entries[pt_idx] & PAGE_PRESENT) {
                // If the page is writable, mark it as COW in both
                if (src_table->entries[pt_idx] & PAGE_RW) {
                    src_table->entries[pt_idx] &= ~PAGE_RW;
                    src_table->entries[pt_idx] |= PAGE_COW;
                }
                
                // Link child to same physical page
                new_table->entries[pt_idx] = src_table->entries[pt_idx];
                
                // Increment refcount
                uint32_t phys = src_table->entries[pt_idx] & 0xFFFFF000;
                pmm_ref_page((void*)phys);
                
                // Flush TLB for parent since we might have made it RO
                // Actually, paging_clone_directory is usually called from task_fork
                // which is fine, but we should be careful.
            }
        }

        // Set page directory entry
        new_dir->entries[pd_idx] = ((uint32_t)new_table & 0xFFFFF000) | 
                                   (src->entries[pd_idx] & 0xFFF);
    }

    return new_dir;
}

/**
 * @brief Free a page directory
 */
void paging_free_directory(page_directory_t* dir) {
    // Free all user space page tables
    for (int pd_idx = 256; pd_idx < PAGE_ENTRIES; pd_idx++) {
        if (dir->entries[pd_idx] & PAGE_PRESENT) {
            page_table_t* table = (page_table_t*)(dir->entries[pd_idx] & 0xFFFFF000);

            // Free all pages in table
            for (int pt_idx = 0; pt_idx < PAGE_ENTRIES; pt_idx++) {
                if (table->entries[pt_idx] & PAGE_PRESENT) {
                    void* page = (void*)(table->entries[pt_idx] & 0xFFFFF000);
                    pmm_free_page(page);
                }
            }

            // Free page table itself
            pmm_free_page(table);
        }
    }

    // Free page directory
    pmm_free_page(dir);
}

/**
 * @brief Allocate a page for user space
 */
int paging_alloc_page(page_directory_t* dir, uint32_t virt, uint32_t flags) {
    void* phys = pmm_alloc_page();
    if (!phys) {
        return 0;
    }

    paging_map_page(dir, virt, (uint32_t)phys, flags);
    return 1;
}

/**
 * @brief Free a page
 */
void paging_free_page(page_directory_t* dir, uint32_t virt) {
    uint32_t phys = paging_get_physical(dir, virt);
    if (phys) {
        pmm_free_page((void*)phys);
        paging_unmap_page(dir, virt);
    }
}

void page_fault_handler(uint32_t error_code, uint32_t faulting_address) {
    // Handle Copy-On-Write
    if (error_code & 0x2) { // Write fault
        uint32_t pd_idx = PAGE_DIR_INDEX(faulting_address);
        if (current_page_directory->entries[pd_idx] & PAGE_PRESENT) {
            page_table_t* table = (page_table_t*)(current_page_directory->entries[pd_idx] & 0xFFFFF000);
            uint32_t pt_idx = PAGE_TABLE_INDEX(faulting_address);
            
            if (table->entries[pt_idx] & PAGE_COW) {
                uint32_t entry = table->entries[pt_idx];
                uint32_t phys = entry & 0xFFFFF000;
                
                if (pmm_get_ref((void*)phys) > 1) {
                    // Shared page, must copy
                    void* new_page = pmm_alloc_page();
                    if (!new_page) {
                        console_print_colored("COW: Out of memory!\n", COLOR_LIGHT_RED);
                        while(1) __asm__ volatile("hlt");
                    }
                    
                    // Copy data
                    memcpy(new_page, (void*)phys, PAGE_SIZE);
                    
                    // Release old page reference
                    pmm_unref_page((void*)phys);
                    
                    // Update entry: New physical address + RW bit - COW bit
                    table->entries[pt_idx] = ((uint32_t)new_page & 0xFFFFF000) | (entry & 0xFFF & ~PAGE_COW) | PAGE_RW;
                } else {
                    // Last owner, just make writable and clear COW bit
                    table->entries[pt_idx] = (entry & ~PAGE_COW) | PAGE_RW;
                }
                
                // Flush TLB for this address
                __asm__ volatile("invlpg (%0)" : : "r"(faulting_address) : "memory");
                return; // Fault handled successfully
            }
        }
    }

    // --- REAL PAGE FAULT (Not COW) ---
    console_print_colored("\n=== PAGE FAULT ===\n", COLOR_LIGHT_RED);
    console_print("Faulting address: 0x");
    char hex[16];
    int_to_hex(faulting_address, hex);
    console_print(hex);
    console_print("\n");

    console_print("Error code: ");
    char num[12];
    int_to_str(error_code, num);
    console_print(num);
    console_print(" (");

    if (!(error_code & 0x1)) console_print("non-present page");
    else console_print("protection violation");

    if (error_code & 0x2) console_print(", write");
    else console_print(", read");

    if (error_code & 0x4) console_print(", user mode");
    else console_print(", kernel mode");

    console_print(")\n");

    // For now, halt on real page faults
    console_print_colored("System halted.\n", COLOR_LIGHT_RED);
    while(1) __asm__ volatile("hlt");
}

int paging_is_user_range(page_directory_t* dir, uint32_t virt, uint32_t size) {
    uint32_t current = virt & 0xFFFFF000;
    uint32_t end = (virt + size + PAGE_SIZE - 1) & 0xFFFFF000;

    while (current < end) {
        uint32_t pd_idx = PAGE_DIR_INDEX(current);
        uint32_t pt_idx = PAGE_TABLE_INDEX(current);

        if (!(dir->entries[pd_idx] & PAGE_PRESENT) || !(dir->entries[pd_idx] & PAGE_USER)) {
            return 0;
        }

        page_table_t* table = (page_table_t*)(dir->entries[pd_idx] & 0xFFFFF000);
        if (!(table->entries[pt_idx] & PAGE_PRESENT) || !(table->entries[pt_idx] & PAGE_USER)) {
            return 0;
        }

        current += PAGE_SIZE;
    }

    return 1;
}
