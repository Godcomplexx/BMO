#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "config.h"
#include "faces.h"
#include "lcd.h"
#include "mic.h"
#include "screens.h"
#include "weather.h"

#define FRAME_MS         33
#define DEBOUNCE_MS      40
#define TRANSITION_MS    260 /* eyes close, face swaps, eyes open */
#define WEATHER_EVERY_MS 1000
#define BLINK_MS         150
#define MIC_CALIB_QUICK_MS 2000  /* automatic calibration when entering MIC mode */
#define MIC_CALIB_FULL_MS  60000 /* button A: record a full minute of silence */
#define AUTO_FACE_MS     4000 /* faces rotate by themselves; buttons skip ahead */

typedef enum { MODE_FACES, MODE_MIC, MODE_WEATHER, MODE_COUNT } app_mode_t;
typedef enum { EV_NONE, EV_SHORT, EV_LONG } btn_event_t;

typedef struct {
    uint pin;
    bool down, long_fired;
    uint32_t changed, pressed_at;
} button_t;

/* Short press is reported on release, long press once while held. */
static btn_event_t button_poll(button_t *b, uint32_t now) {
    bool down = !gpio_get(b->pin);
    if (down != b->down && now - b->changed >= DEBOUNCE_MS) {
        b->down = down;
        b->changed = now;
        if (down) {
            b->pressed_at = now;
            b->long_fired = false;
        } else if (!b->long_fired) {
            return EV_SHORT;
        }
    }
    if (b->down && !b->long_fired && now - b->pressed_at >= LONG_PRESS_MS) {
        b->long_fired = true;
        return EV_LONG;
    }
    return EV_NONE;
}

static uint32_t rng_state = 12345;
static uint32_t rnd(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

/* 1 = open, 0 = closed; triangular dip over `dur` ms. */
static float dip(uint32_t elapsed, uint32_t dur) {
    float p = (float)elapsed / (float)dur;
    return p < 0.5f ? 1.f - p * 2.f : (p - 0.5f) * 2.f;
}

static void button_init(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

static app_mode_t mode = MODE_FACES;
static weather_t weather;
static uint32_t mic_peak, mic_floor, next_weather;
static uint32_t mic_calib_end;            /* ms timestamp, 0 = not calibrating */
#define MIC_CALIB_MAX 2048            /* frames kept while recording silence */
static uint32_t mic_calib_buf[MIC_CALIB_MAX];
static uint32_t mic_calib_n, mic_calib_skip;

static void mic_calibrate_begin(uint32_t now, uint32_t duration_ms) {
    mic_calib_end = now + duration_ms;
    mic_calib_n = 0;
    mic_calib_skip = 15;              /* ignore the start-up transient (~0.5 s) */
}

static void probe_weather(void) {
    screen_message("SCANNING", "I2C BUS...");
    lcd_flush();
    weather_probe(&weather);
    weather_read(&weather);
    next_weather = to_ms_since_boot(get_absolute_time()) + WEATHER_EVERY_MS;
}


static uint32_t median_u32(uint32_t *v, int n) {
    for (int i = 1; i < n; i++) {
        uint32_t x = v[i];
        int j = i;
        while (j > 0 && v[j - 1] > x) { v[j] = v[j - 1]; j--; }
        v[j] = x;
    }
    return v[n / 2];
}

/* Is the display's SPI traffic polluting the microphone? Measure the noise with
 * the LCD idle and while it is being refreshed. Be quiet for ~3 s. */
static void mic_lcd_noise_test(void) {
    uint32_t off[24], on[24];
    screen_message("QUIET 3 S", "LCD NOISE TEST");
    lcd_flush();
    sleep_ms(300);
    for (int i = 0; i < 24; i++) {            /* LCD idle: no SPI traffic at all */
        sleep_ms(40);
        off[i] = mic_sample().level;
    }
    for (int i = 0; i < 24; i++) {            /* LCD busy: sample right after a flush */
        lcd_flush();
        on[i] = mic_sample().level;
        sleep_ms(8);
    }
    char l1[24], l2[32];
    snprintf(l1, sizeof l1, "LCD OFF %lu", (unsigned long)median_u32(off, 24));
    snprintf(l2, sizeof l2, "LCD ON %lu (QUIET ~100-300)", (unsigned long)median_u32(on, 24));
    screen_message(l1, l2);
    lcd_flush();
    sleep_ms(6000);
}

static void enter_mode(app_mode_t m) {
    if (mode == MODE_MIC) mic_stop();
    mode = m;
    if (m == MODE_MIC) {
        mic_peak = 0;
        mic_floor = 0;
        mic_start();
        mic_calibrate_begin(to_ms_since_boot(get_absolute_time()), MIC_CALIB_QUICK_MS);
    } else if (m == MODE_WEATHER) {
        probe_weather();
    }
}

int main(void) {
    stdio_init_all();   /* USB serial: debug output for the mic (115200, any terminal) */
    /* GPIO23 = PS pin of the Pico's RT6150 buck-boost: high forces PWM mode.
     * In the default PFM (power-save) mode the 3V3 rail ripples in bursts,
     * which the INMP441 hears as crackle. */
    gpio_init(23);
    gpio_set_dir(23, GPIO_OUT);
    gpio_put(23, 1);
    lcd_init();
    button_init(BTN_NEXT);
    button_init(BTN_PREV);
    button_t next = {.pin = BTN_NEXT}, prev = {.pin = BTN_PREV};

    int face = 0, pending = 0;
    bool in_transition = false;
    uint32_t trans_start = 0, last_change = 0, swapped_at = 0;
    bool blinking = false;
    uint32_t blink_start = 0, next_blink = 2500;
    rng_state ^= time_us_32();

    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        uint32_t frame_start = now;
        btn_event_t en = button_poll(&next, now);
        btn_event_t ep = button_poll(&prev, now);

        if (en == EV_LONG) enter_mode((app_mode_t)((mode + 1) % MODE_COUNT));
        else if (ep == EV_LONG) enter_mode((app_mode_t)((mode + MODE_COUNT - 1) % MODE_COUNT));

        switch (mode) {
        case MODE_FACES: {
            if (!in_transition) {
                int dir = en == EV_SHORT ? 1 : (ep == EV_SHORT ? -1 : 0);
                if (!dir && now - last_change >= AUTO_FACE_MS) dir = 1; /* auto-rotate */
                if (dir) {
                    pending = (face + dir + FACE_COUNT) % FACE_COUNT;
                    in_transition = true;
                    trans_start = now;
                    last_change = now;
                }
            }
            float squash = 1.f;
            if (in_transition) {
                uint32_t e = now - trans_start;
                if (e >= TRANSITION_MS) {
                    in_transition = false;
                } else {
                    if (e >= TRANSITION_MS / 2 && face != pending) {
                        face = pending;
                        swapped_at = now;
                    }
                    squash = dip(e, TRANSITION_MS);
                }
            }

            /* Blinking: every 2-5 s, sometimes twice in a row. */
            float blink = 1.f;
            if (!in_transition) {
                if (blinking) {
                    uint32_t e = now - blink_start;
                    if (e >= BLINK_MS) {
                        blinking = false;
                        next_blink = now + 2200 + rnd() % 3200;
                        if (rnd() % 5 == 0) next_blink = now + 120; /* double blink */
                    } else {
                        blink = dip(e, BLINK_MS);
                    }
                } else if (now >= next_blink) {
                    blinking = true;
                    blink_start = now;
                }
            } else {
                blinking = false;
            }
            face_draw(face, now, squash, blink, now - swapped_at);
            break;
        }
        case MODE_MIC: {
            if (en == EV_SHORT) mic_calibrate_begin(now, MIC_CALIB_FULL_MS); /* A: record 60 s of silence */
            if (ep == EV_SHORT) mic_lcd_noise_test();       /* B: LCD interference test */
            {   /* PC asks for a recording: 'R' + one byte = seconds (tools/record_mic.py) */
                int c = getchar_timeout_us(0);
                if (c == 'R') {
                    int n = getchar_timeout_us(200000);
                    if (n >= 1 && n <= 120) {
                        screen_message("RECORDING", "SPEAK OR CLAP");
                        lcd_flush();
                        mic_stream_left((uint32_t)n);
                    }
                }
            }
            mic_stats_t s = mic_sample();
            uint32_t calib_left = 0;
            if (mic_calib_end) {
                if (now < mic_calib_end) {
                    if (mic_calib_skip) mic_calib_skip--;
                    else if (mic_calib_n < MIC_CALIB_MAX) mic_calib_buf[mic_calib_n++] = s.level;
                    calib_left = mic_calib_end - now;
                } else {                                    /* done: remember the silence */
                    mic_floor = 1;
                    if (mic_calib_n) {                      /* median: one cough does not matter */
                        for (uint32_t i = 1; i < mic_calib_n; i++) {   /* insertion sort */
                            uint32_t v = mic_calib_buf[i];
                            uint32_t j = i;
                            while (j > 0 && mic_calib_buf[j - 1] > v) {
                                mic_calib_buf[j] = mic_calib_buf[j - 1];
                                j--;
                            }
                            mic_calib_buf[j] = v;
                        }
                        mic_floor = mic_calib_buf[mic_calib_n / 2];
                    }
                    if (mic_floor < 4) mic_floor = 4;
                    mic_calib_end = 0;
                    mic_peak = 0;
                }
            }
            if (s.level > mic_peak && !calib_left) mic_peak = s.level;
#if MIC_SERIAL_DEBUG
            {   /* USB serial debug: ~5 lines/s */
                static uint32_t last_dbg;
                if (now - last_dbg >= 200) {
                    last_dbg = now;
                    int32_t mn[2] = {0x7FFFFFFF, 0x7FFFFFFF}, mx[2] = {-0x7FFFFFFF, -0x7FFFFFFF};
                    for (int i = 0; i < 512; i++) {
                        int c = i & 1;
                        if (s.wave[i] < mn[c]) mn[c] = s.wave[i];
                        if (s.wave[i] > mx[c]) mx[c] = s.wave[i];
                    }
                    printf("MIC L=%lu R=%lu L[%ld..%ld] R[%ld..%ld] floor=%lu pin=%u raw:",
                           (unsigned long)s.level_l, (unsigned long)s.level_r, (long)mn[0],
                           (long)mx[0], (long)mn[1], (long)mx[1], (unsigned long)mic_floor, s.data_pin);
                    for (int i = 0; i < 12; i++) printf(" %08lX", (unsigned long)s.raw[i]);
                    printf("\n");
                }
            }
#endif
            screen_mic(&s, mic_peak, mic_floor ? mic_floor : s.level, calib_left);
            break;
        }
        case MODE_WEATHER:
            if (en == EV_SHORT || ep == EV_SHORT) {
                probe_weather();
            } else if (now >= next_weather) {
                if (weather.found) weather_read(&weather);
                else weather_probe(&weather);
                next_weather = to_ms_since_boot(get_absolute_time()) + WEATHER_EVERY_MS;
            }
            screen_weather(&weather);
            break;
        default:
            break;
        }

        lcd_flush();
        uint32_t spent = to_ms_since_boot(get_absolute_time()) - frame_start;
        if (spent < FRAME_MS) sleep_ms(FRAME_MS - spent);
    }
}
