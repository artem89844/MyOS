; MyOS 32-bit Multiboot v1 entry point.
; The kernel installs its own flat GDT before entering C code.
bits 32

MAGIC    equ 0x1BADB002
FLAGS    equ 0x00000007          ; page-align modules, memory info, video mode
CHECKSUM equ -(MAGIC + FLAGS)

section .multiboot
align 4
dd MAGIC
dd FLAGS
dd CHECKSUM
; Multiboot v1 keeps five address-field slots in the fixed header layout.
; They are ignored for this ELF kernel because flag bit 16 is not set.
dd 0                              ; header_addr (unused)
dd 0                              ; load_addr (unused)
dd 0                              ; load_end_addr (unused)
dd 0                              ; bss_end_addr (unused)
dd 0                              ; entry_addr (unused)
; Video-mode request starts at byte offset 32 in the Multiboot v1 header.
dd 0                              ; mode_type = linear graphics
dd 0                              ; width = any
dd 0                              ; height = any
dd 32                             ; bits per pixel

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kernel_main
extern gdt_install
_start:
    cli
    cld
    mov esp, stack_top

    ; GDT setup lives in a separate architecture module.
    call gdt_install
    mov esp, stack_top

    ; Preserve Multiboot info pointer and align the stack for GCC.
    mov esi, ebx
    and esp, 0xFFFFFFF0
    sub esp, 12
    push esi
    call kernel_main
    add esp, 16

.hang:
    cli
    hlt
    jmp .hang

