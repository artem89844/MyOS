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

struct idt_entry idt[256];
struct idt_ptr idtp;

// Внешние функции из isr.asm
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

void idt_set_gate(unsigned char num, unsigned long base, unsigned short sel, unsigned char flags) {
    idt[num].base_lo = (base & 0xFFFF);
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

static inline void idt_load() {
    __asm__ volatile ("lidt (%0)" : : "r"(&idtp));
}

void idt_init(void) {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (unsigned int)&idt;

    for(int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    // Селектор 0x10 (Code Segment у GRUB)
    idt_set_gate(0, (unsigned long)isr0, 0x10, 0x8E);
    idt_set_gate(1, (unsigned long)isr1, 0x10, 0x8E);
    idt_set_gate(2, (unsigned long)isr2, 0x10, 0x8E);
    idt_set_gate(3, (unsigned long)isr3, 0x10, 0x8E);
    idt_set_gate(4, (unsigned long)isr4, 0x10, 0x8E);
    idt_set_gate(5, (unsigned long)isr5, 0x10, 0x8E);
    idt_set_gate(6, (unsigned long)isr6, 0x10, 0x8E);
    idt_set_gate(7, (unsigned long)isr7, 0x10, 0x8E);
    idt_set_gate(8, (unsigned long)isr8, 0x10, 0x8E);
    idt_set_gate(9, (unsigned long)isr9, 0x10, 0x8E);
    idt_set_gate(10, (unsigned long)isr10, 0x10, 0x8E);
    idt_set_gate(11, (unsigned long)isr11, 0x10, 0x8E);
    idt_set_gate(12, (unsigned long)isr12, 0x10, 0x8E);
    idt_set_gate(13, (unsigned long)isr13, 0x10, 0x8E);
    idt_set_gate(14, (unsigned long)isr14, 0x10, 0x8E);
    idt_set_gate(15, (unsigned long)isr15, 0x10, 0x8E);
    idt_set_gate(16, (unsigned long)isr16, 0x10, 0x8E);
    idt_set_gate(17, (unsigned long)isr17, 0x10, 0x8E);
    idt_set_gate(18, (unsigned long)isr18, 0x10, 0x8E);
    idt_set_gate(19, (unsigned long)isr19, 0x10, 0x8E);
    idt_set_gate(20, (unsigned long)isr20, 0x10, 0x8E);
    idt_set_gate(21, (unsigned long)isr21, 0x10, 0x8E);
    idt_set_gate(22, (unsigned long)isr22, 0x10, 0x8E);
    idt_set_gate(23, (unsigned long)isr23, 0x10, 0x8E);
    idt_set_gate(24, (unsigned long)isr24, 0x10, 0x8E);
    idt_set_gate(25, (unsigned long)isr25, 0x10, 0x8E);
    idt_set_gate(26, (unsigned long)isr26, 0x10, 0x8E);
    idt_set_gate(27, (unsigned long)isr27, 0x10, 0x8E);
    idt_set_gate(28, (unsigned long)isr28, 0x10, 0x8E);
    idt_set_gate(29, (unsigned long)isr29, 0x10, 0x8E);
    idt_set_gate(30, (unsigned long)isr30, 0x10, 0x8E);
    idt_set_gate(31, (unsigned long)isr31, 0x10, 0x8E);

    idt_set_gate(32, (unsigned long)irq0, 0x10, 0x8E);
    idt_set_gate(33, (unsigned long)irq1, 0x10, 0x8E);
    idt_set_gate(44, (unsigned long)irq12, 0x10, 0x8E);

    idt_load();
}