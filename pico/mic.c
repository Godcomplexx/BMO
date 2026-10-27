#include "mic.h"

#include "config.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "i2s_rx.pio.h"
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/stdio.h"

#ifndef MIC_SAMPLE_RATE
#define MIC_SAMPLE_RATE 16000u /* SCK = 64 x fs = 1.024 MHz (48000 was tried: same noise) */
#endif
#define MIC_WORDS       512 /* 256 stereo frames = 16 ms */

static PIO g_pio = pio0;
static int g_sm = -1;
static uint g_offset;
static bool g_loaded;
static uint g_data = MIC_DATA;   /* pin the microphone SD line is read from */
static uint8_t g_pin;
static bool g_clk_ok;
static int g_short = -2;      /* -2 not tested, -1 no GPIO found (GND?), else GPIO number */
static uint8_t g_idle;   /* SD pin state with the clocks held low */
static int32_t g_buf[MIC_WORDS];
static int32_t g_raw[MIC_WORDS];

/* The microphone must get a clock without interruption: when the SM stalled on
 * a full FIFO (while the screen was being drawn) the INMP441 went to sleep and
 * needed ~256 ms to wake up again, so the reading just twitched. A DMA channel
 * therefore drains the FIFO continuously into this ring buffer (128 ms). */
#define RING_WORDS 2048
static int32_t g_ring[RING_WORDS] __attribute__((aligned(RING_WORDS * 4)));
static int g_dma = -1;

static void dma_start(void) {
    if (g_dma < 0) g_dma = dma_claim_unused_channel(true);
    dma_channel_abort(g_dma);
    dma_channel_config c = dma_channel_get_default_config(g_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(g_pio, g_sm, false));
    channel_config_set_ring(&c, true, 13);   /* wrap the write address at 8 KiB */
    dma_channel_configure(g_dma, &c, g_ring, &g_pio->rxf[g_sm], 0xFFFFFFFFu, true);
}

/* Free pins an SD wire could plausibly have been soldered to by mistake. */
static const uint8_t kScan[] = {5, 4, 0, 1, 6, 7, 8, 9, 16, 17, 22, 26, 27, 28};

static void setup_sm(void) {
    /* Keep the MAX98357A quiet: its DIN shares the clocks but stays low. */
    if (g_data != MIC_AMP_DIN) {
        gpio_init(MIC_AMP_DIN);
        gpio_set_dir(MIC_AMP_DIN, GPIO_OUT);
        gpio_put(MIC_AMP_DIN, 0);
    }
    if (!g_loaded) {
        g_offset = pio_add_program(g_pio, &i2s_rx_program);
        g_sm = pio_claim_unused_sm(g_pio, true);
        g_loaded = true;
    }
    pio_gpio_init(g_pio, MIC_BCLK);
    pio_gpio_init(g_pio, MIC_WS);
    pio_gpio_init(g_pio, g_data);
    /* Soft clock edges: fast 12 mA edges ring on long jumper wires, the extra
     * half-edges upset the microphone's modulator and it outputs noise bursts. */
    gpio_set_slew_rate(MIC_BCLK, GPIO_SLEW_RATE_SLOW);
    gpio_set_slew_rate(MIC_WS, GPIO_SLEW_RATE_SLOW);
    gpio_set_drive_strength(MIC_BCLK, GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_drive_strength(MIC_WS, GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_pulls(g_data, false, true);   /* idle slot must read 0, not random noise */
    /* Sample the data pin directly (no 2-flop synchroniser delay). */
    g_pio->input_sync_bypass |= 1u << g_data;

    pio_sm_set_consecutive_pindirs(g_pio, g_sm, MIC_BCLK, 2, true);
    pio_sm_set_consecutive_pindirs(g_pio, g_sm, g_data, 1, false);

    pio_sm_config c = i2s_rx_program_get_default_config(g_offset);
    sm_config_set_in_pins(&c, g_data);
    sm_config_set_sideset_pins(&c, MIC_BCLK);
    sm_config_set_in_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) /
                                 (float)(MIC_SAMPLE_RATE * 64u * 2u));
    pio_sm_init(g_pio, g_sm, g_offset, &c);
    pio_sm_set_enabled(g_pio, g_sm, true);
    dma_start();     /* keep the FIFO empty so the clocks never stall */
    sleep_ms(300);   /* the INMP441 needs 2^18 clock cycles (~256 ms at 1 MHz) to wake up */
}

/* 0 = active (toggling), 1 = floating, 2 = held low, 3 = held high. */
static uint8_t classify_pin(uint pin) {
    int up = 0, down = 0;
    gpio_set_pulls(pin, true, false);
    sleep_us(100);
    for (int i = 0; i < 64; i++) { up += gpio_get(pin); sleep_us(7); }
    gpio_set_pulls(pin, false, true);
    sleep_us(100);
    for (int i = 0; i < 64; i++) { down += gpio_get(pin); sleep_us(7); }
    gpio_set_pulls(pin, false, false);
    if (up >= 60 && down <= 4) return 1;
    if (up <= 4 && down <= 4) return 2;
    if (up >= 60 && down >= 60) return 3;
    return 0;
}

/* Read the clock outputs back to prove the Pico really toggles them. */
static bool clocks_running(void) {
    /* Tight loop (no sleeps): sampling at ~1 MHz would alias with the 1.024 MHz
     * bit clock and hide the edges. 6000 reads span ~120 us, i.e. > 1 WS period. */
    int bclk = 0, ws = 0, lb = gpio_get(MIC_BCLK), lw = gpio_get(MIC_WS);
    for (int i = 0; i < 6000; i++) {
        int b = gpio_get(MIC_BCLK), w = gpio_get(MIC_WS);
        bclk += b != lb;
        ws += w != lw;
        lb = b;
        lw = w;
    }
    return bclk >= 20 && ws >= 1;
}

/* Which GPIO is the SD wire shorted to? Pull SD up, then drive every other free
 * pin low in turn: if SD follows, they are connected. LCD/SD-card/button pins are
 * left alone. */
static int find_short(uint sd) {
    static const uint8_t kPins[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 16, 17, 22, 26, 27, 28};
    gpio_init(sd);
    gpio_set_dir(sd, GPIO_IN);
    gpio_set_pulls(sd, true, false);
    sleep_us(100);
    if (!gpio_get(sd)) {              /* already low with nothing driven by us:      */
        gpio_set_pulls(sd, false, false); /* GND short or an unpowered chip clamping it */
        return -1;
    }
    for (unsigned i = 0; i < sizeof kPins; i++) {
        uint p = kPins[i];
        if (p == sd) continue;
        gpio_init(p);
        gpio_set_dir(p, GPIO_OUT);
        gpio_put(p, 0);
        sleep_us(50);
        int follows = !gpio_get(sd);
        gpio_set_dir(p, GPIO_IN);
        gpio_disable_pulls(p);
        if (follows) {
            gpio_set_pulls(sd, false, false);
            return (int)p;
        }
    }
    gpio_set_pulls(sd, false, false);
    return -1;
}

void mic_start(void) {
    g_data = MIC_DATA;

    /* First look at the SD pin with the clocks held LOW: a microphone without
     * clock is silent (high-Z), so anything driving the pin now is outside. */
    gpio_init(MIC_BCLK); gpio_set_dir(MIC_BCLK, GPIO_OUT); gpio_put(MIC_BCLK, 0);
    gpio_init(MIC_WS);   gpio_set_dir(MIC_WS, GPIO_OUT);   gpio_put(MIC_WS, 0);
    gpio_init(g_data);   gpio_set_dir(g_data, GPIO_IN);
    sleep_ms(2);
    g_idle = classify_pin(g_data);
    g_short = -2;
    if (g_idle == 2) {   /* held low without any clock: find out what it is tied to */
        gpio_init(MIC_BCLK); gpio_init(MIC_WS);   /* release them from the test above */
        g_short = find_short(g_data);
    }

    setup_sm();
    g_clk_ok = clocks_running();
    g_pin = classify_pin(g_data);

    if (g_pin != 0) {                 /* no data here: look for the wire elsewhere */
        for (unsigned k = 0; k < sizeof kScan; k++) {
            uint cand = kScan[k];
            if (cand == g_data || cand == MIC_BCLK || cand == MIC_WS) continue;
            gpio_init(cand);
            gpio_set_dir(cand, GPIO_IN);
            if (classify_pin(cand) == 0) {
                pio_sm_set_enabled(g_pio, g_sm, false);
                gpio_init(g_data);
                g_data = cand;
                setup_sm();
                g_pin = classify_pin(g_data);
                break;
            }
        }
    }
    gpio_set_pulls(g_data, false, true);   /* probing above cleared the pulls */
}

void mic_stop(void) {
    if (g_dma >= 0) dma_channel_abort(g_dma);
    if (g_loaded) pio_sm_set_enabled(g_pio, g_sm, false);
}

mic_stats_t mic_sample(void) {
    /* Newest MIC_WORDS words, starting on a left-channel (even) slot. */
    uint32_t wp = (((uintptr_t)dma_hw->ch[g_dma].write_addr - (uintptr_t)g_ring) >> 2) &
                  (RING_WORDS - 1);
    uint32_t start = (wp - MIC_WORDS) & (RING_WORDS - 1) & ~1u;
    for (int i = 0; i < MIC_WORDS; i++) {
        g_raw[i] = g_ring[(start + (uint32_t)i) & (RING_WORDS - 1)];
        g_buf[i] = g_raw[i] >> 8; /* 24-bit */
    }

    mic_stats_t out = {0};
    uint32_t level[2] = {0, 0};
    bool varies[2] = {false, false};
    bool all_zero = true, all_ones = true;

    for (int ch = 0; ch < 2; ch++) {
        int32_t first = g_buf[ch];
        for (int i = ch; i < MIC_WORDS; i += 2) {
            if (g_buf[i] != first) varies[ch] = true;
            if (g_buf[i] != 0) all_zero = false;
            if (g_buf[i] != -1) all_ones = false;
        }
        /* Loudness = mean |x[n] - x[n-1]|: a first difference is a high-pass that
         * ignores the slow drift / rumble / DC offset of the MEMS microphone (it
         * swung by +-100000 between windows) and only reacts to real sound. */
        int64_t dsum = 0;
        for (int i = ch + 2; i < MIC_WORDS; i += 2) {
            int32_t d = g_buf[i] - g_buf[i - 2];
            dsum += d < 0 ? -d : d;
        }
        level[ch] = (uint32_t)(dsum / ((MIC_WORDS / 2) - 1));
    }

    out.level_l = level[0];
    out.level_r = level[1];
    out.raw_l = g_buf[MIC_WORDS - 2];
    out.raw_r = g_buf[MIC_WORDS - 1];
    out.level = level[0] > level[1] ? level[0] : level[1];
    out.signal = varies[0] || varies[1];
    out.stuck = out.signal ? 0 : (all_zero ? 1 : (all_ones ? 2 : 3));
    out.pin = g_pin;
    out.data_pin = (uint8_t)g_data;
    out.clk_ok = g_clk_ok;
    out.idle = g_idle;
    out.short_pin = g_short;
    out.wave = g_buf;
    out.raw = g_raw;
    return out;
}

static uint32_t ring_write_index(void) {
    return (((uintptr_t)dma_hw->ch[g_dma].write_addr - (uintptr_t)g_ring) >> 2) & (RING_WORDS - 1);
}

void mic_stream_left(uint32_t seconds) {
    const uint32_t total = seconds * MIC_SAMPLE_RATE;
    struct { char magic[4]; uint32_t rate; uint32_t n; } hdr = {{'A', 'U', 'D', '0'}, MIC_SAMPLE_RATE, total};
    stdio_put_string((const char *)&hdr, (int)sizeof hdr, false, false);

    uint32_t rp = ring_write_index() & ~1u;        /* start on a left-channel word */
    uint32_t sent = 0;
    int32_t out[64];
    while (sent < total) {
        uint32_t avail = (ring_write_index() - rp) & (RING_WORDS - 1);
        if (avail < 128) continue;                  /* wait for 64 stereo frames */
        for (int k = 0; k < 64; k++) out[k] = g_ring[(rp + 2u * (uint32_t)k) & (RING_WORDS - 1)] >> 8;
        rp = (rp + 128u) & (RING_WORDS - 1);
        stdio_put_string((const char *)out, (int)sizeof out, false, false);
        sent += 64;
    }
}
