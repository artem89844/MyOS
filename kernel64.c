static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

struct idt_entry {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char ist;
    unsigned char type_attr;
    unsigned short offset_mid;
    unsigned int offset_high;
    unsigned int zero;
} __attribute__((packed));

struct idt_ptr {
    unsigned short limit;
    unsigned long long base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

__attribute__((naked)) void isr_default() {
    __asm__ volatile(
        "cli\n"
        "hlt\n"
        "jmp isr_default"
    );
}

void setup_idt() {
    unsigned long long base = (unsigned long long)isr_default;
    for (int i = 0; i < 256; i++) {
        idt[i].offset_low = base & 0xFFFF;
        idt[i].selector = 0x08; // Код
        idt[i].ist = 0;
        idt[i].type_attr = 0x8E;
        idt[i].offset_mid = (base >> 16) & 0xFFFF;
        idt[i].offset_high = (base >> 32) & 0xFFFFFFFF;
        idt[i].zero = 0;
    }
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (unsigned long long)idt;
    __asm__ volatile("lidt (%0)" : : "r"(&idtp));
}

void kernel_main_64() {
    setup_idt();

    outb(0x21, 0xFF); 
    outb(0xA1, 0xFF);
    outb(0x70, inb(0x70) | 0x80);

    volatile char *vga = (char *)0xB8000;
    const char *msg = "Hello from 64-bit MyOS! System is stable.";
    
    for (int i = 0; msg[i]; i++) {
        vga[i * 2]     = msg[i];
        vga[i * 2 + 1] = 0x0F; 
    }
    
    // Вечный цикл БЕЗ hlt
    while (1) {
        __asm__ volatile("pause");
    }
}