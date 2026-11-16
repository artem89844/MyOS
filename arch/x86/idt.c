#include "idt.h"

struct idt_entry {
    unsigned short base_lo;
    unsigned short sel;
    unsigned char always0;
    unsigned char flags;
    unsigned short base_hi;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

extern void isr0(); extern void isr1(); extern void isr2(); extern void isr3();
extern void isr4(); extern void isr5(); extern void isr6(); extern void isr7();
extern void isr8(); extern void isr9(); extern void isr10(); extern void isr11();
extern void isr12(); extern void isr13(); extern void isr14(); extern void isr15();
extern void isr16(); extern void isr17(); extern void isr18(); extern void isr19();
extern void isr20(); extern void isr21(); extern void isr22(); extern void isr23();
extern void isr24(); extern void isr25(); extern void isr26(); extern void isr27();
extern void isr28(); extern void isr29(); extern void isr30(); extern void isr31();
extern void irq0();
extern void irq1();
extern void irq12();

#define KERNEL_CODE_SELECTOR 0x08
#define IDT_INTERRUPT_GATE   0x8E

static void idt_set_gate(unsigned char num, unsigned int base) {
    idt[num].base_lo = (unsigned short)(base & 0xFFFF);
    idt[num].base_hi = (unsigned short)((base >> 16) & 0xFFFF);
    idt[num].sel = KERNEL_CODE_SELECTOR;
    idt[num].always0 = 0;
    idt[num].flags = IDT_INTERRUPT_GATE;
}

static inline void idt_load(void) {
    __asm__ volatile ("lidt (%0)" : : "r"(&idtp) : "memory");
}

void idt_init(void) {
    unsigned int i;
    idtp.limit = (unsigned short)(sizeof(idt) - 1);
    idtp.base = (unsigned int)&idt;

    for (i = 0; i < 256; ++i) {
        idt[i].base_lo = 0;
        idt[i].base_hi = 0;
        idt[i].sel = 0;
        idt[i].always0 = 0;
        idt[i].flags = 0;
    }

#define SET_ISR(n) idt_set_gate((n), (unsigned int)isr##n)
    SET_ISR(0);  SET_ISR(1);  SET_ISR(2);  SET_ISR(3);
    SET_ISR(4);  SET_ISR(5);  SET_ISR(6);  SET_ISR(7);
    SET_ISR(8);  SET_ISR(9);  SET_ISR(10); SET_ISR(11);
    SET_ISR(12); SET_ISR(13); SET_ISR(14); SET_ISR(15);
    SET_ISR(16); SET_ISR(17); SET_ISR(18); SET_ISR(19);
    SET_ISR(20); SET_ISR(21); SET_ISR(22); SET_ISR(23);
    SET_ISR(24); SET_ISR(25); SET_ISR(26); SET_ISR(27);
    SET_ISR(28); SET_ISR(29); SET_ISR(30); SET_ISR(31);
#undef SET_ISR

    idt_set_gate(32, (unsigned int)irq0);
    idt_set_gate(33, (unsigned int)irq1);
    idt_set_gate(44, (unsigned int)irq12);
    idt_load();
}
