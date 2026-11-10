#include "pic.h"
#include "io.h"

#define PIC1_START_INTERRUPT 0x20
#define PIC2_START_INTERRUPT 0x28

void pic_remap(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, PIC1_START_INTERRUPT);
    outb(0xA1, PIC2_START_INTERRUPT);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    // Маскируем прерывания.
    // 0xF8 = 11111000. Разрешаем IRQ0 (таймер), IRQ1 (клава) и IRQ2 (каскад для slave PIC)
    outb(0x21, 0xF8); 
    
    // 0xEF = 11101111. Разрешаем IRQ12 (мышь). Остальные на slave-контроллере маскируем.
    outb(0xA1, 0xEF); 
}

void pic_send_eoi(unsigned char irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}