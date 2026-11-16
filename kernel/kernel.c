#include "io.h"
#include "firmware.h"
#include "framebuffer.h"
#include "myos/multiboot.h"
#include "myos/kernel.h"
#include "pic.h"
#include "idt.h"
#include "heap.h"
#include "string.h"
#include "ata.h"

// ==========================================
// Forward declarations
// ==========================================

typedef struct { 
    int x, y, w, h; 
    int visible; 
    int minimized;
    int is_maximized;
    int old_x, old_y, old_w, old_h;
    const char* title; 
} window_t;
extern window_t my_window;
extern int dragging;

#define WINDOW_APP_SYSTEM_INFO 0
#define WINDOW_APP_MYOS_SETUP  1
#define SETUP_PAGE_COUNT       5

static int window_app = WINDOW_APP_SYSTEM_INFO;
static int setup_page = 0;
static int boot_setup_requested = 0;
static void* setup_mbi = 0;
static unsigned int setup_ram_mb = 0;
static unsigned int setup_mmap_entries = 0;
static char setup_loader_name[40] = "Unknown";

static struct {
    int available;
    unsigned int max_basic;
    unsigned int max_extended;
    unsigned int family;
    unsigned int model;
    unsigned int stepping;
    unsigned int features_edx;
    unsigned int features_ecx;
    char vendor[13];
    char brand[49];
} setup_cpu;
static void open_setup_window(void);

// ==========================================
// 1. MULTIBOOT
// ==========================================


// ==========================================
// 5. RTC
// ==========================================
void draw_clock() {
    unsigned int wait;
    int s, m, h;
    char t[9];
    for (wait = 0; wait < 100000u; ++wait) {
        if (!(read_cmos(0x0A) & 0x80)) break;
    }
    if (wait == 100000u) {
        draw_round_rect(screen_width - 90, screen_height - 35, 85, 30, 8, 0xBBBBBB);
        draw_string(screen_width - 85, screen_height - 28, "--:--:--", 0x000000);
        return;
    }
    s = bcd_to_bin(read_cmos(0x00));
    m = bcd_to_bin(read_cmos(0x02));
    h = bcd_to_bin(read_cmos(0x04));
    t[0]=(h/10)+'0'; t[1]=(h%10)+'0'; t[2]=':';
    t[3]=(m/10)+'0'; t[4]=(m%10)+'0'; t[5]=':';
    t[6]=(s/10)+'0'; t[7]=(s%10)+'0'; t[8]='\0';
    draw_round_rect(screen_width - 90, screen_height - 35, 85, 30, 8, 0xBBBBBB);
    draw_string(screen_width - 85, screen_height - 28, t, 0x000000);
}

// ==========================================
// 6. КЛАВИАТУРА
// ==========================================
#define SC_LSHIFT_PRESS  0x2A
#define SC_LSHIFT_REL    0xAA
#define SC_RSHIFT_PRESS  0x36
#define SC_RSHIFT_REL    0xB6
#define SC_LCTRL_PRESS   0x1D
#define SC_LCTRL_REL     0x9D
#define SC_LALT_PRESS    0x38
#define SC_LALT_REL      0xB8
#define SC_CAPSLOCK      0x3A
#define SC_NUMLOCK       0x45
#define SC_SCROLLLOCK    0x46

void kbd_send_byte(unsigned char b) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02)) break;
    }
    outb(0x60, b);
}

unsigned char kbd_wait_ack() {
    for (int i = 0; i < 100000; i++) {
        if (inb(0x64) & 0x01) return inb(0x60);
    }
    return 0;
}

void update_keyboard_leds() {
    unsigned char flags = *bda_shift();
    unsigned char leds = 0;
    if (flags & BDA_FLAG_SCROLL) leds |= 1;
    if (flags & BDA_FLAG_NUM)    leds |= 2;
    if (flags & BDA_FLAG_CAPS)   leds |= 4;

    kbd_send_byte(0xED);
    kbd_wait_ack();
    kbd_send_byte(leds);
    kbd_wait_ack();
}

void mark_window_dirty() {
    if (my_window.visible) {
        add_dirty_rect(my_window.x - 5, my_window.y - 5, my_window.w + 10, my_window.h + 10);
    }
}

void handle_keyboard_scancode(unsigned char sc) {
    volatile unsigned char* flags = bda_shift();
    int redraw = 0;
    int leds_changed = 0;

    if (!(sc & 0x80u)) {
        if (sc == 0x3Cu) { /* F2 opens MyOS Setup */
            boot_setup_requested = 1;
            open_setup_window();
            return;
        }
        if (window_app == WINDOW_APP_MYOS_SETUP && my_window.visible) {
            if (sc == 0x01u) { /* Escape closes Setup */
                my_window.visible = 0;
                my_window.minimized = 0;
                dragging = 0;
                add_dirty_rect(0, 0, screen_width, screen_height);
                return;
            }
            if (sc == 0x48u || sc == 0x50u) { /* Up / Down */
                if (sc == 0x48u) setup_page = (setup_page + SETUP_PAGE_COUNT - 1) % SETUP_PAGE_COUNT;
                else setup_page = (setup_page + 1) % SETUP_PAGE_COUNT;
                add_dirty_rect(my_window.x, my_window.y, my_window.w, my_window.h);
                return;
            }
        }
    }

    switch (sc) {
        case SC_LSHIFT_PRESS: *flags |= BDA_FLAG_LSHIFT; redraw = 1; break;
        case SC_LSHIFT_REL:   *flags &= ~BDA_FLAG_LSHIFT; redraw = 1; break;
        case SC_RSHIFT_PRESS: *flags |= BDA_FLAG_RSHIFT; redraw = 1; break;
        case SC_RSHIFT_REL:   *flags &= ~BDA_FLAG_RSHIFT; redraw = 1; break;
        case SC_LCTRL_PRESS:  *flags |= BDA_FLAG_CTRL; redraw = 1; break;
        case SC_LCTRL_REL:    *flags &= ~BDA_FLAG_CTRL; redraw = 1; break;
        case SC_LALT_PRESS:   *flags |= BDA_FLAG_ALT; redraw = 1; break;
        case SC_LALT_REL:     *flags &= ~BDA_FLAG_ALT; redraw = 1; break;
        case SC_CAPSLOCK:     *flags ^= BDA_FLAG_CAPS; leds_changed = 1; redraw = 1; break;
        case SC_NUMLOCK:      *flags ^= BDA_FLAG_NUM; leds_changed = 1; redraw = 1; break;
        case SC_SCROLLLOCK:   *flags ^= BDA_FLAG_SCROLL; leds_changed = 1; redraw = 1; break;
    }

    if (leds_changed) update_keyboard_leds();
    if (redraw) mark_window_dirty();
}

// ==========================================
// 7. МЫШЬ
// ==========================================
int mouse_x = 400, mouse_y = 300;
unsigned char mouse_cycle = 0;
signed char mouse_byte[3];
int left_click_prev = 0;
#define CURSOR_W 12
#define CURSOR_H 19
int current_hover = 0;

const char* cursor_map[CURSOR_H] = {
    "X           ", "XX          ", "XOX         ", "XOOX        ", "XOOOX       ",
    "XOOOOX      ", "XOOOOOX     ", "XOOOOOOX    ", "XOOOOOOOX   ", "XOOOOOOOOX  ",
    "XOOOOOOOOOX ", "XOOOOOOOOOOX", "XOOOOOOOXXXX", "XOOOXOOOX   ", "XOOXXOOOX   ",
    "XOX  XOOOX  ", "XX   XOOOX  ", "X     XOOX  ", "       XX   "
};

void mouse_init() {
    outb(0x64, 0xA8); outb(0x64, 0x20);
    unsigned char s = inb(0x60) | 2;
    outb(0x64, 0x60); outb(0x60, s);
    outb(0x64, 0xD4); outb(0x60, 0xF4); inb(0x60);
}

void draw_mouse(int x, int y) {
    for (int i = 0; i < CURSOR_H; i++)
        for (int j = 0; j < CURSOR_W; j++) {
            char p = cursor_map[i][j];
            if (p == 'X') put_pixel(x + j, y + i, 0x000000);
            else if (p == 'O') put_pixel(x + j, y + i, 0xFFFFFF);
        }
}

// ==========================================
// 8. ОКНО
// ==========================================
window_t my_window = { 
    .x = 0, .y = 0, .w = 460, .h = 400, 
    .visible = 0, 
    .minimized = 0, 
    .is_maximized = 0, 
    .old_x = 0, .old_y = 0, .old_w = 0, .old_h = 0, 
    .title = "System Information" 
};
unsigned char sector1_buffer[512];


int dragging = 0;
int drag_offset_x = 0, drag_offset_y = 0;
int start_menu_open = 0;

static int cpu_has_cpuid(void) {
    unsigned int before, after, toggled;
    __asm__ volatile ("pushfl; popl %0" : "=r"(before));
    toggled = before ^ (1u << 21);
    __asm__ volatile ("pushl %0; popfl" : : "r"(toggled) : "cc");
    __asm__ volatile ("pushfl; popl %0" : "=r"(after));
    __asm__ volatile ("pushl %0; popfl" : : "r"(before) : "cc");
    return ((before ^ after) & (1u << 21)) != 0;
}

static void run_cpuid(unsigned int leaf, unsigned int subleaf,
                      unsigned int* a, unsigned int* b,
                      unsigned int* c, unsigned int* d) {
    unsigned int ra, rb, rc, rd;
    __asm__ volatile ("cpuid"
        : "=a"(ra), "=b"(rb), "=c"(rc), "=d"(rd)
        : "a"(leaf), "c"(subleaf));
    if (a) *a = ra;
    if (b) *b = rb;
    if (c) *c = rc;
    if (d) *d = rd;
}

static void setup_copy_loader_name(const char* source) {
    unsigned int i = 0;
    if (!source) source = "Unknown";
    while (source[i] && i < sizeof(setup_loader_name) - 1u) {
        setup_loader_name[i] = source[i];
        ++i;
    }
    setup_loader_name[i] = '\0';
}

static void setup_collect_cpu_info(void) {
    unsigned int a, b, c, d;
    unsigned int max_ext;
    unsigned int base_family, ext_family, base_model, ext_model;

    setup_cpu.available = 0;
    setup_cpu.vendor[0] = '\0';
    setup_cpu.brand[0] = '\0';
    if (!cpu_has_cpuid()) return;

    run_cpuid(0, 0, &a, &b, &c, &d);
    setup_cpu.max_basic = a;
    {
        unsigned int vendor_words[3];
        vendor_words[0] = b;
        vendor_words[1] = d;
        vendor_words[2] = c;
        memcpy(setup_cpu.vendor, vendor_words, 12u);
    }
    setup_cpu.vendor[12] = '\0';

    run_cpuid(0x80000000u, 0, &a, &b, &c, &d);
    max_ext = a;
    setup_cpu.max_extended = max_ext;
    if (setup_cpu.max_basic >= 1u) {
        run_cpuid(1, 0, &a, &b, &c, &d);
        base_family = (a >> 8) & 0x0Fu;
        ext_family = (a >> 20) & 0xFFu;
        base_model = (a >> 4) & 0x0Fu;
        ext_model = (a >> 16) & 0x0Fu;
        setup_cpu.family = (base_family == 0x0Fu) ? base_family + ext_family : base_family;
        setup_cpu.model = ((base_family == 0x06u || base_family == 0x0Fu) ?
                           ((ext_model << 4) | base_model) : base_model);
        setup_cpu.stepping = a & 0x0Fu;
        setup_cpu.features_ecx = c;
        setup_cpu.features_edx = d;
    }

    if (max_ext >= 0x80000004u) {
        unsigned int words[12];
        run_cpuid(0x80000002u, 0, &words[0], &words[1], &words[2], &words[3]);
        run_cpuid(0x80000003u, 0, &words[4], &words[5], &words[6], &words[7]);
        run_cpuid(0x80000004u, 0, &words[8], &words[9], &words[10], &words[11]);
        memcpy(setup_cpu.brand, words, 48u);
        setup_cpu.brand[48] = '\0';
    } else {
        setup_cpu.brand[0] = '\0';
    }
    setup_cpu.available = 1;
}

static void setup_collect_memory_info(multiboot_info_t* mbi) {
    unsigned long long total = 0;
    unsigned int offset = 0;
    setup_ram_mb = 0;
    setup_mmap_entries = 0;
    setup_mbi = mbi;

    if (mbi && (mbi->flags & (1u << 6)) && mbi->mmap_addr && mbi->mmap_length >= 4u) {
        while (offset <= mbi->mmap_length - 4u) {
            const unsigned char* record = (const unsigned char*)(mbi->mmap_addr + offset);
            unsigned int payload_size = *(const unsigned int*)record;
            unsigned int record_size;
            unsigned long long addr, length, end, capped_end;
            unsigned int type;
            if (payload_size < 20u || payload_size > mbi->mmap_length - offset - 4u) break;
            record_size = payload_size + 4u;
            addr = *(const unsigned long long*)(record + 4u);
            length = *(const unsigned long long*)(record + 12u);
            type = *(const unsigned int*)(record + 20u);
            ++setup_mmap_entries;
            end = addr + length;
            if (type == 1u && end >= addr && addr < 0x100000000ULL) {
                capped_end = end > 0x100000000ULL ? 0x100000000ULL : end;
                if (capped_end > addr) total += capped_end - addr;
            }
            offset += record_size;
        }
    } else if (mbi && (mbi->flags & 1u)) {
        total = 0x100000ULL + (unsigned long long)mbi->mem_upper * 1024ULL;
    }
    setup_ram_mb = (unsigned int)(total / (1024ULL * 1024ULL));
}

static void setup_collect_boot_info(multiboot_info_t* mbi) {
    if (mbi && (mbi->flags & (1u << 9)) && mbi->boot_loader_name) {
        setup_copy_loader_name((const char*)mbi->boot_loader_name);
    } else {
        setup_copy_loader_name("Unknown bootloader");
    }
}

static void draw_hex_dword(int x, int y, unsigned int value, unsigned int color) {
    const char* hex = "0123456789ABCDEF";
    draw_char(x, y, '0', color);
    draw_char(x + 9, y, 'x', color);
    for (int i = 0; i < 8; ++i) {
        unsigned int shift = (7u - (unsigned int)i) * 4u;
        draw_char(x + 18 + i * 9, y, hex[(value >> shift) & 0x0Fu], color);
    }
}

static void setup_draw_row(int x, int y, const char* label,
                           const char* value, unsigned int value_color) {
    draw_string(x, y, label, 0x333F55);
    draw_string(x + 142, y, value, value_color);
}

static void setup_draw_brand_line(int x, int y, unsigned int start, unsigned int count) {
    char line[40];
    unsigned int i;
    if (count > 38u) count = 38u;
    for (i = 0; i < count; ++i) line[i] = setup_cpu.brand[start + i];
    line[count] = '\0';
    draw_string(x, y, line, 0x172B4D);
}

static void setup_draw_cpu_feature(int x, int y, const char* name,
                                   unsigned int bit, int from_ecx) {
    unsigned int flags = from_ecx ? setup_cpu.features_ecx : setup_cpu.features_edx;
    draw_string(x, y, name, 0x333F55);
    if (flags & (1u << bit)) draw_string(x + 52, y, "YES", 0x16804A);
    else draw_string(x + 52, y, "NO", 0xA04444);
}

static void draw_setup_content(window_t* win) {
    int body_x = win->x + 10;
    int body_y = win->y + 28;
    int nav_x = body_x + 2;
    int content_x = body_x + 142;
    int content_y = body_y + 16;
    int content_w = win->w - 166;
    int i;
    const char* tabs[SETUP_PAGE_COUNT] = {"Overview", "Processor", "Memory", "Boot", "About"};

    draw_rect(body_x, body_y, win->w - 20, win->h - 38, 0xE8EDF5);
    draw_rect(nav_x, body_y + 2, 126, win->h - 42, 0x172238);
    draw_string(nav_x + 14, body_y + 16, "MYOS", 0x62C9FF);
    draw_string(nav_x + 14, body_y + 34, "SETUP", 0xFFFFFF);
    draw_rect(nav_x + 12, body_y + 58, 100, 1, 0x46546B);

    for (i = 0; i < SETUP_PAGE_COUNT; ++i) {
        int ty = body_y + 72 + i * 39;
        if (setup_page == i) {
            draw_round_rect(nav_x + 7, ty - 5, 112, 30, 5, 0x1768A5);
        }
        draw_string(nav_x + 17, ty + 2, tabs[i], 0xFFFFFF);
    }
    draw_string(nav_x + 13, win->y + win->h - 34, "F2 AT BOOT", 0xAAB9CC);

    draw_string(content_x, content_y, "MYOS SYSTEM SETUP", 0x12345A);
    draw_rect(content_x, content_y + 21, content_w, 2, 0x77A9D1);

    if (setup_page == 0) {
        draw_string(content_x, content_y + 38, "System overview", 0x1768A5);
        draw_string(content_x, content_y + 62, "Platform: x86 32-bit", 0x172B4D);
        draw_string(content_x, content_y + 84, "Boot protocol: Multiboot v1", 0x172B4D);
        draw_string(content_x, content_y + 106, "Boot loader:", 0x333F55);
        draw_string(content_x + 105, content_y + 106, setup_loader_name, 0x1768A5);
        draw_string(content_x, content_y + 132, "Detected RAM:", 0x333F55);
        draw_uint(content_x + 126, content_y + 132, setup_ram_mb, 0x1768A5);
        draw_string(content_x + 166, content_y + 132, "MB", 0x333F55);
        draw_string(content_x, content_y + 158, "CPU:", 0x333F55);
        if (setup_cpu.available && setup_cpu.brand[0]) {
            setup_draw_brand_line(content_x + 48, content_y + 158, 0, 38);
        } else if (setup_cpu.available) {
            draw_string(content_x + 48, content_y + 158, setup_cpu.vendor, 0x1768A5);
        } else {
            draw_string(content_x + 48, content_y + 158, "CPUID unavailable", 0xA04444);
        }
        draw_round_rect(content_x, content_y + 194, content_w - 4, 62, 6, 0xD5E2F0);
        draw_string(content_x + 12, content_y + 207, "Hardware report is read-only.", 0x172B4D);
        draw_string(content_x + 12, content_y + 228, "Firmware setup is managed before GRUB.", 0x172B4D);
    } else if (setup_page == 1) {
        draw_string(content_x, content_y + 38, "Processor information", 0x1768A5);
        if (!setup_cpu.available) {
            draw_string(content_x, content_y + 68, "CPUID is not available on this CPU.", 0xA04444);
        } else {
            setup_draw_row(content_x, content_y + 66, "Vendor:", setup_cpu.vendor, 0x1768A5);
            draw_string(content_x, content_y + 91, "Brand:", 0x333F55);
            setup_draw_brand_line(content_x, content_y + 112, 0, 38);
            setup_draw_brand_line(content_x, content_y + 130, 38, 10);
            draw_string(content_x, content_y + 158, "Family:", 0x333F55);
            draw_uint(content_x + 142, content_y + 158, setup_cpu.family, 0x1768A5);
            draw_string(content_x + 190, content_y + 158, "Model:", 0x333F55);
            draw_uint(content_x + 252, content_y + 158, setup_cpu.model, 0x1768A5);
            draw_string(content_x, content_y + 181, "Stepping:", 0x333F55);
            draw_uint(content_x + 142, content_y + 181, setup_cpu.stepping, 0x1768A5);
            draw_string(content_x, content_y + 218, "CPU features:", 0x1768A5);
            setup_draw_cpu_feature(content_x, content_y + 241, "FPU", 0, 0);
            setup_draw_cpu_feature(content_x + 170, content_y + 241, "TSC", 4, 0);
            setup_draw_cpu_feature(content_x, content_y + 261, "MSR", 5, 0);
            setup_draw_cpu_feature(content_x + 170, content_y + 261, "PAE", 6, 0);
            setup_draw_cpu_feature(content_x, content_y + 281, "MMX", 23, 0);
            setup_draw_cpu_feature(content_x + 170, content_y + 281, "SSE", 25, 0);
            setup_draw_cpu_feature(content_x, content_y + 301, "SSE2", 26, 0);
            setup_draw_cpu_feature(content_x + 170, content_y + 301, "SSE3", 0, 1);
            draw_string(content_x, content_y + 326, "Clock and voltage are firmware-controlled.", 0x7A5361);
        }
    } else if (setup_page == 2) {
        draw_string(content_x, content_y + 38, "Memory information", 0x1768A5);
        draw_string(content_x, content_y + 68, "Usable RAM (below 4 GB):", 0x333F55);
        draw_uint(content_x, content_y + 90, setup_ram_mb, 0x1768A5);
        draw_string(content_x + 48, content_y + 90, "MB", 0x333F55);
        draw_string(content_x, content_y + 126, "Free kernel heap:", 0x333F55);
        draw_uint(content_x, content_y + 148, heap_available() / (1024u * 1024u), 0x1768A5);
        draw_string(content_x + 30, content_y + 148, "MB", 0x333F55);
        draw_string(content_x, content_y + 184, "Memory map entries:", 0x333F55);
        draw_uint(content_x + 180, content_y + 184, setup_mmap_entries, 0x1768A5);
        draw_string(content_x, content_y + 218, "Base memory (BDA):", 0x333F55);
        draw_uint(content_x + 180, content_y + 218, bda_word(BDA_BASE_MEM), 0x1768A5);
        draw_string(content_x + 225, content_y + 218, "KB", 0x333F55);
        draw_round_rect(content_x, content_y + 254, content_w - 4, 54, 6, 0xD5E2F0);
        draw_string(content_x + 10, content_y + 266, "RAM timings and capacity are set by firmware.", 0x172B4D);
        draw_string(content_x + 10, content_y + 286, "MyOS currently reports memory; it does not tune it.", 0x172B4D);
    } else if (setup_page == 3) {
        draw_string(content_x, content_y + 38, "Boot configuration", 0x1768A5);
        draw_string(content_x, content_y + 68, "Boot protocol:", 0x333F55);
        draw_string(content_x + 142, content_y + 68, "Multiboot v1", 0x1768A5);
        draw_string(content_x, content_y + 96, "Boot loader:", 0x333F55);
        draw_string(content_x + 142, content_y + 96, setup_loader_name, 0x1768A5);
        draw_string(content_x, content_y + 124, "Boot device ID:", 0x333F55);
        if (setup_mbi && (((multiboot_info_t*)setup_mbi)->flags & (1u << 3))) {
            draw_hex_dword(content_x + 142, content_y + 124,
                           ((multiboot_info_t*)setup_mbi)->boot_device, 0x1768A5);
        } else {
            draw_string(content_x + 142, content_y + 124, "Not reported", 0xA04444);
        }
        draw_round_rect(content_x, content_y + 164, content_w - 4, 112, 6, 0xD5E2F0);
        draw_string(content_x + 10, content_y + 178, "Firmware options are read-only here.", 0x172B4D);
        draw_string(content_x + 10, content_y + 201, "Change boot order in motherboard BIOS/UEFI.", 0x172B4D);
        draw_string(content_x + 10, content_y + 224, "CPU clocks and RAM timings are firmware-owned.", 0x172B4D);
        draw_string(content_x + 10, content_y + 247, "Persistent MyOS settings need a filesystem.", 0x172B4D);
    } else {
        draw_string(content_x, content_y + 38, "About MyOS Setup", 0x1768A5);
        draw_string(content_x, content_y + 68, "Version:", 0x333F55);
        draw_string(content_x + 90, content_y + 68, MYOS_KERNEL_VERSION, 0x1768A5);
        draw_string(content_x, content_y + 96, "Architecture: Kernel Architecture 2.0", 0x172B4D);
        draw_string(content_x, content_y + 124, "Platform: 32-bit x86", 0x172B4D);
        draw_string(content_x, content_y + 152, "Boot: GRUB / Multiboot v1", 0x172B4D);
        draw_round_rect(content_x, content_y + 194, content_w - 4, 66, 6, 0xD5E2F0);
        draw_string(content_x + 10, content_y + 208, "This is an OS setup utility, not motherboard", 0x172B4D);
        draw_string(content_x + 10, content_y + 230, "firmware. Hardware tuning is not implemented.", 0x172B4D);
    }
}

static void open_setup_window(void) {
    window_app = WINDOW_APP_MYOS_SETUP;
    setup_page = 0;
    my_window.title = "MyOS Setup";
    my_window.w = screen_width > 620 ? 590 : screen_width - 24;
    my_window.h = screen_height > 480 ? 430 : screen_height - 56;
    if (my_window.w < 440) my_window.w = screen_width - 8;
    if (my_window.h < 340) my_window.h = screen_height - 8;
    my_window.x = (screen_width - my_window.w) / 2;
    my_window.y = (screen_height - 40 - my_window.h) / 2;
    if (my_window.x < 0) my_window.x = 0;
    if (my_window.y < 0) my_window.y = 0;
    my_window.visible = 1;
    my_window.minimized = 0;
    my_window.is_maximized = 0;
    dragging = 0;
    add_dirty_rect(0, 0, screen_width, screen_height);
}

static void open_system_info_window(void) {
    window_app = WINDOW_APP_SYSTEM_INFO;
    my_window.title = "System Information";
    my_window.w = 460;
    my_window.h = 400;
    my_window.x = (screen_width - my_window.w) / 2;
    my_window.y = (screen_height - 40 - my_window.h) / 2;
    if (my_window.x < 0) my_window.x = 0;
    if (my_window.y < 0) my_window.y = 0;
    my_window.visible = 1;
    my_window.minimized = 0;
    my_window.is_maximized = 0;
    dragging = 0;
    add_dirty_rect(0, 0, screen_width, screen_height);
}

void draw_window_content(window_t* win) {
    if (window_app == WINDOW_APP_MYOS_SETUP) {
        draw_setup_content(win);
        return;
    }
    int x = win->x + 15;
    int y = win->y + 32;

    unsigned short equip = bda_word(BDA_EQUIPMENT);
    int floppies = (equip >> 6) & 0x03;
    unsigned char shift = *bda_shift();

    draw_string(x, y, "BIOS Ticks (live):", 0x000000);
    draw_uint(x + 175, y, *bda_ticks(), 0x000088); y += 18;
    draw_string(x, y, "Video Mode:", 0x000000);
    draw_uint(x + 110, y, bda_byte(BDA_VIDEO_MODE), 0x000000);
    draw_string(x + 145, y, "Cols:", 0x000000);
    draw_uint(x + 200, y, bda_word(BDA_VIDEO_COLS), 0x000000); y += 18;
    draw_string(x, y, "Equipment:", 0x000000);
    draw_hex_word(x + 110, y, equip, 0x000000);
    draw_string(x + 180, y, "Floppies:", 0x000000);
    draw_uint(x + 260, y, floppies, 0x000000); y += 18;
    draw_string(x, y, "Base RAM:", 0x000000);
    draw_uint(x + 110, y, bda_word(BDA_BASE_MEM), 0x000000);
    draw_string(x + 160, y, "KB", 0x000000); y += 22;
    draw_rect(x, y, win->w - 30, 1, 0x888888); y += 8;

    draw_string(x, y, "EBDA:", 0x000088);
    draw_uint(x + 75, y, ebda_phys(), 0x000000); y += 18;
    draw_string(x, y, "CMOS Battery:", 0x000000);
    if (read_cmos(0x0D) & 0x80) {
        draw_string(x + 150, y, "DEAD", 0xCC0000);
    } else {
        draw_string(x + 150, y, "OK", 0x00AA00);
    }
    y += 18;
    
    unsigned short ext_kb = read_cmos(0x30) | ((unsigned short)read_cmos(0x31) << 8);
    draw_string(x, y, "Ext. Memory:", 0x000000);
    draw_uint(x + 130, y, ext_kb, 0x000000);
    draw_string(x + 190, y, "KB", 0x000000); y += 22;
    draw_rect(x, y, win->w - 30, 1, 0x888888); y += 8;

    draw_string(x, y, "Keyboard (BDA 0x417):", 0x000000);
    draw_hex_byte(x + 210, y, shift, 0x000088); y += 20;
    const char* labels[] = {"NUM", "CAPS", "SCRL", "SHFT", "CTRL", "ALT"};
    int bits[] = {0x20, 0x40, 0x10, 0x03, 0x04, 0x08};
    int bx = x;
    for (int i = 0; i < 6; i++) {
        int on = (shift & bits[i]) != 0;
        unsigned int bg = on ? 0x00CC00 : 0xAAAAAA;
        unsigned int fg = on ? 0xFFFFFF : 0x333333;
        draw_round_rect(bx, y, 42, 20, 4, bg);
        draw_string(bx + 6, y + 2, labels[i], fg);
        bx += 48;
    }
    y += 32;

    draw_rect(x, y, win->w - 30, 1, 0x888888); y += 8;

    draw_round_rect(x + 110, y, 160, 30, 6, 0x555555);
    draw_string(x + 120, y + 10, "ATA READ TEST", 0xFFFFFF); y += 40;
    
    draw_string(x, y, "Sector 1 Data (from ATA):", 0x000000); y += 18;
    const char* hex = "0123456789ABCDEF";
    for (int i = 0; i < 32; i++) {
        char out[4];
        out[0] = hex[(sector1_buffer[i] >> 4) & 0xF];
        out[1] = hex[sector1_buffer[i] & 0xF];
        out[2] = ' '; out[3] = '\0';
        draw_string(x + (i % 16) * 27, y + (i / 16) * 18, out, 0x000088);
    }
}

void draw_window(window_t* win) {
    if (!win->visible) return;
    draw_rect(win->x + 5, win->y + 5, win->w, win->h, 0x333333);
    draw_round_rect(win->x, win->y, win->w, win->h, 10, 0xCCCCCC);
    draw_rect(win->x + 10, win->y, win->w - 20, 24, 0x000088);
    draw_fill_circle(win->x + 10, win->y + 12, 12, 0x000088);
    draw_fill_circle(win->x + win->w - 11, win->y + 12, 12, 0x000088);
    draw_rect(win->x + 10, win->y + 12, win->w - 20, 12, 0x000088);
    draw_string(win->x + 15, win->y + 4, win->title, 0xFFFFFF);
    
    draw_fill_circle(win->x + win->w - 65, win->y + 12, 8, 0xAAAA00);
    draw_string(win->x + win->w - 68, win->y + 4, "-", 0xFFFFFF);
    draw_fill_circle(win->x + win->w - 40, win->y + 12, 8, 0x00AA00);
    draw_string(win->x + win->w - 44, win->y + 4, "M", 0xFFFFFF);
    draw_fill_circle(win->x + win->w - 15, win->y + 12, 8, 0xCC0000);
    draw_string(win->x + win->w - 18, win->y + 4, "x", 0xFFFFFF);

    draw_line(win->x + 10, win->y, win->x + win->w - 10, win->y, 0xFFFFFF);
    draw_line(win->x + 10, win->y + win->h - 1, win->x + win->w - 10, win->y + win->h - 1, 0x888888);
    draw_line(win->x, win->y + 12, win->x, win->y + win->h - 12, 0xFFFFFF);
    draw_line(win->x + win->w - 1, win->y + 12, win->x + win->w - 1, win->y + win->h - 12, 0x888888);
    draw_window_content(win);
}

// ==========================================
// 9. DESKTOP
// ==========================================
unsigned char disk_buffer[512];

void draw_desktop() {
    int sy = (clip_y < 0) ? 0 : clip_y;
    int ey = clip_y + clip_h; if (ey > screen_height) ey = screen_height;
    int sx = (clip_x < 0) ? 0 : clip_x;
    int ex = clip_x + clip_w; if (ex > screen_width) ex = screen_width;

    for (int y = sy; y < ey; y++) {
        unsigned char blue = 100 + (y * 100 / screen_height);
        unsigned int col = (0 << 16) | (50 << 8) | blue;
        for (int x = sx; x < ex; x++) put_pixel(x, y, col);
    }

    draw_rect(0, screen_height - 40, screen_width, 40, 0xCCCCCC);
    draw_round_rect(5, screen_height - 35, 80, 30, 8, 0x33AA33);
    draw_string(20, screen_height - 28, "Start", 0xFFFFFF);
    draw_clock();
    
    draw_rect(50, 50, 64, 64, 0xFFDD00);
    draw_rect(50, 150, 64, 64, 0xDDDDDD);
    draw_string(100, 50, "My Computer", 0xFFFFFF);
    draw_string(100, 150, "Recycle Bin", 0xFFFFFF);

    draw_string(200, 200, "Disk Sector 0 Data:", 0xFFFFFF);
    for(int i = 0; i < 40; i++) {
        char hex[] = "0123456789ABCDEF";
        char out[4];
        out[0] = hex[(disk_buffer[i] >> 4) & 0xF];
        out[1] = hex[disk_buffer[i] & 0xF];
        out[2] = ' '; out[3] = '\0';
        int x = 200 + (i % 10) * 27;
        int y = 225 + (i / 10) * 18;
        draw_string(x, y, out, 0xFFFF00);
    }

    draw_window(&my_window);

    if (my_window.minimized) {
        draw_round_rect(150, screen_height - 35, 180, 30, 8, 0xDDDDDD);
        draw_string(160, screen_height - 28, my_window.title, 0x000000);
    }

    if (start_menu_open) {
        draw_round_rect(5, screen_height - 180, 180, 130, 10, 0xEEEEEE);
        draw_rect(5, screen_height - 180, 180, 24, 0xAAAAAA);
        draw_string(15, screen_height - 176, "MyOS Menu", 0x000000);
        if (current_hover == 1) draw_rect(5, screen_height - 140, 180, 24, 0xAAAAAA);
        draw_icon(15, screen_height - 136, icon_folder, 0xFFFFFF);
        draw_string(35, screen_height - 136, "Programs", 0x000000);
        if (current_hover == 2) draw_rect(5, screen_height - 110, 180, 24, 0xAAAAAA);
        draw_icon(15, screen_height - 106, icon_gear, 0x000000);
        draw_string(35, screen_height - 106, "Settings", 0x000000);
        if (current_hover == 3) draw_rect(5, screen_height - 80, 180, 24, 0xAAAAAA);
        draw_icon(15, screen_height - 76, icon_power, 0xCC0000);
        draw_string(35, screen_height - 76, "Shutdown", 0x000000);
    }
}

// ==========================================
// 10. UPDATE SCREEN
// ==========================================
void update_screen() {
    if (num_dirty_rects == 0) return;
    wait_for_vsync();
    for (int i = 0; i < num_dirty_rects; i++) {
        Rect r = dirty_rects[i];
        if (r.w <= 0 || r.h <= 0) continue;
        clip_x = r.x; clip_y = r.y; clip_w = r.w; clip_h = r.h;
        draw_desktop();
        draw_mouse(mouse_x, mouse_y);
        for (int row = r.y; row < r.y + r.h; row++) {
            if (row >= 0 && row < screen_height) {
                int sx = (r.x < 0) ? 0 : r.x;
                int ex = (r.x + r.w > screen_width) ? screen_width : r.x + r.w;
                if (ex > sx) {
                    memcpy(framebuffer + row * pitch + sx * 4,
                           &back_buffer[row * screen_width + sx],
                           (ex - sx) * 4);
                }
            }
        }
    }
    num_dirty_rects = 0;
    clip_x = 0; clip_y = 0; clip_w = screen_width; clip_h = screen_height;
}

// ==========================================
// 11. КЛИКИ
// ==========================================
void handle_click(int x, int y) {
    if (my_window.minimized) {
        if (x >= 150 && x <= 330 && y >= screen_height - 35 && y <= screen_height - 5) {
            my_window.minimized = 0;
            my_window.visible = 1;
            add_dirty_rect(0, 0, screen_width, screen_height);
            return;
        }
    }

    if (my_window.visible) {
        int dy = y - (my_window.y + 12);
        int dx = x - (my_window.x + my_window.w - 15);
        if (dx * dx + dy * dy <= 8 * 8) {
            my_window.visible = 0;
            my_window.minimized = 0;
            my_window.is_maximized = 0;
            dragging = 0;
            add_dirty_rect(0, 0, screen_width, screen_height);
            return;
        }

        dx = x - (my_window.x + my_window.w - 40);
        if (dx * dx + dy * dy <= 8 * 8) {
            add_dirty_rect(0, 0, screen_width, screen_height);
            if (my_window.is_maximized) {
                my_window.x = my_window.old_x; my_window.y = my_window.old_y;
                my_window.w = my_window.old_w; my_window.h = my_window.old_h;
                my_window.is_maximized = 0;
            } else {
                my_window.old_x = my_window.x; my_window.old_y = my_window.y;
                my_window.old_w = my_window.w; my_window.old_h = my_window.h;
                my_window.x = 0; my_window.y = 0;
                my_window.w = screen_width; my_window.h = screen_height - 40;
                my_window.is_maximized = 1;
            }
            add_dirty_rect(0, 0, screen_width, screen_height);
            return;
        }

        dx = x - (my_window.x + my_window.w - 65);
        if (dx * dx + dy * dy <= 8 * 8) {
            my_window.visible = 0;
            my_window.minimized = 1;
            dragging = 0;
            add_dirty_rect(0, 0, screen_width, screen_height);
            add_dirty_rect(150, screen_height - 35, 180, 30);
            return;
        }

        if (window_app == WINDOW_APP_MYOS_SETUP) {
            int nav_x = my_window.x + 12;
            int nav_top = my_window.y + 28 + 72;
            if (x >= nav_x + 7 && x <= nav_x + 119 &&
                y >= nav_top - 5 && y < nav_top - 5 + SETUP_PAGE_COUNT * 39) {
                int page = (y - (nav_top - 5)) / 39;
                if (page >= 0 && page < SETUP_PAGE_COUNT) {
                    setup_page = page;
                    add_dirty_rect(my_window.x, my_window.y, my_window.w, my_window.h);
                }
                return;
            }
            if (x >= my_window.x && x <= my_window.x + my_window.w &&
                y >= my_window.y && y <= my_window.y + 24) {
                dragging = 1;
                drag_offset_x = x - my_window.x;
                drag_offset_y = y - my_window.y;
            }
            return;
        }

        int by = my_window.y + 32 + 220;
        if (x >= my_window.x + 110 && x <= my_window.x + 270 && y >= by && y <= by + 30) {
            /* Read-only until MyOS has a partition-aware block-device layer. */
            (void)ata_read_sector(1, sector1_buffer);
            mark_window_dirty();
            return;
        }

        if (x >= my_window.x && x <= my_window.x + my_window.w &&
            y >= my_window.y && y <= my_window.y + 24) {
            dragging = 1;
            drag_offset_x = x - my_window.x;
            drag_offset_y = y - my_window.y;
            return;
        }
    }

    /* Desktop icon: My Computer -> System Information. */
    if (x >= 50 && x <= 114 && y >= 50 && y <= 114) {
        if (!my_window.visible) open_system_info_window();
        return;
    }

    /* Start button toggles the Start menu. */
    if (x >= 5 && x <= 85 && y >= screen_height - 35 && y <= screen_height - 5) {
        start_menu_open = !start_menu_open;
        add_dirty_rect(5, screen_height - 180, 180, 130);
        return;
    }

    if (start_menu_open) {
        if (x >= 5 && x <= 185 && y >= screen_height - 110 && y <= screen_height - 86) {
            start_menu_open = 0;
            open_setup_window();
            return;
        } else if (x >= 5 && x <= 185 && y >= screen_height - 140 && y <= screen_height - 116) {
            start_menu_open = 0;
            add_dirty_rect(5, screen_height - 180, 180, 130);
            return;
        } else if (x >= 5 && x <= 185 && y >= screen_height - 80 && y <= screen_height - 56) {
            outw(0x604, 0x2000);
            while (1) __asm__ volatile ("hlt");
        } else if (!(x >= 5 && x <= 185 && y >= screen_height - 180 && y <= screen_height - 50)) {
            start_menu_open = 0;
            add_dirty_rect(5, screen_height - 180, 180, 130);
            return;
        }
    }
}

// ==========================================
// 12. ПРЕРЫВАНИЯ И ОТЛОЖЕННАЯ ОБРАБОТКА СОБЫТИЙ
// ==========================================
typedef struct {
    unsigned int gs, fs, es, ds, edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    unsigned int int_no, err_code, eip, cs, eflags, useresp, ss;
} registers_t;

#define INPUT_QUEUE_SIZE 128u
#define INPUT_QUEUE_MASK (INPUT_QUEUE_SIZE - 1u)
#define INPUT_EVENT_KEYBOARD 1u
#define INPUT_EVENT_MOUSE    2u

typedef struct {
    unsigned char type;
    unsigned char data;
    signed char dx;
    signed char dy;
    unsigned char buttons;
} input_event_t;

static input_event_t input_queue[INPUT_QUEUE_SIZE];
static volatile unsigned int input_head = 0;
static volatile unsigned int input_tail = 0;
volatile unsigned int timer_ticks = 0;
volatile unsigned int timer_redraw_pending = 0;
unsigned int tick_acc = 0;
unsigned int bda_tick_acc = 0;

void isr_handler(registers_t *regs) {
    (void)regs;
    fatal_error("CPU EXCEPTION");
}

static void enqueue_input_event(input_event_t event) {
    unsigned int head = input_head;
    unsigned int next = (head + 1u) & INPUT_QUEUE_MASK;
    if (next == input_tail) return;
    input_queue[head] = event;
    __asm__ volatile ("" ::: "memory");
    input_head = next;
}

static int dequeue_input_event(input_event_t* event) {
    unsigned int tail = input_tail;
    if (tail == input_head) return 0;
    __asm__ volatile ("" ::: "memory");
    *event = input_queue[tail];
    input_tail = (tail + 1u) & INPUT_QUEUE_MASK;
    return 1;
}

void update_hover_state() {
    if (!start_menu_open) { current_hover = 0; return; }
    int nh = 0;
    if (mouse_x >= 5 && mouse_x <= 185) {
        if (mouse_y >= screen_height - 140 && mouse_y <= screen_height - 116) nh = 1;
        else if (mouse_y >= screen_height - 110 && mouse_y <= screen_height - 86) nh = 2;
        else if (mouse_y >= screen_height - 80 && mouse_y <= screen_height - 56) nh = 3;
    }
    if (nh != current_hover) {
        if (current_hover == 1) add_dirty_rect(5, screen_height - 140, 180, 24);
        if (current_hover == 2) add_dirty_rect(5, screen_height - 110, 180, 24);
        if (current_hover == 3) add_dirty_rect(5, screen_height - 80, 180, 24);
        if (nh == 1) add_dirty_rect(5, screen_height - 140, 180, 24);
        if (nh == 2) add_dirty_rect(5, screen_height - 110, 180, 24);
        if (nh == 3) add_dirty_rect(5, screen_height - 80, 180, 24);
        current_hover = nh;
    }
}

static void process_mouse_event(const input_event_t* event) {
    int dx = event->dx;
    int dy = event->dy;
    int left_click = event->buttons & 1u;

    add_dirty_rect(mouse_x, mouse_y, CURSOR_W, CURSOR_H);
    mouse_x += dx;
    mouse_y += dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > screen_width - CURSOR_W) mouse_x = screen_width - CURSOR_W;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > screen_height - CURSOR_H) mouse_y = screen_height - CURSOR_H;

    if (dragging && (dx != 0 || dy != 0)) {
        add_dirty_rect(my_window.x - 5, my_window.y - 5, my_window.w + 10, my_window.h + 10);
        my_window.x += dx;
        my_window.y += dy;
        if (my_window.x < 0) my_window.x = 0;
        if (my_window.x > screen_width - my_window.w) my_window.x = screen_width - my_window.w;
        if (my_window.y < 0) my_window.y = 0;
        if (my_window.y > (screen_height - 40) - my_window.h) my_window.y = (screen_height - 40) - my_window.h;
        add_dirty_rect(my_window.x - 5, my_window.y - 5, my_window.w + 10, my_window.h + 10);
    }

    update_hover_state();
    if (left_click && !left_click_prev) {
        handle_click(mouse_x, mouse_y);
        left_click_prev = 1;
    } else if (!left_click && left_click_prev) {
        dragging = 0;
        left_click_prev = 0;
    }
    add_dirty_rect(mouse_x, mouse_y, CURSOR_W, CURSOR_H);
}

static void process_input_events(void) {
    input_event_t event;
    while (dequeue_input_event(&event)) {
        if (event.type == INPUT_EVENT_KEYBOARD) {
            handle_keyboard_scancode(event.data);
        } else if (event.type == INPUT_EVENT_MOUSE) {
            process_mouse_event(&event);
        }
    }
}

void irq_handler(registers_t *regs) {
    if (regs->int_no == 32) {
        /* PIT runs at 100 Hz; update the BIOS tick counter at ~18.2 Hz. */
        timer_ticks++;
        tick_acc++;
        bda_tick_acc += 182;
        if (bda_tick_acc >= 1000) {
            bda_tick_acc -= 1000;
            volatile unsigned int* bios_ticks = bda_ticks();
            ++(*bios_ticks);
            if (*bios_ticks >= 0x1800B0u) {
                *bios_ticks = 0;
                *(volatile unsigned char*)(BDA_BASE + 0x70) = 1;
            }
        }
        if (tick_acc >= 100) {
            tick_acc = 0;
            timer_redraw_pending = 1;
        }
    } else if (regs->int_no == 33) {
        input_event_t event = { INPUT_EVENT_KEYBOARD, inb(0x60), 0, 0, 0 };
        enqueue_input_event(event);
    } else if (regs->int_no == 44) {
        if (inb(0x64) & 0x20) {
            unsigned char value = inb(0x60);
            if (mouse_cycle == 0 && !(value & 0x08)) {
                /* Re-synchronise to the first byte of a PS/2 packet. */
            } else {
                mouse_byte[mouse_cycle++] = (signed char)value;
                if (mouse_cycle == 3) {
                    input_event_t event;
                    mouse_cycle = 0;
                    event.type = INPUT_EVENT_MOUSE;
                    event.data = 0;
                    event.dx = (signed char)mouse_byte[1];
                    event.dy = (signed char)(-(signed char)mouse_byte[2]);
                    event.buttons = (unsigned char)mouse_byte[0];
                    enqueue_input_event(event);
                }
            }
        }
    }

    if (regs->int_no >= 32 && regs->int_no <= 47) {
        pic_send_eoi((unsigned char)(regs->int_no - 32));
    }
}

// ==========================================
// 13. BOOT SPLASH
// ==========================================
static void wait_boot_ticks(unsigned int ticks) {
    unsigned int start = timer_ticks;
    while ((unsigned int)(timer_ticks - start) < ticks && !boot_setup_requested) {
        process_input_events();
        if (boot_setup_requested) break;
        /* PIT IRQ wakes the CPU; input is handled in this non-IRQ context. */
        __asm__ volatile ("sti; hlt");
    }
}

void show_boot_splash() {
    for(int y = 0; y < screen_height; y++) {
        for(int x = 0; x < screen_width; x++) {
            put_pixel(x, y, 0x000044);
        }
    }
    
    int cx = screen_width / 2;
    int cy = screen_height / 2 - 50;
    
    draw_rect(cx - 150, cy - 60, 300, 100, 0x000066);
    draw_rect(cx - 148, cy - 58, 296, 96, 0xFFFFFF);
    
    draw_string(cx - 36, cy - 35, "MyOS", 0xFFFFFF);
    draw_string(cx - 45, cy - 10, "Booting system...", 0xAAAAAA);
    draw_string(cx - 68, cy + 120, "Press F2 for MyOS Setup", 0xAAAAAA);
    
    draw_rect(cx - 100, cy + 60, 200, 20, 0xFFFFFF);
    
    swap_buffers();
}

void update_boot_progress(int percent) {
    int cx = screen_width / 2;
    int cy = screen_height / 2 - 50;
    
    int fill_width = (196 * percent) / 100;
    draw_rect(cx - 98, cy + 62, fill_width, 16, 0x00FF00);
    
    draw_rect(cx - 30, cy + 90, 80, 16, 0x000044);
    
    char buf[5];
    buf[0] = '0' + (percent / 10);
    buf[1] = '0' + (percent % 10);
    buf[2] = '%';
    buf[3] = '\0';
    draw_string(cx - 15, cy + 90, buf, 0xFFFFFF);
    
    swap_buffers();
}

// ==========================================
// 14. ГЛАВНАЯ
// ==========================================
static void early_boot_error(const char* message) {
    volatile unsigned short* text = (volatile unsigned short*)0xB8000;
    unsigned int i = 0;
    while (message[i] && i < 80u) {
        text[i] = (unsigned short)(0x4F00u | (unsigned char)message[i]);
        ++i;
    }
    __asm__ volatile ("cli");
    for (;;) __asm__ volatile ("hlt");
}

void kernel_main(multiboot_info_t* mbi) {
    if (!mbi || !heap_init(mbi->flags, mbi->mem_upper, mbi->mmap_addr, mbi->mmap_length)) {
        early_boot_error("MyOS: cannot initialise heap from Multiboot memory map");
    }
    init_graphics(mbi);
    setup_collect_cpu_info();
    setup_collect_memory_info(mbi);
    setup_collect_boot_info(mbi);

    /* Install IRQ handlers before the splash so F2 can be detected reliably. */
    pic_remap();
    pit_init(100);
    idt_init();
    __asm__ volatile ("sti");

    show_boot_splash();
    wait_boot_ticks(200u); /* about two seconds at 100 Hz */

    if (!boot_setup_requested) {
        update_boot_progress(20);
        wait_boot_ticks(100u);
    }
    if (!boot_setup_requested) {
        (void)ata_read_sector(0, disk_buffer);
        (void)ata_read_sector(1, sector1_buffer);
    }

    if (!boot_setup_requested) {
        update_boot_progress(50);
        wait_boot_ticks(100u);
    }
    mouse_init();

    if (!boot_setup_requested) {
        update_boot_progress(75);
        wait_boot_ticks(100u);
    }
    if (!boot_setup_requested) {
        update_boot_progress(100);
        wait_boot_ticks(100u);
    }

    mouse_x = screen_width / 2;
    mouse_y = screen_height / 2;

    add_dirty_rect(0, 0, screen_width, screen_height);
    update_screen();

    __asm__ volatile ("sti");
    for (;;) {
        process_input_events();

        if (timer_redraw_pending) {
            __asm__ volatile ("cli");
            timer_redraw_pending = 0;
            __asm__ volatile ("sti");
            add_dirty_rect(screen_width - 90, screen_height - 35, 85, 30);
            if (my_window.visible) {
                add_dirty_rect(my_window.x - 5, my_window.y - 5,
                               my_window.w + 10, my_window.h + 10);
            }
        }

        /* Framebuffer work is performed only in the main loop, never in IRQ. */
        if (num_dirty_rects > 0) update_screen();
        __asm__ volatile ("sti; hlt");
    }
}
