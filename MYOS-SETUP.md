# MyOS Setup

## Opening Setup

1. **During boot:** press `F2` while the MyOS boot splash displays `Press F2 for MyOS Setup`.
2. **From desktop:** click `Start` → `Settings`.

## Navigation

- Mouse: click Overview, Processor, Memory, Boot, or About in the left navigation.
- Keyboard: `Up` / `Down` changes pages.
- `Esc` closes the Setup window.
- The normal window controls can minimize, maximize, restore, and close Setup.

## Information displayed

- **Overview:** x86 architecture, Multiboot protocol, bootloader name, detected RAM, and CPU brand.
- **Processor:** CPUID vendor/brand, family, model, stepping, and supported feature bits.
- **Memory:** total usable memory reported by the Multiboot memory map (limited to the 32-bit address space), kernel heap remaining, map entry count, and BIOS base-memory value.
- **Boot:** loader name, protocol, and boot-device metadata when supplied by GRUB.
- **About:** build and architecture information.

## Hardware configuration limits

This first version is intentionally read-only. A kernel loaded by GRUB cannot generally set motherboard firmware options such as CPU multiplier/voltage, memory timings, or the firmware's boot-device order. Those are controlled by the platform BIOS/UEFI before GRUB starts. MyOS also does not yet have a filesystem or persistent settings store, so it does not claim to save firmware or boot settings.

## Build and checks

```sh
make clean
make -j2
make check
```

A successful `make check` validates the Multiboot header/ELF32 image, runs the existing static kernel audit, and checks that the Setup entry points and navigation are present. It does not replace a runtime test in QEMU or on hardware.
