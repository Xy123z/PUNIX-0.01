#include "../include/ata.h"
#include "../include/types.h"
#include "../include/interrupt.h" // For inb/outb
#include "../include/console.h"   // For console_print_colored

// --- ATA I/O Ports (Primary Bus, Master Drive) ---
#define ATA_PRIMARY_BASE_IO 0x1F0
#define ATA_PRIMARY_DCR_AS 0x3F6 // Device Control Register / Alternate Status

// Register offsets relative to ATA_PRIMARY_BASE_IO (0x1F0)
#define ATA_REG_DATA 0x00      // Data Register (16-bit)
#define ATA_REG_ERROR 0x01     // Error Register (R)
#define ATA_REG_SECTOR_COUNT 0x02 // Sector Count Register (R/W)
#define ATA_REG_LBA_LOW 0x03   // LBA 0-7 (R/W)
#define ATA_REG_LBA_MID 0x04   // LBA 8-15 (R/W)
#define ATA_REG_LBA_HIGH 0x05  // LBA 16-23 (R/W)
#define ATA_REG_DRIVE_SEL 0x06 // Drive/Head Register (R/W)
#define ATA_REG_STATUS 0x07    // Status Register (R)
#define ATA_REG_COMMAND 0x07   // Command Register (W)

// ATA Commands
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_WRITE_PIO 0x30

// --- CRITICAL FIX: Add 16-bit I/O functions ---
static inline uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
// -----------------------------------------------

/**
 * @brief Waits for the ATA controller to not be busy (BSY flag clear).
 * @return 0 on success, -1 on timeout/failure (error bit set).
 */
static int ata_wait_for_ready() {
    // Read status four times for the "400ns delay"
    for (int i = 0; i < 4; i++) {
        inb(ATA_PRIMARY_BASE_IO + ATA_REG_STATUS);
    }

    // Wait for BSY to clear and DRDY to set
    int timeout = 10000000; // Add timeout to prevent infinite loop
    while (timeout-- > 0) {
        uint8_t status = inb(ATA_PRIMARY_BASE_IO + ATA_REG_STATUS);

        // Check for Error or Device Fault
        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            return -1; // Drive reported an error
        }

        // Wait until not busy (BSY clear)
        if (!(status & ATA_SR_BSY)) {
            // Check if drive is ready (DRDY set)
            if (status & ATA_SR_DRDY) {
                return 0; // Ready
            }
        }
    }

    console_print_colored("ATA: Timeout waiting for drive.\n", COLOR_LIGHT_RED);
    return -1;
}

/**
 * @brief Handles the command setup and waits for the drive.
 */
static int ata_setup_command(uint32_t lba, uint8_t count, uint8_t command) {
    if (ata_wait_for_ready() != 0) {
        console_print_colored("ATA: Drive not ready before command.\n", COLOR_LIGHT_RED);
        return -1;
    }

    // 1. Send count
    outb(ATA_PRIMARY_BASE_IO + ATA_REG_SECTOR_COUNT, count);

    // 2. Send LBA (using LBA28 mode)
    outb(ATA_PRIMARY_BASE_IO + ATA_REG_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_BASE_IO + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_BASE_IO + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));

    // 3. Send Drive/Head & LBA mode (LBA bit 0xE0) + Master (0x00)
    outb(ATA_PRIMARY_BASE_IO + ATA_REG_DRIVE_SEL, 0xE0 | ((lba >> 24) & 0x0F));

    // 4. Send Command
    outb(ATA_PRIMARY_BASE_IO + ATA_REG_COMMAND, command);

    return 0;
}


// --- Public Interface Functions ---

void ata_init() {
    // Perform a software reset on the primary bus.
    outb(ATA_PRIMARY_DCR_AS, 0x04); // Set SRST bit
    outb(ATA_PRIMARY_DCR_AS, 0x00); // Clear SRST bit

    // Wait for the drive to become ready after reset
    if (ata_wait_for_ready() != 0) {
        console_print_colored("ATA: Initialization failed, drive not ready.\n", COLOR_LIGHT_RED);
        return;
    }

    console_print_colored("ATA: Primary Master Drive initialized.\n", COLOR_GREEN_ON_BLACK);
}


int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer) {
    if (ata_setup_command(lba, count, ATA_CMD_READ_PIO) != 0) {
        return -1;
    }

    uint16_t* buf = (uint16_t*)buffer;
    for (int j = 0; j < count; j++) {
        // Wait for the drive to be ready to transfer data (DRQ set)
        if (ata_wait_for_ready() != 0) {
            console_print_colored("ATA: Read sector failed - drive not ready.\n", COLOR_LIGHT_RED);
            return -1;
        }

        // --- CRITICAL FIX: Use inw() for 16-bit reads ---
        for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
            buf[i] = inw(ATA_PRIMARY_BASE_IO + ATA_REG_DATA);
        }
        // -----------------------------------------------

        buf += ATA_SECTOR_SIZE / 2; // Move buffer pointer to next sector location
    }

    // Final check for BSY clear and DRQ clear
    if (ata_wait_for_ready() != 0) return -1;

    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, void* buffer) {
    if (ata_setup_command(lba, count, ATA_CMD_WRITE_PIO) != 0) {
        return -1;
    }

    uint16_t* buf = (uint16_t*)buffer;
    for (int j = 0; j < count; j++) {
        // Wait for the drive to be ready to accept data (DRQ set)
        if (ata_wait_for_ready() != 0) {
            console_print_colored("ATA: Write sector failed - drive not ready.\n", COLOR_LIGHT_RED);
            return -1;
        }

        // --- CRITICAL FIX: Use outw() for 16-bit writes ---
        for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
            outw(ATA_PRIMARY_BASE_IO + ATA_REG_DATA, buf[i]);
        }
        // -----------------------------------------------

        buf += ATA_SECTOR_SIZE / 2; // Move buffer pointer to next sector location
    }

    // Send the FLUSH CACHE command to ensure data is written to the physical platter
    outb(ATA_PRIMARY_BASE_IO + ATA_REG_COMMAND, 0xE7); // ATA_CMD_CACHE_FLUSH

    // Wait for the flush to complete
    if (ata_wait_for_ready() != 0) {
        console_print_colored("ATA: Write cache flush failed.\n", COLOR_LIGHT_RED);
        return -1;
    }

    return 0;
}
