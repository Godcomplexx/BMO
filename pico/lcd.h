#pragma once
#include "gfx.h"

void lcd_init(void);
/* Push the whole gfx framebuffer to the panel. */
void lcd_flush(void);
