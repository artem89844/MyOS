#ifndef PIC_H
#define PIC_H

void pic_remap(void);
void pic_send_eoi(unsigned char irq);
void pit_init(unsigned int frequency_hz);

#endif
