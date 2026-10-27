#pragma once
#include <stdint.h>

#define SCR_W 160
#define SCR_H 128

/* RGB565, stored byte-swapped so the buffer can be sent to the panel as is. */
typedef uint16_t color_t;
#define RGB565(r, g, b) ((color_t)__builtin_bswap16((uint16_t)( \
    (((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))))

color_t *gfx_fb(void);
void gfx_clear(color_t c);
void gfx_rect(int x, int y, int w, int h, color_t c);
void gfx_ellipse(float cx, float cy, float rx, float ry, color_t c);
/* Lower half of an ellipse (flat edge on top at cy). */
void gfx_half_ellipse_down(float cx, float cy, float rx, float ry, color_t c);
void gfx_line(float x0, float y0, float x1, float y1, float w, color_t c);
/* Stroked elliptical arc; angles in degrees, 0 = right, 90 = down. */
void gfx_arc(float cx, float cy, float rx, float ry, float a0, float a1,
             float w, color_t c);
void gfx_poly(const float *xy, int n, color_t c);

/* 5x7 font (A-Z, 0-9 and a few symbols); clips at the screen edges. */
void gfx_text(int x, int y, const char *s, int scale, color_t c);
int gfx_text_width(const char *s, int scale);
