/* GNU assembler equivalent of boot.asm, used by the included Makefile. */
.section .multiboot,"a"
.balign 4
.long 0x1BADB002
.long 0x00000007
.long -(0x1BADB002 + 0x00000007)
/* Multiboot v1 reserves fixed slots for the five address fields.
   They are ignored unless header flag bit 16 is set, but the video
   request still begins at byte offset 32 in the header. */
.long 0                         /* header_addr (unused: ELF kernel) */
.long 0                         /* load_addr (unused: ELF kernel) */
.long 0                         /* load_end_addr (unused: ELF kernel) */
.long 0                         /* bss_end_addr (unused: ELF kernel) */
.long 0                         /* entry_addr (unused: ELF kernel) */
.long 0                         /* mode_type: linear graphics */
.long 0                         /* any width */
.long 0                         /* any height */
.long 32                        /* 32 bpp */

.section .bss
.balign 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.code32
.globl _start
.type _start, @function
.extern kernel_main
_start:
    cli
    cld
    movl $stack_top, %esp

    call gdt_install
    movl $stack_top, %esp

    movl %ebx, %esi
    andl $-16, %esp
    subl $12, %esp
    pushl %esi
    call kernel_main
    addl $16, %esp
.Lhang:
    cli
    hlt
    jmp .Lhang
.size _start, .-_start

.extern gdt_install

.section .note.GNU-stack,"",@progbits
