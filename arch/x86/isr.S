/* GNU assembler equivalent of isr.asm, used by the included Makefile. */
.code32
.extern isr_handler
.extern irq_handler

.macro ISR_NOERR n
.globl isr\n
isr\n:
    cli
    pushl $0
    pushl $\n
    jmp isr_common_stub
.endm

.macro ISR_ERR n
.globl isr\n
isr\n:
    cli
    pushl $\n
    jmp isr_common_stub
.endm

.macro IRQ_STUB name, vector
.globl \name
\name:
    cli
    pushl $0
    pushl $\vector
    jmp irq_common_stub
.endm

IRQ_STUB irq0, 32
IRQ_STUB irq1, 33
IRQ_STUB irq12, 44

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

isr_common_stub:
    pusha
    movw %ds, %ax
    pushl %eax
    movw %es, %ax
    pushl %eax
    movw %fs, %ax
    pushl %eax
    movw %gs, %ax
    pushl %eax

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    pushl %esp
    call isr_handler
    addl $4, %esp

    popl %eax
    movw %ax, %gs
    popl %eax
    movw %ax, %fs
    popl %eax
    movw %ax, %es
    popl %eax
    movw %ax, %ds
    popa
    addl $8, %esp
    iret

irq_common_stub:
    pusha
    movw %ds, %ax
    pushl %eax
    movw %es, %ax
    pushl %eax
    movw %fs, %ax
    pushl %eax
    movw %gs, %ax
    pushl %eax

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    pushl %esp
    call irq_handler
    addl $4, %esp

    popl %eax
    movw %ax, %gs
    popl %eax
    movw %ax, %fs
    popl %eax
    movw %ax, %es
    popl %eax
    movw %ax, %ds
    popa
    addl $8, %esp
    iret

.section .note.GNU-stack,"",@progbits
