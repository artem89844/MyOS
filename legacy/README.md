# Legacy images

`DO_NOT_BOOT-myos-original-before-fixes.iso` is retained only as a historical artifact from before the kernel fixes. It is not the current MyOS build. Create a new image from the current source with `make iso` after installing `grub-mkrescue` and `xorriso`.

The separate `myos64.iso` and `kernel64.bin` are an earlier experimental 64-bit path; the current default Makefile builds the 32-bit x86 kernel only.
