#include "../include/gdt.h"
#include "../include/string.h"

struct gdt_entry gdt[6];
struct gdt_ptr gdt_p;
struct tss_entry tss;

extern void gdt_flush(uint32_t gdt_ptr);
extern void tss_flush();

/**
 * @brief Set a GDT gate
 */
static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;

    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access = access;
}

/**
 * @brief Initialize GDT and TSS
 */
void gdt_init() {
    gdt_p.limit = (sizeof(struct gdt_entry) * 6) - 1;
    gdt_p.base = (uint32_t)&gdt;

    // Null descriptor
    gdt_set_gate(0, 0, 0, 0, 0);

    // Kernel Code (0x08)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    // Kernel Data (0x10)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    // User Code (0x18)
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    // User Data (0x20)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    // TSS (0x28)
    memset(&tss, 0, sizeof(struct tss_entry));
    uint32_t base = (uint32_t)&tss;
    uint32_t limit = sizeof(struct tss_entry);
    gdt_set_gate(5, base, limit, 0x89, 0x40);

    tss.ss0 = KERNEL_DATA_SEL;
    tss.esp0 = 0x90000; // Default kernel stack

    gdt_flush((uint32_t)&gdt_p);
    tss_flush();
}

/**
 * @brief Update the kernel stack in TSS
 */
void tss_set_stack(uint32_t stack) {
    tss.esp0 = stack;
}
