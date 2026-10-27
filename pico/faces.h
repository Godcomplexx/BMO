#pragma once
#include <stdint.h>

#define FACE_COUNT 25

/*
 * Draws face `id` (a pixel-exact Figma bitmap) with cartoon-style motion:
 *   t_ms      : free-running time, drives the idle motion of that face
 *   squash    : 1 = normal .. 0 = squashed flat (used for face changes)
 *   blink     : 1 = eyes open .. 0 = closed (only faces with detected eyes)
 *   since_swap: ms since this face appeared (spring "pop" on arrival)
 */
void face_draw(int id, uint32_t t_ms, float squash, float blink, uint32_t since_swap);
