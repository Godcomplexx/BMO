#include "lcd.h"

#include "config.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

static void cmd(uint8_t v) {
    gpio_put(LCD_DC, 0);
    spi_write_blocking(LCD_SPI, &v, 1);
}

static void data(const uint8_t *d, size_t n) {
    gpio_put(LCD_DC, 1);
    spi_write_blocking(LCD_SPI, d, n);
}

static void cmd1(uint8_t c, uint8_t v) {
    cmd(c);
    data(&v, 1);
}

void lcd_init(void) {
    spi_init(LCD_SPI, LCD_SPI_HZ);
    gpio_set_function(LCD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_MOSI, GPIO_FUNC_SPI);
    gpio_init(LCD_DC);  gpio_set_dir(LCD_DC, GPIO_OUT);
    gpio_init(LCD_RST); gpio_set_dir(LCD_RST, GPIO_OUT);

    gpio_put(LCD_RST, 1); sleep_ms(10);
    gpio_put(LCD_RST, 0); sleep_ms(20);
    gpio_put(LCD_RST, 1); sleep_ms(120);

    cmd(0x01); sleep_ms(150);          /* software reset */
    cmd(0x11); sleep_ms(500);          /* sleep out */
    cmd1(0x3A, 0x05);                  /* 16-bit colour */
    cmd1(0x36, LCD_MADCTL);            /* orientation */
    cmd(LCD_INVERT ? 0x21 : 0x20);
    cmd(0x13);                         /* normal mode */
    cmd(0x29); sleep_ms(100);          /* display on */
}

void lcd_flush(void) {
    uint8_t col[] = {0, LCD_X_OFFSET, 0, LCD_X_OFFSET + SCR_W - 1};
    uint8_t row[] = {0, LCD_Y_OFFSET, 0, LCD_Y_OFFSET + SCR_H - 1};
    cmd(0x2A); data(col, 4);
    cmd(0x2B); data(row, 4);
    cmd(0x2C);
    data((const uint8_t *)gfx_fb(), SCR_W * SCR_H * sizeof(color_t));
}
