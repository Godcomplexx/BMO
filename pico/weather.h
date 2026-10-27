#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool found;            /* SHT31 answered */
    uint8_t addr;
    unsigned sda, scl;
    float temp_c, hum;
    bool valid;            /* last read OK */
    /* Some other I2C device seen while the SHT31 was missing. */
    bool other;
    uint8_t other_addr;
    unsigned other_sda, other_scl;
} weather_t;

/* Tries several I2C pin pairs for an SHT31 (0x44 / 0x45). */
void weather_probe(weather_t *w);
/* Blocks ~20 ms. Updates temp/hum, sets w->valid. */
void weather_read(weather_t *w);

