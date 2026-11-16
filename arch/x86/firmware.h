#ifndef MYOS_X86_FIRMWARE_H
#define MYOS_X86_FIRMWARE_H

#define BDA_BASE 0x400u
#define BDA_EQUIPMENT 0x10u
#define BDA_BASE_MEM 0x13u
#define BDA_SHIFT_FLAGS 0x17u
#define BDA_VIDEO_MODE 0x49u
#define BDA_VIDEO_COLS 0x4Au
#define BDA_TIMER_TICKS 0x6Cu

#define BDA_FLAG_RSHIFT 0x01u
#define BDA_FLAG_LSHIFT 0x02u
#define BDA_FLAG_CTRL 0x04u
#define BDA_FLAG_ALT 0x08u
#define BDA_FLAG_SCROLL 0x10u
#define BDA_FLAG_NUM 0x20u
#define BDA_FLAG_CAPS 0x40u

unsigned char bda_byte(unsigned int off);
unsigned short bda_word(unsigned int off);
unsigned int bda_dword(unsigned int off);
volatile unsigned char* bda_shift(void);
volatile unsigned int* bda_ticks(void);
unsigned short ebda_segment(void);
unsigned int ebda_phys(void);
unsigned short ebda_size_kb(void);
const char* cmos_floppy_name(unsigned char type);
unsigned char read_cmos(unsigned char reg);
int bcd_to_bin(unsigned char value);

#endif /* MYOS_X86_FIRMWARE_H */
