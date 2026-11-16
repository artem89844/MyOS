/* Global Descriptor Table is isolated from the Multiboot entry stub. */
.code32
.section .text
.globl gdt_install
.type gdt_install, @function
gdt_install:
    lgdt gdt_descriptor
    ljmp $0x08, $.Lreload_segments
.Lreload_segments:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ret
.size gdt_install, .-gdt_install

.section .rodata
.balign 8
gdt_start:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_end:
gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start

.section .note.GNU-stack,"",@progbits
