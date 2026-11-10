#include "io.h"
#include "pic.h"
#include "idt.h"
#include "heap.h"
#include "string.h"
#include "ata.h"

// ==========================================
// Forward declarations
// ==========================================
extern unsigned char font_8x16[128][16];

typedef struct { 
    int x, y, w, h; 
    int visible; 
    int minimized;
    int is_maximized;
    int old_x, old_y, old_w, old_h;
    const char* title; 
} window_t;
extern window_t my_window;

// ==========================================
// 1. MULTIBOOT
// ==========================================
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

// ==========================================
// 2. BIOS DATA AREA (BDA) — 0x400-0x4FF
// ==========================================
#define BDA_BASE 0x400
#define BDA_EQUIPMENT   0x10
#define BDA_BASE_MEM    0x13
#define BDA_SHIFT_FLAGS 0x17
#define BDA_VIDEO_MODE  0x49
#define BDA_VIDEO_COLS  0x4A
#define BDA_TIMER_TICKS 0x6C

unsigned char  bda_byte(unsigned int off) { return *(volatile unsigned char*)(BDA_BASE + off); }
unsigned short bda_word(unsigned int off) { return *(volatile unsigned short*)(BDA_BASE + off); }
unsigned int   bda_dword(unsigned int off){ return *(volatile unsigned int*)(BDA_BASE + off); }
volatile unsigned char* bda_shift() { return (volatile unsigned char*)(BDA_BASE + BDA_SHIFT_FLAGS); }
volatile unsigned int*  bda_ticks() { return (volatile unsigned int*)(BDA_BASE + BDA_TIMER_TICKS); }

// ==========================================
// 2b. EBDA
// ==========================================
unsigned short ebda_segment()  { return *(volatile unsigned short*)0x40E; }
unsigned int   ebda_phys()     { return (unsigned int)ebda_segment() << 4; }
unsigned short ebda_size_kb()  {
    unsigned int p = ebda_phys();
    if (p == 0) return 0;
    return *(volatile unsigned short*)p;
}

// ==========================================
// 2c. CMOS
// ==========================================
const char* cmos_floppy_name(unsigned char t) {
    switch(t) {
        case 0x0: return "none";
        case 0x1: return "360K";
        case 0x2: return "1.2M";
        case 0x3: return "720K";
        case 0x4: return "1.44M";
        case 0x5: return "2.88M";
        default:  return "?";
    }
}

// ==========================================
// 3. ГРАФИКА И DIRTY RECTS
// ==========================================
unsigned char* framebuffer = 0;
unsigned int pitch = 0;
unsigned int* back_buffer = 0;
int screen_width = 0;
int screen_height = 0;

int clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;

#define MAX_DIRTY_RECTS 32
typedef struct { int x, y, w, h; } Rect;
Rect dirty_rects[MAX_DIRTY_RECTS];
int num_dirty_rects = 0;

static inline void outw(unsigned short port, unsigned short val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

void fatal_error(const char* msg) {
    __asm__ volatile ("cli");
    if (framebuffer && pitch > 0 && screen_width > 0 && screen_height > 0) {
        int cx = 10, cy = 10;
        for (int i = 0; msg[i]; i++) {
            unsigned char c = (unsigned char)msg[i];
            if (c == '\n') { cx = 10; cy += 16; continue; }
            if (c > 127) { cx += 9; continue; }
            unsigned char* g = font_8x16[(int)c];
            for (int row = 0; row < 16; row++) for (int col = 0; col < 8; col++) {
                int px = cx + col, py = cy + row;
                if (px >= 0 && px < screen_width && py >= 0 && py < screen_height) {
                    unsigned int* p = (unsigned int*)(framebuffer + py * pitch + px * 4);
                    *p = ((g[row] >> col) & 1) ? 0xFFFFFF : 0x000088;
                }
            }
            cx += 9;
        }
    }
    while (1) __asm__ volatile ("hlt");
}

void add_dirty_rect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (num_dirty_rects >= MAX_DIRTY_RECTS) {
        dirty_rects[0] = (Rect){0, 0, screen_width, screen_height};
        num_dirty_rects = 1;
        return;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > screen_width) w = screen_width - x;
    if (y + h > screen_height) h = screen_height - y;
    if (w <= 0 || h <= 0) return;
    dirty_rects[num_dirty_rects++] = (Rect){x, y, w, h};
}

void init_graphics(multiboot_info_t* mbi) {
    if (!(mbi->flags & (1 << 12))) fatal_error("Fatal: No framebuffer!");
    if ((unsigned long long)mbi->framebuffer_addr > 0xFFFFFFFFULL) fatal_error("Fatal: FB above 4GB!");
    framebuffer = (unsigned char*)(unsigned int)mbi->framebuffer_addr;
    pitch = mbi->framebuffer_pitch;
    screen_width = mbi->framebuffer_width;
    screen_height = mbi->framebuffer_height;
    if (screen_width <= 0 || screen_height <= 0) fatal_error("Fatal: Invalid dims!");
    if (mbi->framebuffer_bpp != 32 || mbi->framebuffer_type != 1) fatal_error("Fatal: Need 32bpp RGB!");
    if (pitch < (unsigned int)(screen_width * 4)) fatal_error("Fatal: Pitch mismatch!");

    unsigned long long total = (unsigned long long)screen_width * screen_height * 4ULL;
    if (total > 128ULL * 1024 * 1024) fatal_error("Fatal: >128MB!");
    back_buffer = (unsigned int*)malloc((unsigned int)total);
    if (!back_buffer) fatal_error("Fatal: malloc failed!");
    clip_x = 0; clip_y = 0; clip_w = screen_width; clip_h = screen_height;
}

void put_pixel(int x, int y, unsigned int color) {
    if (x < clip_x || x >= clip_x + clip_w || y < clip_y || y >= clip_y + clip_h) return;
    if (x < 0 || x >= screen_width || y < 0 || y >= screen_height) return;
    back_buffer[y * screen_width + x] = color;
}

void draw_rect(int x, int y, int w, int h, unsigned int color) {
    for (int i = 0; i < h; i++) for (int j = 0; j < w; j++) put_pixel(x + j, y + i, color);
}

void draw_line(int x0, int y0, int x1, int y1, unsigned int color) {
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? (y0 - y1) : (y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    while (1) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_fill_circle(int xc, int yc, int r, unsigned int color) {
    int x = 0, y = r, d = 3 - 2 * r;
    while (y >= x) {
        draw_line(xc - x, yc + y, xc + x, yc + y, color);
        draw_line(xc - x, yc - y, xc + x, yc - y, color);
        draw_line(xc - y, yc + x, xc + y, yc + x, color);
        draw_line(xc - y, yc - x, xc + y, yc - x, color);
        x++;
        if (d > 0) { y--; d = d + 4 * (x - y) + 10; } else { d = d + 4 * x + 6; }
    }
}

void draw_round_rect(int x, int y, int w, int h, int r, unsigned int color) {
    draw_rect(x + r, y, w - 2 * r, h, color);
    draw_rect(x, y + r, w, h - 2 * r, color);
    draw_fill_circle(x + r, y + r, r, color);
    draw_fill_circle(x + w - r - 1, y + r, r, color);
    draw_fill_circle(x + r, y + h - r - 1, r, color);
    draw_fill_circle(x + w - r - 1, y + h - r - 1, r, color);
}

void wait_for_vsync() {
    while (inb(0x3DA) & 0x08);
    while (!(inb(0x3DA) & 0x08));
}

void swap_buffers() {
    wait_for_vsync();
    for (int y = 0; y < screen_height; y++) {
        memcpy(framebuffer + y * pitch, &back_buffer[y * screen_width], screen_width * 4);
    }
}

// ==========================================
// 4. ШРИФТ 8x16 (ПОЛНЫЙ)
// ==========================================
unsigned char font_8x16[128][16] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 1
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 2
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 3
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 4
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 5
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 6
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 7
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 8
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 9
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 10
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 11
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 12
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 13
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 14
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 15
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 16
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 17
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 18
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 19
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 20
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 21
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 22
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 23
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 24
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 25
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 26
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 27
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 28
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 29
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 30
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 31
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 32
    { 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00}, // 33
    { 0x00, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 34
    { 0x00, 0x00, 0x36, 0x36, 0x7F, 0x36, 0x36, 0x36, 0x36, 0x7F, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00}, // 35
    { 0x00, 0x18, 0x18, 0x3E, 0x63, 0x03, 0x03, 0x3E, 0x60, 0x60, 0x63, 0x3E, 0x18, 0x18, 0x00, 0x00}, // 36
    { 0x00, 0x00, 0x00, 0x63, 0x33, 0x18, 0x0C, 0x06, 0x33, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 37
    { 0x00, 0x00, 0x1C, 0x36, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x33, 0x33, 0x6E, 0x00, 0x00, 0x00, 0x00}, // 38
    { 0x00, 0x0C, 0x0C, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 39
    { 0x00, 0x00, 0x18, 0x0C, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00}, // 40
    { 0x00, 0x00, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00}, // 41
    { 0x00, 0x00, 0x00, 0x66, 0x3C, 0x18, 0x7E, 0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 42
    { 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 43
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06, 0x00, 0x00}, // 44
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 45
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00}, // 46
    { 0x00, 0x00, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 47
    { 0x00, 0x00, 0x3E, 0x63, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x63, 0x63, 0x3E, 0x00, 0x00, 0x00, 0x00}, // 48
    { 0x00, 0x00, 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00, 0x00, 0x00, 0x00}, // 49
    { 0x00, 0x00, 0x1E, 0x33, 0x30, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x33, 0x3F, 0x00, 0x00, 0x00, 0x00}, // 50
    { 0x00, 0x00, 0x1E, 0x33, 0x30, 0x30, 0x1C, 0x30, 0x30, 0x30, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 51
    { 0x00, 0x00, 0x38, 0x3C, 0x36, 0x33, 0x33, 0x7F, 0x30, 0x30, 0x30, 0x78, 0x00, 0x00, 0x00, 0x00}, // 52
    { 0x00, 0x00, 0x3F, 0x03, 0x03, 0x1F, 0x30, 0x30, 0x30, 0x30, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 53
    { 0x00, 0x00, 0x1C, 0x06, 0x03, 0x03, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 54
    { 0x00, 0x00, 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00}, // 55
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 56
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x30, 0x18, 0x0E, 0x00, 0x00, 0x00, 0x00}, // 57
    { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00}, // 58
    { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00}, // 59
    { 0x00, 0x00, 0x18, 0x0C, 0x06, 0x03, 0x03, 0x06, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 60
    { 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 61
    { 0x00, 0x00, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 62
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00}, // 63
    { 0x00, 0x00, 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x7B, 0x7B, 0x03, 0x03, 0x3E, 0x00, 0x00, 0x00, 0x00}, // 64
    { 0x00, 0x00, 0x0C, 0x1E, 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x33, 0x00, 0x00, 0x00, 0x00}, // 65
    { 0x00, 0x00, 0x3F, 0x66, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x66, 0x66, 0x3F, 0x00, 0x00, 0x00, 0x00}, // 66
    { 0x00, 0x00, 0x3C, 0x66, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00}, // 67
    { 0x00, 0x00, 0x1F, 0x36, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00, 0x00, 0x00, 0x00}, // 68
    { 0x00, 0x00, 0x7F, 0x66, 0x06, 0x16, 0x1E, 0x16, 0x06, 0x06, 0x66, 0x7F, 0x00, 0x00, 0x00, 0x00}, // 69
    { 0x00, 0x00, 0x7F, 0x66, 0x06, 0x16, 0x1E, 0x16, 0x06, 0x06, 0x06, 0x0F, 0x00, 0x00, 0x00, 0x00}, // 70
    { 0x00, 0x00, 0x3C, 0x66, 0x03, 0x03, 0x03, 0x73, 0x63, 0x63, 0x66, 0x7C, 0x00, 0x00, 0x00, 0x00}, // 71
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x33, 0x33, 0x00, 0x00, 0x00, 0x00}, // 72
    { 0x00, 0x00, 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 73
    { 0x00, 0x00, 0x78, 0x30, 0x30, 0x30, 0x30, 0x30, 0x33, 0x33, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 74
    { 0x00, 0x00, 0x67, 0x66, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x66, 0x66, 0x67, 0x00, 0x00, 0x00, 0x00}, // 75
    { 0x00, 0x00, 0x0F, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x66, 0x66, 0x7F, 0x00, 0x00, 0x00, 0x00}, // 76
    { 0x00, 0x00, 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x6B, 0x63, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00}, // 77
    { 0x00, 0x00, 0x63, 0x67, 0x6F, 0x7F, 0x7B, 0x73, 0x63, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00}, // 78
    { 0x00, 0x00, 0x1C, 0x36, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00, 0x00, 0x00, 0x00}, // 79
    { 0x00, 0x00, 0x3F, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x06, 0x06, 0x0F, 0x00, 0x00, 0x00, 0x00}, // 80
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x70, 0x00, 0x00, 0x00, 0x00}, // 81
    { 0x00, 0x00, 0x3F, 0x66, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x66, 0x66, 0x67, 0x00, 0x00, 0x00, 0x00}, // 82
    { 0x00, 0x00, 0x1E, 0x33, 0x03, 0x03, 0x1E, 0x30, 0x30, 0x30, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 83
    { 0x00, 0x00, 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 84
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 85
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00}, // 86
    { 0x00, 0x00, 0x63, 0x63, 0x63, 0x63, 0x6B, 0x6B, 0x7F, 0x77, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00}, // 87
    { 0x00, 0x00, 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x1C, 0x36, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00}, // 88
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 89
    { 0x00, 0x00, 0x7F, 0x63, 0x31, 0x18, 0x0C, 0x06, 0x03, 0x63, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00}, // 90
    { 0x00, 0x00, 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 91
    { 0x00, 0x00, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 92
    { 0x00, 0x00, 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 93
    { 0x00, 0x00, 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 94
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00}, // 95
    { 0x00, 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 96
    { 0x00, 0x00, 0x00, 0x00, 0x3C, 0x66, 0x60, 0x3E, 0x63, 0x63, 0x63, 0x7E, 0x00, 0x00, 0x00, 0x00}, // 97
    { 0x00, 0x00, 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3B, 0x00, 0x00, 0x00, 0x00}, // 98
    { 0x00, 0x00, 0x00, 0x00, 0x1E, 0x33, 0x03, 0x03, 0x03, 0x03, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 99
    { 0x00, 0x00, 0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00, 0x00, 0x00, 0x00}, // 100
    { 0x00, 0x00, 0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x03, 0x03, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 101
    { 0x00, 0x00, 0x1C, 0x36, 0x06, 0x06, 0x0F, 0x06, 0x06, 0x06, 0x06, 0x0F, 0x00, 0x00, 0x00, 0x00}, // 102
    { 0x00, 0x00, 0x00, 0x00, 0x6E, 0x33, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1E, 0x33, 0x1E, 0x00, 0x00}, // 103
    { 0x00, 0x00, 0x07, 0x06, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x66, 0x66, 0x67, 0x00, 0x00, 0x00, 0x00}, // 104
    { 0x00, 0x00, 0x18, 0x18, 0x00, 0x1C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00}, // 105
    { 0x00, 0x00, 0x60, 0x60, 0x00, 0x70, 0x60, 0x60, 0x60, 0x60, 0x63, 0x63, 0x3E, 0x00, 0x00, 0x00}, // 106
    { 0x00, 0x00, 0x07, 0x06, 0x66, 0x36, 0x1E, 0x0E, 0x1E, 0x36, 0x66, 0x67, 0x00, 0x00, 0x00, 0x00}, // 107
    { 0x00, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 108
    { 0x00, 0x00, 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x6B, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00}, // 109
    { 0x00, 0x00, 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x00, 0x00, 0x00, 0x00}, // 110
    { 0x00, 0x00, 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 111
    { 0x00, 0x00, 0x00, 0x00, 0x3B, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x0F, 0x00, 0x00, 0x00, 0x00}, // 112
    { 0x00, 0x00, 0x00, 0x00, 0x6E, 0x33, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x78, 0x00, 0x00, 0x00, 0x00}, // 113
    { 0x00, 0x00, 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x06, 0x06, 0x06, 0x0F, 0x00, 0x00, 0x00, 0x00}, // 114
    { 0x00, 0x00, 0x00, 0x00, 0x3E, 0x03, 0x03, 0x1E, 0x30, 0x30, 0x33, 0x1E, 0x00, 0x00, 0x00, 0x00}, // 115
    { 0x00, 0x00, 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00}, // 116
    { 0x00, 0x00, 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00, 0x00, 0x00, 0x00}, // 117
    { 0x00, 0x00, 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00, 0x00, 0x00, 0x00}, // 118
    { 0x00, 0x00, 0x00, 0x00, 0x63, 0x63, 0x6B, 0x6B, 0x7F, 0x7F, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00}, // 119
    { 0x00, 0x00, 0x00, 0x00, 0x63, 0x36, 0x1C, 0x1C, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00}, // 120
    { 0x00, 0x00, 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1E, 0x33, 0x1E, 0x00, 0x00}, // 121
    { 0x00, 0x00, 0x00, 0x00, 0x3F, 0x33, 0x18, 0x0C, 0x06, 0x03, 0x33, 0x3F, 0x00, 0x00, 0x00, 0x00}, // 122
    { 0x00, 0x00, 0x18, 0x0C, 0x0C, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00}, // 123
    { 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00}, // 124
    { 0x00, 0x00, 0x06, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x06, 0x00, 0x00, 0x00}, // 125
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 126
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // 127
};

const char* icon_folder[16] = {
    "................", "................", "..YYYYYYYY......", ".YYYYYYYYYY.....",
    ".YYYYYYYYYY.....", ".YYYYYYYYYYYYYY.", ".YYYYYYYYYYYYYY.", ".YYYYYYYYYYYYYY.",
    ".YYYYYYYYYYYYYY.", ".YYYYYYYYYYYYYY.", ".YYYYYYYYYYYYYY.", ".YYYYYYYYYYYYYY.",
    "................", "................", "................", "................"
};
const char* icon_gear[16] = {
    "................", "....XX....XX....", "...XXXX..XXXX...", "..XXXXXXXXXXXX..",
    "..XXXX....XXXX..", "..XXX......XXX..", "..XXX......XXX..", "..XXX......XXX..",
    "..XXX......XXX..", "..XXXX....XXXX..", "..XXXXXXXXXXXX..", "...XXXX..XXXX...",
    "....XX....XX....", "................", "................", "................"
};
const char* icon_power[16] = {
    "................", "................", ".......XX.......", ".......XX.......",
    "....X..XX..X....", "...X........X...", "..X..........X..", "..X..........X..",
    "..X..........X..", "...X........X...", "....XX....XX....", "......XXXX......",
    "................", "................", "................", "................"
};

void draw_char(int x, int y, char c, unsigned int color) {
    if (c < 0) return;
    unsigned char* g = font_8x16[(int)c];
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 8; col++)
            if ((g[row] >> col) & 1) put_pixel(x + col, y + row, color);
}

void draw_string(int x, int y, const char* str, unsigned int color) {
    int ox = 0;
    for (int i = 0; str[i]; i++) { draw_char(x + ox, y, str[i], color); ox += 9; }
}

void draw_icon(int x, int y, const char** icon, unsigned int color) {
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++) {
            char c = icon[i][j];
            if (c == '\0') break;
            if (c == 'X') put_pixel(x + j, y + i, color);
            else if (c == 'Y') put_pixel(x + j, y + i, 0xFFD700);
        }
}

void draw_uint(int x, int y, unsigned int num, unsigned int color) {
    char buf[12]; int i = 0;
    if (num == 0) { draw_char(x, y, '0', color); return; }
    while (num > 0) { buf[i++] = '0' + (num % 10); num /= 10; }
    for (int j = 0; j < i; j++) draw_char(x + j * 9, y, buf[i - 1 - j], color);
}

void draw_hex_word(int x, int y, unsigned short val, unsigned int color) {
    const char* hex = "0123456789ABCDEF";
    draw_char(x, y, '0', color); draw_char(x + 9, y, 'x', color);
    for (int i = 0; i < 4; i++) draw_char(x + 18 + i * 9, y, hex[(val >> (12 - i * 4)) & 0xF], color);
}

void draw_hex_byte(int x, int y, unsigned char val, unsigned int color) {
    const char* hex = "0123456789ABCDEF";
    draw_char(x, y, '0', color); draw_char(x + 9, y, 'x', color);
    draw_char(x + 18, y, hex[(val >> 4) & 0xF], color);
    draw_char(x + 27, y, hex[val & 0xF], color);
}

// ==========================================
// 5. RTC
// ==========================================
unsigned char read_cmos(unsigned char r) { outb(0x70, r); return inb(0x71); }
int bcd_to_bin(unsigned char b) { return (b & 0x0F) + ((b / 16) * 10); }

void draw_clock() {
    while (read_cmos(0x0A) & 0x80);
    int s = bcd_to_bin(read_cmos(0x00)), m = bcd_to_bin(read_cmos(0x02)), h = bcd_to_bin(read_cmos(0x04));
    char t[9];
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

#define BDA_FLAG_RSHIFT  0x01
#define BDA_FLAG_LSHIFT  0x02
#define BDA_FLAG_CTRL    0x04
#define BDA_FLAG_ALT     0x08
#define BDA_FLAG_SCROLL  0x10
#define BDA_FLAG_NUM     0x20
#define BDA_FLAG_CAPS    0x40

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

void draw_window_content(window_t* win) {
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

    draw_round_rect(x + 110, y, 160, 30, 6, 0x008800);
    draw_string(x + 120, y + 10, "ATA WRITE TEST", 0xFFFFFF); y += 40;
    
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
        draw_string(160, screen_height - 28, "System Info", 0x000000);
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
            add_dirty_rect(my_window.x - 5, my_window.y - 5, my_window.w + 10, my_window.h + 10);
            add_dirty_rect(150, screen_height - 35, 180, 30);
            return;
        }
    }

    if (my_window.visible) {
        int dy = y - (my_window.y + 12);
        
        int dx = x - (my_window.x + my_window.w - 15);
        if (dx*dx + dy*dy <= 8*8) {
            my_window.visible = 0;
            add_dirty_rect(my_window.x - 5, my_window.y - 5, my_window.w + 10, my_window.h + 10);
            return;
        }
        
        dx = x - (my_window.x + my_window.w - 40);
        if (dx*dx + dy*dy <= 8*8) {
            add_dirty_rect(my_window.x - 5, my_window.y - 5, my_window.w + 10, my_window.h + 10);
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
            add_dirty_rect(my_window.x - 5, my_window.y - 5, my_window.w + 10, my_window.h + 10);
            return;
        }
        
        dx = x - (my_window.x + my_window.w - 65);
        if (dx*dx + dy*dy <= 8*8) {
            my_window.visible = 0;
            my_window.minimized = 1;
            add_dirty_rect(my_window.x - 5, my_window.y - 5, my_window.w + 10, my_window.h + 10);
            add_dirty_rect(150, screen_height - 35, 180, 30);
            return;
        }

        int by = my_window.y + 32 + 220;
        if (x >= my_window.x + 110 && x <= my_window.x + 110 + 160 && y >= by && y <= by + 30) {
            unsigned char write_buf[512] = {0};
            const char* test_str = "HELLO FROM ATA WRITE!";
            for(int i=0; test_str[i]; i++) write_buf[i] = test_str[i];
            
            ata_write_sector(1, write_buf);
            ata_read_sector(1, sector1_buffer);
            
            mark_window_dirty();
            return;
        }

        if (x >= my_window.x && x <= my_window.x + my_window.w &&
            y >= my_window.y && y <= my_window.y + 24) {
            dragging = 1; drag_offset_x = x - my_window.x; drag_offset_y = y - my_window.y;
            return;
        }
    }
    if (x >= 50 && x <= 114 && y >= 50 && y <= 114) {
        if (!my_window.visible && !my_window.minimized) {
            my_window.visible = 1;
            my_window.x = (screen_width / 2) - (my_window.w / 2);
            my_window.y = (screen_height / 2) - (my_window.h / 2);
            if (my_window.x < 0) my_window.x = 0;
            if (my_window.y < 0) my_window.y = 0;
            if (my_window.x > screen_width - my_window.w) my_window.x = screen_width - my_window.w;
            if (my_window.y > (screen_height - 40) - my_window.h) my_window.y = (screen_height - 40) - my_window.h;
            add_dirty_rect(my_window.x - 5, my_window.y - 5, my_window.w + 10, my_window.h + 10);
        }
        return;
    }
    if (x >= 5 && x <= 85 && y >= screen_height - 35 && y <= screen_height - 5) {
        start_menu_open = !start_menu_open;
        add_dirty_rect(5, screen_height - 180, 180, 130);
        return;
    } else if (start_menu_open) {
        if (x >= 5 && x <= 185 && y >= screen_height - 80 && y <= screen_height - 56) {
            pic_send_eoi(12);
            outw(0x604, 0x2000);
            while(1) __asm__ volatile("hlt");
        } else if (x >= 5 && x <= 185 && y >= screen_height - 140 && y <= screen_height - 86) {
            start_menu_open = 0;
            add_dirty_rect(5, screen_height - 180, 180, 130);
            return;
        } else if (!(x >= 5 && x <= 185 && y >= screen_height - 180 && y <= screen_height - 50)) {
            start_menu_open = 0;
            add_dirty_rect(5, screen_height - 180, 180, 130);
            return;
        }
    }
}

// ==========================================
// 12. ПРЕРЫВАНИЯ
// ==========================================
typedef struct {
    unsigned int gs, fs, es, ds, edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    unsigned int int_no, err_code, eip, cs, eflags, useresp, ss;
} registers_t;

unsigned long long ticks = 0;
unsigned int tick_acc = 0;
unsigned int bda_tick_acc = 0;

void isr_handler(registers_t *regs) { (void)regs; while(1) __asm__ volatile("hlt"); }

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

void irq_handler(registers_t *regs) {
    if (regs->int_no == 32) {
        ticks++;
        tick_acc++;
        bda_tick_acc += 18;
        if (bda_tick_acc >= 100) { bda_tick_acc -= 100; (*bda_ticks())++; }

        if (tick_acc >= 100) {
            tick_acc = 0;
            add_dirty_rect(screen_width - 90, screen_height - 35, 85, 30);
            if (my_window.visible) {
                add_dirty_rect(my_window.x - 5, my_window.y - 5, my_window.w + 10, my_window.h + 10);
            }
            update_screen();
        }
    } else if (regs->int_no == 33) {
        unsigned char sc = inb(0x60);
        handle_keyboard_scancode(sc);
        if (my_window.visible) update_screen();
    }
    else if (regs->int_no == 44) {
        if (inb(0x64) & 0x20) {
            mouse_byte[mouse_cycle++] = inb(0x60);
            if (mouse_cycle == 3) {
                mouse_cycle = 0;
                int dx = (signed char)mouse_byte[1], dy = -(signed char)mouse_byte[2];
                add_dirty_rect(mouse_x, mouse_y, CURSOR_W, CURSOR_H);
                mouse_x += dx; mouse_y += dy;
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_x > screen_width - CURSOR_W) mouse_x = screen_width - CURSOR_W;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_y > screen_height - CURSOR_H) mouse_y = screen_height - CURSOR_H;
                if (dragging) {
                    add_dirty_rect(my_window.x - 5, my_window.y - 5, my_window.w + 10, my_window.h + 10);
                    my_window.x += dx; my_window.y += dy;
                    if (my_window.x < 0) my_window.x = 0;
                    if (my_window.x > screen_width - my_window.w) my_window.x = screen_width - my_window.w;
                    if (my_window.y < 0) my_window.y = 0;
                    if (my_window.y > (screen_height - 40) - my_window.h) my_window.y = (screen_height - 40) - my_window.h;
                    add_dirty_rect(my_window.x - 5, my_window.y - 5, my_window.w + 10, my_window.h + 10);
                }
                update_hover_state();
                int lc = mouse_byte[0] & 1;
                if (lc && !left_click_prev) { handle_click(mouse_x, mouse_y); left_click_prev = 1; }
                else if (!lc && left_click_prev) { dragging = 0; left_click_prev = 0; }
                add_dirty_rect(mouse_x, mouse_y, CURSOR_W, CURSOR_H);
                update_screen();
            }
        }
        pic_send_eoi(12); return;
    }
    pic_send_eoi(regs->int_no - 32);
}

// ==========================================
// 13. BOOT SPLASH
// ==========================================
void delay(int n) {
    for (volatile int i = 0; i < n; i++) {
        for (volatile int j = 0; j < 1000; j++) {
            __asm__ volatile ("nop");
        }
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
void kernel_main(multiboot_info_t* mbi) {
    heap_init();
    init_graphics(mbi);
    
    show_boot_splash();
    delay(2000); 
    
    update_boot_progress(20);
    delay(1000); 
    ata_read_sector(0, disk_buffer);
    ata_read_sector(1, sector1_buffer);
    
    update_boot_progress(50);
    delay(1000); 
    pic_remap(); 
    idt_init();
    
    update_boot_progress(75);
    delay(1000); 
    mouse_init();
    
    update_boot_progress(100);
    delay(1500); 
    
    mouse_x = screen_width / 2;
    mouse_y = screen_height / 2;
    
    add_dirty_rect(0, 0, screen_width, screen_height);
    update_screen();
    
    __asm__ volatile ("sti");
    while (1) __asm__ volatile ("hlt");
}