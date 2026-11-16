#ifndef MYOS_FRAMEBUFFER_H
#define MYOS_FRAMEBUFFER_H

#include "myos/multiboot.h"

#define MAX_DIRTY_RECTS 32
typedef struct { int x, y, w, h; } Rect;

extern unsigned char* framebuffer;
extern unsigned int pitch;
extern unsigned int* back_buffer;
extern int screen_width;
extern int screen_height;
extern int clip_x, clip_y, clip_w, clip_h;
extern Rect dirty_rects[MAX_DIRTY_RECTS];
extern int num_dirty_rects;
extern const char* icon_folder[16];
extern const char* icon_gear[16];
extern const char* icon_power[16];

void fatal_error(const char* message) __attribute__((noreturn));
void add_dirty_rect(int x, int y, int w, int h);
void init_graphics(multiboot_info_t* mbi);
void put_pixel(int x, int y, unsigned int color);
void draw_rect(int x, int y, int w, int h, unsigned int color);
void draw_line(int x0, int y0, int x1, int y1, unsigned int color);
void draw_fill_circle(int xc, int yc, int r, unsigned int color);
void draw_round_rect(int x, int y, int w, int h, int r, unsigned int color);
void wait_for_vsync(void);
void swap_buffers(void);
void draw_char(int x, int y, char c, unsigned int color);
void draw_string(int x, int y, const char* str, unsigned int color);
void draw_icon(int x, int y, const char** icon, unsigned int color);
void draw_uint(int x, int y, unsigned int value, unsigned int color);
void draw_hex_word(int x, int y, unsigned short value, unsigned int color);
void draw_hex_byte(int x, int y, unsigned char value, unsigned int color);

#endif /* MYOS_FRAMEBUFFER_H */
