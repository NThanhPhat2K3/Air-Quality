#ifndef DISPLAY_HAL_H
#define DISPLAY_HAL_H

#include <stdint.h>

#define TFT_WIDTH 160
#define TFT_HEIGHT 128

#define RGB565(r, g, b)                                                        \
  (uint16_t)((((r)&0xF8) << 8) | (((g)&0xFC) << 3) | ((b) >> 3))

void lcd_init(void);
void lcd_present_framebuffer(void);

void fb_clear(uint16_t color);
void fb_draw_pixel(int x, int y, uint16_t color);
void fb_fill_rect(int x, int y, int w, int h, uint16_t color);
void fb_draw_rect(int x, int y, int w, int h, uint16_t color);
void fb_draw_line(int x0, int y0, int x1, int y1, uint16_t color);
void fb_fill_circle(int cx, int cy, int radius, uint16_t color);
void fb_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2,
                      uint16_t color);

uint16_t *fb_data(void);

#endif
