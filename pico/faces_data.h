#pragma once
#include <stdint.h>

#define FACE_BITMAP_COUNT 25
#define FACE_BG_R 192
#define FACE_BG_G 249
#define FACE_BG_B 222
extern const uint16_t *const face_bitmaps[FACE_BITMAP_COUNT];
extern const uint8_t face_eyes[FACE_BITMAP_COUNT][8];
extern const uint8_t face_wide[FACE_BITMAP_COUNT];
