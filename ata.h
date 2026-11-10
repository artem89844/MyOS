#ifndef ATA_H
#define ATA_H

void ata_read_sector(unsigned int lba, unsigned char* buffer);
void ata_write_sector(unsigned int lba, unsigned char* buffer);

#endif