#ifndef MYOS_X86_GDT_H
#define MYOS_X86_GDT_H

/* Installs MyOS's flat ring-0 code/data descriptors and reloads segment regs. */
void gdt_install(void);

#endif /* MYOS_X86_GDT_H */
