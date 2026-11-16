#include "pic.h"
#include "io.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC1_START_INTERRUPT 0x20
#define PIC2_START_INTERRUPT 0x28
#define PIT_INPUT_HZ 1193182u

void pic_remap(void) {
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    outb(PIC1_DATA, PIC1_START_INTERRUPT);
    outb(PIC2_DATA, PIC2_START_INTERRUPT);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* Unmask IRQ0 (timer), IRQ1 (keyboard), cascade IRQ2 and IRQ12 (mouse). */
    outb(PIC1_DATA, 0xF8);
    outb(PIC2_DATA, 0xEF);
}

void pit_init(unsigned int frequency_hz) {
    unsigned int divisor;
    if (frequency_hz < 19u) frequency_hz = 19u;
    if (frequency_hz > 1000u) frequency_hz = 1000u;
    divisor = PIT_INPUT_HZ / frequency_hz;
    outb(0x43, 0x36); /* channel 0, lobyte/hibyte, mode 3 */
    outb(0x40, (unsigned char)(divisor & 0xFFu));
    outb(0x40, (unsigned char)((divisor >> 8) & 0xFFu));
}

void pic_send_eoi(unsigned char irq) {
    if (irq >= 8) outb(PIC2_COMMAND, 0x20);
    outb(PIC1_COMMAND, 0x20);
}
