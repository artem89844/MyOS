#!/usr/bin/env python3
"""Static regression checks for the MyOS Setup integration."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
source = (root / "kernel" / "kernel.c").read_text(encoding="utf-8")

checks = {
    "Setup has five pages": "{\"Overview\", \"Processor\", \"Memory\", \"Boot\", \"About\"}" in source,
    "CPU information uses CPUID": "static void setup_collect_cpu_info(void)" in source and "run_cpuid(1, 0" in source,
    "CPU vendor and brand are collected": "setup_cpu.vendor" in source and "setup_cpu.brand" in source,
    "Memory information reads Multiboot map": "setup_collect_memory_info" in source and "setup_mmap_entries" in source,
    "Boot information reads Multiboot boot device": "setup_mbi" in source and "boot_device" in source,
    "F2 opens Setup during boot": "Press F2 for MyOS Setup" in source and "wait_boot_ticks(200u)" in source,
    "F2 opens Setup after boot": "if (sc == 0x3Cu)" in source and "open_setup_window();" in source,
    "Start Settings opens Setup": "open_setup_window();" in source and "y >= screen_height - 110" in source,
    "Setup supports mouse tab selection": "SETUP_PAGE_COUNT * 39" in source,
    "Setup supports keyboard navigation": "sc == 0x48u || sc == 0x50u" in source and "sc == 0x01u" in source,
    "Architecture build version is displayed": "MYOS_KERNEL_VERSION" in source and "Kernel Architecture 2.0" in source,
    "Hardware tuning is not falsely exposed": "Firmware options are read-only here." in source and "does not tune it." in source,
}

failed = False
for name, passed in checks.items():
    print(f"{'PASS' if passed else 'FAIL'}: {name}")
    failed |= not passed
if failed:
    sys.exit(1)
