#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t level;     /* best of the two channels: mean abs deviation, 24-bit units */
    uint32_t level_l, level_r;
    int32_t raw_l, raw_r; /* last 24-bit sample of each slot */
    bool signal;        /* false if the data line looks dead/stuck */
    uint8_t stuck;      /* 0 = ok, 1 = all zeros, 2 = all ones, 3 = constant */
    /* State of the SD (data) pin measured with pull-up / pull-down while the
     * clocks run: 0 = active (mic drives it), 1 = floating (nothing driving),
     * 2 = held low, 3 = held high. */
    uint8_t pin;
    uint8_t idle;       /* SD pin state with clocks held low (same codes as `pin`) */
    int short_pin;      /* idle-low only: GPIO the SD wire is tied to, -1 = none found, -2 = n/a */
    const int32_t *raw;   /* the same words before the >>8 (to check bit alignment) */
    const int32_t *wave;  /* last 512 words, interleaved L,R (24-bit samples) */
    bool clk_ok;        /* BCLK/WS really toggle on GP2/GP3 (Pico side is fine) */
    uint8_t data_pin;  /* GPIO the SD line is being read from (auto-detected) */
} mic_stats_t;

void mic_start(void);
void mic_stop(void);
/* Blocks for ~16 ms and measures loudness. */
mic_stats_t mic_sample(void);

/* Streams `seconds` of the left channel over the USB serial port (blocking):
 * header 'AUD0' + u32 rate + u32 sample count, then int32 24-bit samples. */
void mic_stream_left(uint32_t seconds);
