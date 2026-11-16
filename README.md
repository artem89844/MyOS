# MyOS — 32-bit x86 operating system

MyOS is an experimental freestanding x86 operating system using a GRUB/Multiboot v1 boot path, a C/Assembly kernel, and a linear-framebuffer desktop.

## Kernel Architecture 2.0 — Phase 1

The source tree now has explicit subsystem boundaries:

```text
MyOS/
├── arch/x86/          # Boot entry, GDT, IDT, ISR, PIC/PIT, port I/O, Legacy firmware data
├── drivers/video/     # Framebuffer backend, drawing primitives, font
├── drivers/storage/   # Read-only ATA PIO driver
├── mm/                # Early Multiboot-aware heap allocator
├── lib/               # Freestanding string/memory routines
├── kernel/            # Boot flow, event loop, desktop and window integration
├── include/myos/      # Shared Multiboot structure and kernel interface
├── tools/             # Binary and architecture checks
└── verification/      # Static regression audits
```

### Module ownership

- `arch/x86`: machine-specific entry and interrupt plumbing, GDT/IDT/PIC/PIT, I/O ports, and Legacy BIOS Data Area/CMOS reads.
- `mm`: early physical heap allocation from the Multiboot memory map. This is not yet a virtual-memory manager.
- `drivers/video`: framebuffer setup, drawing, text, dirty rectangles, and bounded retrace polling.
- `drivers/storage`: read-only ATA sector reads; no disk-write API is exposed.
- `lib`: freestanding C string and memory functions.
- `kernel`: kernel lifecycle, boot UI, event queue, windowing and desktop integration.
- `include/myos`: shared kernel/bootloader interface definitions.

The desktop and event-loop integration still resides in `kernel/kernel.c`. The next architecture phase should separate keyboard/mouse event handling and the window manager into independent modules. This release is an incremental modularisation, not a microkernel and not a multi-process kernel.

## Build requirements

- GCC with 32-bit compilation support
- GNU binutils (`ld`, `objcopy`)
- Python 3
- For ISO creation: `grub-mkrescue` and `xorriso`
- For runtime testing: `qemu-system-i386` or Bochs

Build and run all static checks:

```sh
make clean
make -j2
make check
```

Outputs:

- `kernel.bin` — ELF32 Multiboot v1 kernel for GRUB.
- `kernel.raw` — flat binary generated from the ELF (not the GRUB ELF image).

Create a fresh ISO:

```sh
make iso
```

This copies the freshly built ELF into `iso/boot/kernel.bin` and builds `myos-new.iso` using the GRUB configuration in `iso/boot/grub/grub.cfg`.

Run in an isolated emulator (do not attach host physical disks):

```sh
qemu-system-i386 -m 256M -cdrom myos-new.iso -boot d -no-reboot -no-shutdown
```

## Current status and limitations

- Architecture: 32-bit x86 (i386-compatible build).
- Boot protocol: GRUB + Multiboot v1.
- Kernel implementation: C and GNU Assembly.
- Early heap uses the Multiboot memory map; paging and process address-space isolation are not implemented.
- Interrupt handlers enqueue keyboard/mouse input; UI drawing occurs in the main loop.
- ATA access is read-only. Do not add write tests against host disks.
- MyOS Setup is informational. It does not modify motherboard firmware settings, CPU voltage/frequency, RAM timings, or firmware boot order.
- There is no scheduler, userspace, syscall ABI, VFS/filesystem, USB stack, network stack, or UEFI boot path yet.
- Static build checks do not replace a QEMU/Bochs boot test. No runtime boot result is claimed by `make check`.

See [ARCHITECTURE.md](ARCHITECTURE.md) for subsystem responsibilities and the boot flow.
