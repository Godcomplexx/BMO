#include "weather.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

typedef struct {
    i2c_inst_t *i2c;
    uint sda, scl;
} bus_t;

/* Free pins that form valid I2C pairs (GP4/5, 10-15, 18-21 are in use). */
static const bus_t kBuses[] = {
    {i2c0, 0, 1}, {i2c1, 6, 7}, {i2c0, 8, 9}, {i2c0, 16, 17}, {i2c1, 26, 27},
};
#define BUS_COUNT ((int)(sizeof(kBuses) / sizeof(kBuses[0])))
#define TIMEOUT_US 3000

static i2c_inst_t *g_i2c;

static void bus_open(const bus_t *b) {
    i2c_init(b->i2c, 100 * 1000);
    gpio_set_function(b->sda, GPIO_FUNC_I2C);
    gpio_set_function(b->scl, GPIO_FUNC_I2C);
    gpio_pull_up(b->sda);
    gpio_pull_up(b->scl);
}

static void bus_close(const bus_t *b) {
    i2c_deinit(b->i2c);
    gpio_init(b->sda);
    gpio_init(b->scl);
    gpio_disable_pulls(b->sda);
    gpio_disable_pulls(b->scl);
}

static bool sht_start(i2c_inst_t *i2c, uint8_t addr) {
    const uint8_t cmd[2] = {0x24, 0x00};  /* single shot, high repeatability */
    return i2c_write_timeout_us(i2c, addr, cmd, 2, false, TIMEOUT_US) == 2;
}

static uint8_t crc8(const uint8_t *d, int n) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

void weather_probe(weather_t *w) {
    if (w->found && g_i2c) {   /* release the previous bus before rescanning */
        for (int i = 0; i < BUS_COUNT; i++)
            if (kBuses[i].sda == w->sda) bus_close(&kBuses[i]);
    }
    *w = (weather_t){0};
    g_i2c = NULL;

    for (int i = 0; i < BUS_COUNT; i++) {
        const bus_t *b = &kBuses[i];
        bus_open(b);
        for (uint8_t addr = 0x44; addr <= 0x45; addr++) {
            if (sht_start(b->i2c, addr)) {
                w->found = true;
                w->addr = addr;
                w->sda = b->sda;
                w->scl = b->scl;
                g_i2c = b->i2c;
                return;                       /* keep this bus open */
            }
        }
        if (!w->other) {                      /* remember any other device */
            for (uint8_t a = 0x08; a < 0x78; a++) {
                uint8_t byte;
                if (i2c_read_timeout_us(b->i2c, a, &byte, 1, false, TIMEOUT_US) >= 0) {
                    w->other = true;
                    w->other_addr = a;
                    w->other_sda = b->sda;
                    w->other_scl = b->scl;
                    break;
                }
            }
        }
        bus_close(b);
    }
}

void weather_read(weather_t *w) {
    w->valid = false;
    if (!w->found || !g_i2c) return;
    if (!sht_start(g_i2c, w->addr)) return;
    sleep_ms(16);
    uint8_t d[6];
    if (i2c_read_timeout_us(g_i2c, w->addr, d, 6, false, TIMEOUT_US) != 6) return;
    if (crc8(d, 2) != d[2] || crc8(d + 3, 2) != d[5]) return;
    uint16_t rt = (uint16_t)((d[0] << 8) | d[1]);
    uint16_t rh = (uint16_t)((d[3] << 8) | d[4]);
    w->temp_c = -45.f + 175.f * (float)rt / 65535.f;
    w->hum = 100.f * (float)rh / 65535.f;
    w->valid = true;
}
