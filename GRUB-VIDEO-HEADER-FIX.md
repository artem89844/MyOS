# MyOS — GRUB Multiboot video-header fix

## Symptom

GRUB displayed:

```text
error: unsupported graphical mode type 251662512.
error: you need to load the kernel first.
```

## Root cause

The Multiboot v1 header set flag bit 2 (video-mode request), but `boot.S` and `boot.asm` emitted the four graphics words immediately after the 12-byte magic/flags/checksum prefix.

The Multiboot v1 header has five fixed 32-bit address-field slots at offsets 12 through 31. They are unused for this ELF kernel because flag bit 16 is clear, but the video-mode request still begins at byte offset 32. GRUB therefore read bytes from the first machine instructions as `mode_type`; those bytes decoded to 251662512 (0x0F0010B0).

The follow-up message, `you need to load the kernel first`, was a consequence: the `multiboot` command failed, so the subsequent `boot` command had no successfully loaded kernel.

## Changes

- Added the five reserved address-field words to `boot.S` and `boot.asm`.
- Kept the requested video mode as linear framebuffer (mode_type 0), any width/height, 32 bpp.
- Updated `tools/check_multiboot.py` to read and validate the video fields at offset 32, and to require the full 48-byte Multiboot v1 header when the video flag is set.
- Rebuilt `kernel.bin` and `kernel.raw`; copied the new `kernel.bin` into `iso/boot/kernel.bin`.

## Verification performed

- `make -j2`: passed.
- `make check`: passed.
- ELF32 little-endian check: passed.
- Multiboot v1 magic and checksum: passed.
- Video request parsed at offset 32 as `(mode_type=0, width=0, height=0, depth=32)`: passed.

## Not verified in this environment

A fresh ISO and a QEMU boot test were not produced because `grub-mkrescue`, `xorriso`, and `qemu-system-i386` are not installed in this environment. No old ISO is presented as a new build.
