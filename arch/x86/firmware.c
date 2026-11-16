#include "firmware.h"
#include "io.h"

// ==========================================
// 2. BIOS DATA AREA (BDA) — 0x400-0x4FF
// ==========================================

unsigned char  bda_byte(unsigned int off) { return *(volatile unsigned char*)(BDA_BASE + off); }
unsigned short bda_word(unsigned int off) { return *(volatile unsigned short*)(BDA_BASE + off); }
unsigned int   bda_dword(unsigned int off){ return *(volatile unsigned int*)(BDA_BASE + off); }
volatile unsigned char* bda_shift() { return (volatile unsigned char*)(BDA_BASE + BDA_SHIFT_FLAGS); }
volatile unsigned int*  bda_ticks() { return (volatile unsigned int*)(BDA_BASE + BDA_TIMER_TICKS); }

// ==========================================
// 2b. EBDA
// ==========================================
unsigned short ebda_segment()  { return *(volatile unsigned short*)0x40E; }
unsigned int   ebda_phys()     { return (unsigned int)ebda_segment() << 4; }
unsigned short ebda_size_kb()  {
    unsigned int p = ebda_phys();
    if (p == 0) return 0;
    return *(volatile unsigned short*)p;
}

// ==========================================
// 2c. CMOS
// ==========================================
const char* cmos_floppy_name(unsigned char t) {
    switch(t) {
        case 0x0: return "none";
        case 0x1: return "360K";
        case 0x2: return "1.2M";
        case 0x3: return "720K";
        case 0x4: return "1.44M";
        case 0x5: return "2.88M";
        default:  return "?";
    }
}

unsigned char read_cmos(unsigned char r) { outb(0x70, r); return inb(0x71); }
int bcd_to_bin(unsigned char b) { return (b & 0x0F) + ((b / 16) * 10); }

