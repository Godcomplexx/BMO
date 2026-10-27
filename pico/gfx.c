#include "gfx.h"

#include <math.h>

static color_t fb[SCR_W * SCR_H];

color_t *gfx_fb(void) { return fb; }

void gfx_clear(color_t c) {
    for (int i = 0; i < SCR_W * SCR_H; i++) fb[i] = c;
}

static void hspan(int x0, int x1, int y, color_t c) {
    if (y < 0 || y >= SCR_H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > SCR_W) x1 = SCR_W;
    color_t *p = &fb[y * SCR_W];
    for (int x = x0; x < x1; x++) p[x] = c;
}

void gfx_rect(int x, int y, int w, int h, color_t c) {
    for (int j = 0; j < h; j++) hspan(x, x + w, y + j, c);
}

void gfx_ellipse(float cx, float cy, float rx, float ry, color_t c) {
    if (rx < 0.6f) rx = 0.6f;
    if (ry < 0.6f) ry = 0.6f;
    int y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
    for (int y = y0; y <= y1; y++) {
        float dy = ((float)y + 0.5f - cy) / ry;
        if (dy < -1.f || dy > 1.f) continue;
        float hw = rx * sqrtf(1.f - dy * dy);
        hspan((int)floorf(cx - hw + 0.5f), (int)floorf(cx + hw + 0.5f), y, c);
    }
}

void gfx_half_ellipse_down(float cx, float cy, float rx, float ry, color_t c) {
    int y0 = (int)floorf(cy), y1 = (int)ceilf(cy + ry);
    for (int y = y0; y <= y1; y++) {
        float dy = ((float)y + 0.5f - cy) / ry;
        if (dy < 0.f || dy > 1.f) continue;
        float hw = rx * sqrtf(1.f - dy * dy);
        hspan((int)floorf(cx - hw + 0.5f), (int)floorf(cx + hw + 0.5f), y, c);
    }
}

void gfx_line(float x0, float y0, float x1, float y1, float w, color_t c) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    int steps = (int)ceilf(len) + 1;
    float r = w * 0.5f;
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        gfx_ellipse(x0 + dx * t, y0 + dy * t, r, r, c);
    }
}

void gfx_arc(float cx, float cy, float rx, float ry, float a0, float a1,
             float w, color_t c) {
    const float deg = 3.14159265f / 180.f;
    float span = a1 - a0;
    int steps = (int)(fabsf(span) / 6.f) + 2;
    float r = w * 0.5f;
    for (int i = 0; i <= steps; i++) {
        float a = (a0 + span * (float)i / (float)steps) * deg;
        gfx_ellipse(cx + rx * cosf(a), cy + ry * sinf(a), r, r, c);
    }
}

void gfx_poly(const float *xy, int n, color_t c) {
    if (n < 3 || n > 16) return;
    float miny = xy[1], maxy = xy[1];
    for (int i = 1; i < n; i++) {
        if (xy[2 * i + 1] < miny) miny = xy[2 * i + 1];
        if (xy[2 * i + 1] > maxy) maxy = xy[2 * i + 1];
    }
    for (int y = (int)floorf(miny); y <= (int)ceilf(maxy); y++) {
        float yc = (float)y + 0.5f;
        float xs[16];
        int k = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            float ya = xy[2 * i + 1], yb = xy[2 * j + 1];
            if ((ya <= yc && yb > yc) || (yb <= yc && ya > yc)) {
                float t = (yc - ya) / (yb - ya);
                xs[k++] = xy[2 * i] + t * (xy[2 * j] - xy[2 * i]);
            }
        }
        for (int a = 1; a < k; a++) {            /* insertion sort */
            float v = xs[a];
            int b = a - 1;
            while (b >= 0 && xs[b] > v) { xs[b + 1] = xs[b]; b--; }
            xs[b + 1] = v;
        }
        for (int a = 0; a + 1 < k; a += 2)
            hspan((int)floorf(xs[a] + 0.5f), (int)floorf(xs[a + 1] + 0.5f), y, c);
    }
}
