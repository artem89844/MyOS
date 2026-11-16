# MyOS Kernel Architecture 2.0 — Phase 1

This revision introduces explicit subsystem boundaries while preserving the current 32-bit desktop behaviour. It is an incremental refactor, not a claim that the kernel is already microkernel-based or fully process-isolated.

## Source layout

```text
MyOS/
├── arch/x86/          # Multiboot entry, GDT, IDT, ISR, PIC, port I/O, legacy firmware data
├── drivers/video/     # Linear-framebuffer drawing backend and 8x16 font
├── drivers/storage/   # Read-only ATA PIO sector reader
├── mm/                # Multiboot-aware physical heap allocator (pre-paging)
├── lib/               # Freestanding memory/string primitives
├── kernel/            # Kernel lifecycle, event loop, current desktop/window integration
├── include/myos/      # Shared kernel interfaces and bootloader data structures
├── tools/             # Build and binary validation scripts
└── verification/      # Static regression checks
```

## Subsystem responsibilities

- **arch/x86** owns CPU entry, descriptor tables, interrupt entry/dispatch plumbing, PIC/PIT, port I/O, and Legacy BIOS data reads.
- **mm** owns early physical heap initialisation and allocation. It uses the Multiboot memory map and is intentionally an early-boot allocator, not yet a paged virtual-memory manager.
- **drivers/video** owns framebuffer setup, drawing primitives, text rendering, dirty-rectangle state, and bounded retrace polling.
- **drivers/storage** exposes read-only ATA sector access. There is no write API.
- **lib** provides freestanding memory/string routines.
- **kernel** owns boot orchestration, the desktop integration, the input event queue, and the main event loop.
- **include/myos** contains shared ABI structures and version constants.

## Boot flow

1. GRUB loads the ELF32 Multiboot v1 kernel and passes the Multiboot information pointer in EBX.
2. `arch/x86/boot.S` establishes a stack and calls `gdt_install`.
3. The GDT module installs flat ring-0 selectors (`0x08` code, `0x10` data).
4. `kernel_main` validates boot memory, initialises the early heap and framebuffer, then collects Setup information.
5. PIC/PIT/IDT are installed and the interrupt flag is enabled.
6. The boot splash can be interrupted with F2; otherwise the desktop starts.
7. IRQ handlers enqueue keyboard/mouse input; the kernel main loop consumes events and redraws outside interrupt context.

## Interface rules

- Drivers expose narrow headers; callers should not duplicate port-I/O or hardware structure definitions.
- The framebuffer API is independent of window-management code.
- ATA is read-only until a partition-aware block layer and isolated test-image workflow exist.
- The heap is explicitly an early physical allocator. A later VMM/page allocator must not silently reuse this API as if it provided process isolation.
- New code should avoid adding unrelated features to `kernel/kernel.c`. The next refactor phase should extract input/event processing and UI/window management into their own modules.

## Current limitations

The desktop/window code and input event loop are still integrated in `kernel/kernel.c`; extracting them is the next phase. The kernel is still single-address-space and single-task. There is no scheduler, userspace, syscall ABI, VFS, paging-based protection, or UEFI boot path yet.
