CC      ?= gcc
LD      ?= ld
OBJCOPY ?= objcopy
PYTHON  ?= python3

BUILD_DIR := build
MYOS_VERSION := 0.3.0-arch-alpha
CPPFLAGS := -Iinclude -Iarch/x86 -Imm -Idrivers/video -Idrivers/storage -Ilib
C_SOURCES := \
    kernel/kernel.c \
    drivers/video/framebuffer.c \
    arch/x86/firmware.c \
    arch/x86/idt.c \
    arch/x86/pic.c \
    mm/heap.c \
    drivers/storage/ata.c \
    lib/string.c
ASM_SOURCES := arch/x86/boot.S arch/x86/gdt.S arch/x86/isr.S
C_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
ASM_OBJECTS := $(patsubst %.S,$(BUILD_DIR)/%.o,$(ASM_SOURCES))
OBJECTS := $(ASM_OBJECTS) $(C_OBJECTS)
DEPS := $(C_OBJECTS:.o=.d)

CFLAGS := $(CPPFLAGS) -m32 -std=gnu99 -O2 -Wall -Wextra -Wno-array-bounds \
          -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
          -fno-common -fno-asynchronous-unwind-tables -fno-unwind-tables \
          -MMD -MP -Werror=implicit-function-declaration \
          -mno-mmx -mno-sse -mpreferred-stack-boundary=2 \
          -mincoming-stack-boundary=2
ASFLAGS := $(CPPFLAGS) -m32 -ffreestanding -fno-pic -fno-pie
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib

.PHONY: all clean check iso print-config
all: kernel.bin kernel.raw

-include $(DEPS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(ASFLAGS) -c $< -o $@

kernel.bin: $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

kernel.raw: kernel.bin
	$(OBJCOPY) -O binary $< $@

check: all
	$(PYTHON) tools/check_multiboot.py kernel.bin
	$(PYTHON) verification/static_audit.py
	$(PYTHON) tools/check_myos_setup.py
	$(PYTHON) tools/check_architecture.py

iso: kernel.bin
	@command -v grub-mkrescue >/dev/null 2>&1 || { echo "ERROR: grub-mkrescue not found"; exit 1; }
	@command -v xorriso >/dev/null 2>&1 || { echo "ERROR: xorriso not found"; exit 1; }
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/kernel.bin
	grub-mkrescue -o myos-new.iso iso

print-config:
	@echo "MyOS $(MYOS_VERSION)"
	@echo "Sources: $(words $(C_SOURCES)) C, $(words $(ASM_SOURCES)) Assembly"

clean:
	rm -rf $(BUILD_DIR) kernel.bin kernel.raw myos-new.iso
