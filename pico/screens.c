#include "screens.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "gfx.h"

#define BG     RGB565(201, 228, 195)
#define INK    RGB565(20, 40, 20)
#define DGREEN RGB565(57, 99, 55)
#define RED    RGB565(200, 40, 40)

static void centered(int y, const char *s, int scale, color_t c) {
    gfx_text((SCR_W - gfx_text_width(s, scale)) / 2, y, s, scale, c);
}

static void footer(void) {
    centered(118, "HOLD BTN = NEXT MODE", 1, DGREEN);
}

void screen_message(const char *l1, const char *l2) {
    gfx_clear(BG);
    centered(48, l1, 2, INK);
    if (l2) centered(72, l2, 1, DGREEN);
}

/* 0..1 on a log scale of the 24-bit sample magnitude. */
static float level_frac(uint32_t v) {
    float f = (log2f((float)v + 1.f) - 8.f) / 14.f;
    return f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
}

void screen_mic(const mic_stats_t *s, uint32_t peak, uint32_t floor, uint32_t calib_ms_left) {
    char buf[40];
    gfx_clear(BG);
    centered(4, "MIC TEST", 2, INK);

    /* Waveform of the louder channel, +-32768 full scale (noise stays thin). */
    const int wy = 40, wh = 17;
    const int ch = s->level_r > s->level_l ? 1 : 0;
    gfx_rect(0, wy, SCR_W, 1, DGREEN);
    int prev = 0;
    for (int x = 0; x < SCR_W; x++) {
        /* first difference = high-pass: slow rumble / DC drift disappears */
        int32_t v = s->wave[2 * (96 + x) + ch] - s->wave[2 * (95 + x) + ch];
        /* log scale: silence (+-8) and a loud clap (+-100000) are both visible */
        float mag = log2f((float)(v < 0 ? -v : v) + 1.f) / 18.f;   /* 0..~1 */
        if (mag > 1.f) mag = 1.f;
        int y = (int)(mag * (float)wh) * (v < 0 ? -1 : 1);
        if (x > 0) gfx_line((float)(x - 1), (float)(wy + prev), (float)x, (float)(wy + y), 1.f, INK);
        prev = y;
    }

    /* Absolute level in dB (20*log10 of the 24-bit amplitude): independent of
     * the silence calibration. ~20 dB = quiet room, 90+ dB = a clap up close. */
    float abs_db = 20.f * log10f((float)s->level + 1.f);
    float floor_db = 20.f * log10f((float)floor + 1.f);
    float peak_db = 20.f * log10f((float)peak + 1.f);
    const int bx = 4, by = 62, bw = SCR_W - 8, bh = 8;
    gfx_rect(bx, by, bw, bh, INK);
    gfx_rect(bx + 1, by + 1, bw - 2, bh - 2, BG);
    float frac = abs_db / 120.f; if (frac > 1.f) frac = 1.f;
    gfx_rect(bx + 1, by + 1, (int)((float)(bw - 2) * frac), bh - 2, s->signal ? DGREEN : RED);
    float pf = peak_db / 120.f; if (pf > 1.f) pf = 1.f;
    gfx_rect(bx + 1 + (int)((float)(bw - 3) * pf), by + 1, 2, bh - 2, INK);
    float ff = floor_db / 120.f; if (ff > 1.f) ff = 1.f;                 /* recorded silence */
    gfx_rect(bx + 1 + (int)((float)(bw - 3) * ff), by - 2, 1, bh + 4, RED);

    if (calib_ms_left > 0) {
        centered(72, "KEEP QUIET", 2, RED);
        snprintf(buf, sizeof buf, "RECORDING SILENCE %lu S", (unsigned long)((calib_ms_left + 999) / 1000));
        centered(88, buf, 1, INK);
    } else if (s->signal) {
        snprintf(buf, sizeof buf, "%d DB", (int)(abs_db + 0.5f));
        centered(74, buf, 3, abs_db > floor_db + 12.f ? RED : DGREEN);
    } else {
        static const char code[] = {'A', 'F', 'L', 'H'}; /* active/floating/low/high */
        const char *why;
        char shortbuf[32];
        if (s->idle == 2 && s->short_pin >= 0) {
            snprintf(shortbuf, sizeof shortbuf, "SD TOUCHES GP%d WIRE!", s->short_pin);
            why = shortbuf;
        } else if (s->idle == 2) {
            why = "SD LOW: GND/MIC UNPOWERED?";
        } else if (s->pin == 1)  why = "SD FLOATING: WIRE/POWER?";
        else if (s->pin == 2)    why = "MIC OUTPUTS ZEROS";
        else if (s->pin == 3)    why = "SD HELD HIGH: SHORT 3V3?";
        else                     why = "MIC TALKS BUT IS FLAT";
        centered(74, "NO SIGNAL", 1, RED);
        centered(83, why, 1, RED);
        snprintf(buf, sizeof buf, "NOCLK:%c CLK:%c  %s", code[s->idle & 3], code[s->pin & 3],
                 s->clk_ok ? "CLK OK" : "CLK DEAD");
        centered(92, buf, 1, s->clk_ok ? DGREEN : RED);
    }

    snprintf(buf, sizeof buf, "L %lu R %lu SD GP%u%s", (unsigned long)s->level_l,
             (unsigned long)s->level_r, s->data_pin, s->data_pin == MIC_DATA ? "" : "*");
    gfx_text(4, 100, buf, 1, INK);
    snprintf(buf, sizeof buf, "SILENCE %d DB  PEAK %d DB", (int)(floor_db + 0.5f),
             (int)(peak_db + 0.5f));
    gfx_text(4, 109, buf, 1, INK);
    centered(119, "A=SILENCE 60S B=LCD TEST", 1, DGREEN);
}

static void fmt1(char *out, size_t n, const char *prefix, float v, const char *suffix) {
    int t = (int)lroundf(v * 10.f);
    snprintf(out, n, "%s%s%d.%d%s", prefix, t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10, suffix);
}

void screen_weather(const weather_t *w) {
    char buf[40];
    gfx_clear(BG);
    centered(6, "WEATHER", 2, INK);
    if (w->found) {
        if (w->valid) {
            fmt1(buf, sizeof buf, "T ", w->temp_c, " C");
            centered(32, buf, 3, INK);
            fmt1(buf, sizeof buf, "H ", w->hum, " %");
            centered(62, buf, 3, INK);
        } else {
            centered(46, "READ ERROR", 2, RED);
        }
        snprintf(buf, sizeof buf, "SHT31 0X%02X GP%u/GP%u", w->addr, w->sda, w->scl);
        centered(96, buf, 1, DGREEN);
    } else {
        centered(32, "NO SENSOR", 2, RED);
        centered(54, "TRIED GP0/1 6/7 8/9", 1, INK);
        centered(64, "16/17 26/27 (SDA/SCL)", 1, INK);
        if (w->other) {
            snprintf(buf, sizeof buf, "FOUND 0X%02X GP%u/GP%u", w->other_addr,
                     w->other_sda, w->other_scl);
            centered(80, buf, 1, DGREEN);
            centered(90, "NOT AN SHT31", 1, DGREEN);
        } else {
            centered(80, "NOTHING ON I2C", 1, DGREEN);
        }
    }
    footer();
}
