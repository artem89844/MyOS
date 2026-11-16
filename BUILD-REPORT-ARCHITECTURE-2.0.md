# MyOS Kernel Architecture 2.0 — Build report

Date: 2026-11-16
Source baseline: `MyOS-Setup-alpha.zip`
Release label: `0.3.0-arch-alpha` (32-bit x86)

## Implemented in this phase

- Moved the 32-bit Multiboot entry, interrupt stubs, IDT, PIC/PIT and port I/O into `arch/x86/`.
- Extracted the GDT into a dedicated GNU Assembly module `arch/x86/gdt.S`; the boot entry now calls `gdt_install`.
- Added a NASM counterpart `arch/x86/gdt.asm` and updated the NASM boot reference to use it.
- Moved BIOS Data Area, EBDA and CMOS accessors into `arch/x86/firmware.c` with a public `firmware.h` contract.
- Extracted framebuffer initialisation, dirty-rectangle state, drawing primitives and the bitmap font into `drivers/video/framebuffer.c` / `framebuffer.h`.
- Moved the early heap to `mm/`, read-only ATA PIO to `drivers/storage/`, and freestanding string/memory routines to `lib/`.
- Centralised the Multiboot v1 structure and public kernel version/interface headers under `include/myos/`.
- Rewrote the Makefile for the directory tree, added automatic header dependencies (`-MMD -MP`), and made implicit function declarations a compile error.
- Updated Setup's About page to show `0.3.0-arch-alpha` and `Kernel Architecture 2.0`.
- Preserved the older 32-bit pre-fix ISO under `legacy/` and marked it as not for booting.

## Build verification

Commands executed:

```sh
make clean
make -j2
make check
```

Results:

- Clean 32-bit build completed successfully with no compiler warnings.
- `kernel.bin` is a statically linked little-endian ELF32 executable for Intel 80386.
- Entry point: `0x00100030`.
- Multiboot v1 header found at file offset `0x1000`, flags `0x00000007`, checksum valid.
- Video request fields validated at Multiboot offset 32: `mode_type=0`, width/height 0 (any), depth 32.
- `nm -u kernel.bin` reported no undefined symbols.
- Existing static kernel regression checks: 12 passed.
- MyOS Setup integration checks: 12 passed.
- Architecture boundary checks: 15 passed.
- Multiboot/ELF checks: 3 passed.
- Total static checks: 42 passed.

Fresh artifacts:

- `kernel.bin`: 40,332 bytes.
- `kernel.raw`: 31,136 bytes.
- `iso/boot/kernel.bin` and `iso/boot/kernel.raw` were refreshed from this build.

## Not verified in this environment

`qemu-system-i386`, `grub-mkrescue`, `xorriso`, and NASM are not installed. Therefore a new ISO was not generated and no runtime boot/desktop test is claimed. The included GRUB ISO recipe remains `make iso` once `grub-mkrescue` and `xorriso` are installed.

## Known next refactor

The current desktop/window subsystem, keyboard/mouse event processing, Setup UI and main event loop remain integrated in `kernel/kernel.c`. The next phase should split these into `kernel/`, `drivers/input/` and `ui/` modules behind explicit headers. This phase does not add paging, a scheduler, userspace, syscalls, VFS, or multiprocess isolation.
