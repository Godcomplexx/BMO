#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "mic.h"
#include "weather.h"

/* floor = quiet-room level tracked by the caller; the screen shows dB above it. */
/* calib_ms_left > 0: the silence calibration is running (show a countdown). */
void screen_mic(const mic_stats_t *s, uint32_t peak, uint32_t floor, uint32_t calib_ms_left);
void screen_weather(const weather_t *w);
void screen_message(const char *line1, const char *line2);
