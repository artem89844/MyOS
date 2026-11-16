#include "ata.h"
#include "io.h"

#define ATA_DATA        0x1F0
#define ATA_SECTOR_CNT  0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7
#define ATA_ALT_STATUS  0x3F6

#define ATA_SR_ERR      0x01
#define ATA_SR_DRQ      0x08
#define ATA_SR_DF       0x20
#define ATA_SR_DRDY     0x40
#define ATA_SR_BSY      0x80
#define ATA_LBA28_MAX   0x0FFFFFFFu
#define ATA_TIMEOUT     1000000u

static inline unsigned short inw(unsigned short port) {
    unsigned short value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void ata_400ns_delay(void) {
    (void)inb(ATA_ALT_STATUS);
    (void)inb(ATA_ALT_STATUS);
    (void)inb(ATA_ALT_STATUS);
    (void)inb(ATA_ALT_STATUS);
}

static int ata_wait_ready(unsigned char require_drq) {
    unsigned int i;
    for (i = 0; i < ATA_TIMEOUT; ++i) {
        unsigned char status = inb(ATA_STATUS);
        if (status == 0x00 || status == 0xFF) return 0; /* no device */
        if (status & (ATA_SR_ERR | ATA_SR_DF)) return 0;
        if (!(status & ATA_SR_BSY)) {
            if (!require_drq || (status & ATA_SR_DRQ)) return 1;
        }
    }
    return 0;
}

int ata_read_sector(unsigned int lba, unsigned char* buffer) {
    unsigned int i;
    unsigned short* words;

    if (!buffer || lba > ATA_LBA28_MAX) return 0;

    /* Select primary-master LBA28 device and allow it to settle. */
    outb(ATA_DRIVE, (unsigned char)(0xE0u | ((lba >> 24) & 0x0Fu)));
    ata_400ns_delay();
    if (!ata_wait_ready(0)) return 0;

    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LOW,  (unsigned char)(lba & 0xFFu));
    outb(ATA_LBA_MID,  (unsigned char)((lba >> 8) & 0xFFu));
    outb(ATA_LBA_HIGH, (unsigned char)((lba >> 16) & 0xFFu));
    outb(ATA_COMMAND, 0x20); /* READ SECTORS */

    if (!ata_wait_ready(1)) return 0;

    words = (unsigned short*)buffer;
    for (i = 0; i < 256; ++i) words[i] = inw(ATA_DATA);

    ata_400ns_delay();
    return ata_wait_ready(0);
}

/*
 * Disk writes are intentionally not implemented yet. MyOS has no partition
 * manager or filesystem, so allowing arbitrary LBA writes could overwrite
 * partition tables or user data. Add a dedicated, validated disk-image layer
 * before introducing a write API.
 */
