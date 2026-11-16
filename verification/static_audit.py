from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
boot = (root / 'arch' / 'x86' / 'boot.S').read_text()
gdt = (root / 'arch' / 'x86' / 'gdt.S').read_text()
idt = (root / 'arch' / 'x86' / 'idt.c').read_text()
isr = (root / 'arch' / 'x86' / 'isr.S').read_text()
heap = (root / 'mm' / 'heap.c').read_text()
ata_h = (root / 'drivers' / 'storage' / 'ata.h').read_text()
ata_c = (root / 'drivers' / 'storage' / 'ata.c').read_text()
kernel = (root / 'kernel' / 'kernel.c').read_text()

checks = {
    'boot calls dedicated GDT module': 'call gdt_install' in boot and 'lgdt gdt_descriptor' in gdt and 'gdt_descriptor:' in gdt,
    'GDT code selector is 0x08': 'ljmp $0x08' in gdt,
    'GDT data selector is 0x10': 'movw $0x10, %ax' in gdt,
    'IDT uses code selector 0x08': '#define KERNEL_CODE_SELECTOR 0x08' in idt,
    'ISR uses data selector 0x10': 'movw $0x10, %ax' in isr,
    'heap uses Multiboot memory map': 'find_mmap_heap_end' in heap and 'mmap_length' in heap,
    'heap has mem_upper fallback': 'mem_upper' in heap and '0x100000ULL' in heap,
    'ATA public interface is read-only': 'ata_read_sector' in ata_h and 'ata_write' not in ata_h.lower(),
    'ATA write implementation absent': 'ata_write_sector' not in ata_c,
}
irq_start = kernel.index('void irq_handler(')
irq_end = kernel.find('\n}\n', irq_start) + 3
irq_body = kernel[irq_start:irq_end]
checks['IRQ handler does not render'] = not any(x in irq_body for x in ('update_screen(', 'draw_rect(', 'draw_string(', 'wait_for_vsync('))
checks['main loop processes queued input'] = 'process_input_events();' in kernel
checks['main loop redraws outside IRQ'] = 'if (num_dirty_rects > 0) update_screen();' in kernel

for name, ok in checks.items():
    print(f"{'PASS' if ok else 'FAIL'}: {name}")
if not all(checks.values()):
    raise SystemExit(1)
