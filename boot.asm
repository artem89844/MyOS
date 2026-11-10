MAGIC    equ 0x1BADB002
FLAGS    equ 0x00000007          ; PAGE_ALIGN (1) + MEMINFO (2) + VIDEO_MODE (4)
CHECKSUM equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

    ; --- aout kludge (обязательные 5 полей, даже если не используются) ---
    dd 0        ; header_addr
    dd 0        ; load_addr
    dd 0        ; load_end_addr
    dd 0        ; bss_end_addr
    dd 0        ; entry_addr

    ; --- Параметры видеорежима ---
    dd 0        ; mode_type = 0 (линейный графический режим)
    dd 0        ; width (0 = пусть GRUB выберет лучшее)
    dd 0        ; height (0 = пусть GRUB выберет лучшее)
    dd 32       ; depth (бит на пиксель)

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

extern kernel_main

section .text
global _start
_start:
    mov esp, stack_top
    push ebx
    call kernel_main
    cli
.hang:
    hlt
    jmp .hang