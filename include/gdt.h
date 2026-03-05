#ifndef GDT_H
#define GDT_H

#include "types.h"

// GDT selectors
#define KERNEL_CODE_SEL 0x08
#define KERNEL_DATA_SEL 0x10
#define USER_CODE_SEL   0x18
#define USER_DATA_SEL   0x20
#define TSS_SEL         0x28

/**
 * @brief GDT Entry structure
 */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

/**
 * @brief GDT Pointer structure
 */
struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/**
 * @brief Task State Segment (TSS) structure
 */
struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;       // Kernel stack pointer
    uint32_t ss0;        // Kernel stack segment
    uint32_t esp1, ss1, esp2, ss2, cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi, es, cs, ss, ds, fs, gs, ldt, trap, iomap_base;
} __attribute__((packed));

void gdt_init();
void tss_set_stack(uint32_t stack);

#endif
