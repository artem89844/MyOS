#!/usr/bin/env python3
"""Static checks for the MyOS Kernel Architecture 2.0 source boundaries."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
checks = {
    "x86 boot entry is isolated": (root / "arch/x86/boot.S").is_file(),
    "GDT has its own assembly module": (root / "arch/x86/gdt.S").is_file(),
    "GDT is called from boot entry": "call gdt_install" in (root / "arch/x86/boot.S").read_text(),
    "IDT and PIC live under arch/x86": (root / "arch/x86/idt.c").is_file() and (root / "arch/x86/pic.c").is_file(),
    "legacy firmware access is isolated": (root / "arch/x86/firmware.c").is_file() and (root / "arch/x86/firmware.h").is_file(),
    "framebuffer is a separate driver": (root / "drivers/video/framebuffer.c").is_file() and (root / "drivers/video/framebuffer.h").is_file(),
    "storage driver has its own directory": (root / "drivers/storage/ata.c").is_file(),
    "memory manager has its own directory": (root / "mm/heap.c").is_file(),
    "freestanding library has its own directory": (root / "lib/string.c").is_file(),
    "Multiboot structure is shared via public header": "multiboot_info_t" in (root / "include/myos/multiboot.h").read_text(),
    "kernel public interface exists": (root / "include/myos/kernel.h").is_file(),
    "architecture documentation exists": (root / "ARCHITECTURE.md").is_file(),
    "kernel source uses module headers": '#include "framebuffer.h"' in (root / "kernel/kernel.c").read_text() and '#include "firmware.h"' in (root / "kernel/kernel.c").read_text(),
    "Makefile compiles separated modules": "drivers/video/framebuffer.c" in (root / "Makefile").read_text() and "arch/x86/gdt.S" in (root / "Makefile").read_text(),
    "old root-level monolithic source is removed": not (root / "kernel.c").exists(),
}
failed = False
for label, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {label}")
    failed |= not passed
if failed:
    sys.exit(1)
