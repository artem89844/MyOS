#!/usr/bin/env python3
"""Small offline check for the Multiboot v1 header in a kernel ELF file."""
import pathlib
import struct
import sys

if len(sys.argv) != 2:
    raise SystemExit("Usage: python3 tools/check_multiboot.py kernel.bin")

path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
magic = 0x1BADB002
limit = min(len(data), 8192)
found = False
for offset in range(0, max(0, limit - 12) + 1, 4):
    m, flags, checksum = struct.unpack_from("<III", data, offset)
    if m == magic:
        found = True
        if (m + flags + checksum) & 0xFFFFFFFF:
            raise SystemExit(f"FAIL: bad Multiboot checksum at file offset 0x{offset:X}")
        if flags & 0x00000004:
            # Multiboot v1 has five fixed address-field slots at offsets
            # 12..31. They are ignored for ELF kernels when flag 16 is clear,
            # but the graphics fields still begin at offset 32.
            if offset + 48 > len(data):
                raise SystemExit("FAIL: incomplete Multiboot v1 video-mode request")
            mode_type, width, height, depth = struct.unpack_from("<IIII", data, offset + 32)
            if mode_type not in (0, 1) or depth not in (0, 15, 16, 24, 32):
                raise SystemExit(f"FAIL: invalid Multiboot video-mode request: mode_type={mode_type}, depth={depth}")
            print(f"PASS: Multiboot video request mode_type={mode_type}, width={width}, height={height}, depth={depth}")
        print(f"PASS: Multiboot v1 header found at 0x{offset:X}; flags=0x{flags:08X}")
        break
if not found:
    raise SystemExit("FAIL: Multiboot v1 header not found in first 8 KiB")

if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
    raise SystemExit("FAIL: expected a little-endian ELF32 executable")
print("PASS: little-endian ELF32 image")
