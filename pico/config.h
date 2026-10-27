#pragma once

/* ---- ST7735 display (SPI0) ---- */
#define LCD_SPI      spi0
#define LCD_SCK      18
#define LCD_MOSI     19
#define LCD_DC       20
#define LCD_RST      21
#define LCD_SPI_HZ   (24u * 1000u * 1000u)

/* Landscape 160x128. If the picture is mirrored or upside down, change
 * LCD_MADCTL: 0x60, 0xA0, 0xC0 or 0x00|0x60 variants. If red/blue are swapped,
 * toggle the 0x08 (BGR) bit. If colours look inverted, set LCD_INVERT to 1.
 * If a 1-2 px stripe appears on an edge, set LCD_X_OFFSET / LCD_Y_OFFSET. */
#define LCD_MADCTL   0xA0
#define LCD_INVERT   0
#define LCD_X_OFFSET 0
#define LCD_Y_OFFSET 0

/* ---- Buttons (to GND, internal pull-up) ---- */
#define BTN_NEXT     14
#define BTN_PREV     15

/* ---- I2S microphone INMP441 (shares clocks with the MAX98357A) ---- */
#define MIC_BCLK     2    /* BCLK; WS must be the next pin (GP3) */
#define MIC_WS       3
#define MIC_AMP_DIN  4    /* amplifier data, held low during the mic test */
#define MIC_DATA     4    /* INMP441 SD: wired to physical pin 6 = GP4 (pin 7 = GP5 is also tried) */

/* ---- Long press (ms) switches mode: FACES -> MIC -> WEATHER ---- */
#define LONG_PRESS_MS 700

/* 1 = print mic statistics on the USB serial port (COM) 5x per second */
#define MIC_SERIAL_DEBUG 0
