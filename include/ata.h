#ifndef ATA_H
#define ATA_H

#include "types.h"

// Standard sector size
#define ATA_SECTOR_SIZE 512

// Status values
#define ATA_SR_BSY 0x80 // Busy
#define ATA_SR_DRDY 0x40 // Drive Ready
#define ATA_SR_DF 0x20 // Drive Write Fault
#define ATA_SR_ERR 0x01 // Error

// Public Interface
void ata_init();

/**
 * @brief Reads one or more sectors from the disk.
 * @param lba The starting Logical Block Address (28-bit).
 * @param count The number of sectors to read (1 to 256).
 * @param buffer The destination buffer (must be large enough).
 * @return 0 on success, -1 on failure.
 */
int ata_read_sectors(uint32_t lba, uint8_t count, void* buffer);

/**
 * @brief Writes one or more sectors to the disk.
 * @param lba The starting Logical Block Address (28-bit).
 * @param count The number of sectors to write (1 to 256).
 * @param buffer The source buffer.
 * @return 0 on success, -1 on failure.
 */
int ata_write_sectors(uint32_t lba, uint8_t count, void* buffer);

#endif // ATA_H
