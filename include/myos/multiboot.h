#ifndef MYOS_MULTIBOOT_H
#define MYOS_MULTIBOOT_H

/* Multiboot v1 information structure passed by the bootloader. */
typedef struct {
    unsigned int flags; unsigned int mem_lower; unsigned int mem_upper;
    unsigned int boot_device; unsigned int cmdline; unsigned int mods_count;
    unsigned int mods_addr; unsigned int syms[4]; unsigned int mmap_length;
    unsigned int mmap_addr; unsigned int drives_length; unsigned int drives_addr;
    unsigned int config_table; unsigned int boot_loader_name; unsigned int apm_table;
    unsigned int vbe_control_info; unsigned int vbe_mode_info; unsigned short vbe_mode;
    unsigned short vbe_interface_seg; unsigned short vbe_interface_off;
    unsigned short vbe_interface_len; unsigned long long framebuffer_addr;
    unsigned int framebuffer_pitch; unsigned int framebuffer_width;
    unsigned int framebuffer_height; unsigned char framebuffer_bpp;
    unsigned char framebuffer_type;
} __attribute__((packed)) multiboot_info_t;

#endif /* MYOS_MULTIBOOT_H */
