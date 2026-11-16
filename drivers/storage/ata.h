#ifndef ATA_H
#define ATA_H

/* MyOS currently exposes ATA in read-only mode. Returns 1 on success. */
int ata_read_sector(unsigned int lba, unsigned char* buffer);

#endif
