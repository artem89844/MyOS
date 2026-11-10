#include "ata.h"
#include "io.h"

static inline void outw(unsigned short port, unsigned short val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned short inw(unsigned short port) {
    unsigned short ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void ata_wait() {
    for (int i = 0; i < 400000; i++) {
        if ((inb(0x1F7) & 0x80) == 0) return;
    }
}

void ata_read_sector(unsigned int lba, unsigned char* buffer) {
    ata_wait();
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1); 
    outb(0x1F3, (unsigned char)lba);
    outb(0x1F4, (unsigned char)(lba >> 8));
    outb(0x1F5, (unsigned char)(lba >> 16));
    outb(0x1F7, 0x20);

    for (int i = 0; i < 400000; i++) {
        unsigned char status = inb(0x1F7);
        if (status & 0x08) break; 
    }

    unsigned short* ptr = (unsigned short*)buffer;
    for (int i = 0; i < 256; i++) {
        ptr[i] = inw(0x1F0);
    }
}

void ata_write_sector(unsigned int lba, unsigned char* buffer) {
    ata_wait();
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1); 
    outb(0x1F3, (unsigned char)lba);
    outb(0x1F4, (unsigned char)(lba >> 8));
    outb(0x1F5, (unsigned char)(lba >> 16));
    outb(0x1F7, 0x30); // WRITE SECTORS

    for (int i = 0; i < 400000; i++) {
        unsigned char status = inb(0x1F7);
        if (status & 0x08) break; 
    }

    unsigned short* ptr = (unsigned short*)buffer;
    for (int i = 0; i < 256; i++) {
        outw(0x1F0, ptr[i]);
    }
}