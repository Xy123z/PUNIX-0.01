// include/memory.h - Memory management

#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

// Memory configuration
#define PAGE_SIZE 4096
#define KERNEL_START 0x10000
#define KERNEL_END   0x400000
#define MEMORY_END   0x2000000
#define TOTAL_PAGES ((MEMORY_END - KERNEL_END) / PAGE_SIZE)

// Physical memory manager
void pmm_init();
void* pmm_alloc_page();
void* pmm_alloc_pages(uint32_t count);
void pmm_free_page(void* addr);
void pmm_ref_page(void* addr);
void pmm_unref_page(void* addr);
uint32_t pmm_get_ref(void* addr);
void pmm_get_stats(uint32_t* total, uint32_t* used, uint32_t* free);

// Heap allocator
void heap_init();
void* kmalloc(size_t size);
void kfree(void* ptr);

#endif // MEMORY_H
