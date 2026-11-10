bits 32

MAGIC    equ 0x1BADB002
FLAGS    equ 0x00000003
CHECKSUM equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

align 4096
pml4: resb 4096
pdpt: resb 4096
pd:   resb 4096

section .data
align 8
gdt64:
    dq 0
.code: equ $ - gdt64
    dq 0x00AF9A000000FFFF ; Стандартный 64-битный сегмент кода
.data: equ $ - gdt64
    dq 0x00CF92000000FFFF ; Стандартный сегмент данных
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .text
global _start
extern kernel_main_64
_start:
    cli 
    mov esp, stack_top

    ; Проверка Long Mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode

    ; Очистка таблиц страниц
    cld 
    mov edi, pml4
    mov ecx, 3 * 1024 
    xor eax, eax
    rep stosd

    ; Настройка PML4
    mov eax, pdpt
    or eax, 0b11
    mov [pml4], eax
    mov dword [pml4 + 4], 0

    ; Настройка PDPT
    mov eax, pd
    or eax, 0b11
    mov [pdpt], eax
    mov dword [pdpt + 4], 0

    ; Заполнение Page Directory
    mov ecx, 0
.fill_pd:
    mov eax, 0x200000
    mul ecx
    or eax, 0b10000011
    mov [pd + ecx * 8], eax
    mov dword [pd + ecx * 8 + 4], 0
    inc ecx
    cmp ecx, 512
    jne .fill_pd

    ; PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; PML4 в CR3
    mov eax, pml4
    mov cr3, eax

    ; Long Mode (EFER)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    xor edx, edx
    wrmsr

    ; Paging (CR0)
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    lgdt [gdt64.pointer]
    jmp gdt64.code:long_mode_start

.no_long_mode:
    mov dword [0xB8000], 0x4F4F4F4F
    hlt

bits 64
long_mode_start:
    mov ax, 0x10
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rax, stack_top
    mov rsp, rax
    and rsp, 0xFFFFFFFFFFFFFFF0

    call kernel_main_64
    
.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits