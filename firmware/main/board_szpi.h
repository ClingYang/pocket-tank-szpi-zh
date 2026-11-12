#pragma once

#include <stdbool.h>
#include "driver/i2c_master.h"

/* Official LCKFB beginner LCD, LVGL and ES8311 examples. */
#define SZPI_LCD_WIDTH 320
#define SZPI_LCD_HEIGHT 240
#define SZPI_LCD_MOSI 40
#define SZPI_LCD_SCLK 41
#define SZPI_LCD_DC 39
#define SZPI_LCD_BACKLIGHT 42
#define SZPI_I2C_SDA 1
#define SZPI_I2C_SCL 2
#define SZPI_I2S_MCLK 38
#define SZPI_I2S_BCLK 14
#define SZPI_I2S_WS 13
#define SZPI_I2S_DOUT 45

bool board_szpi_init(void);
i2c_master_bus_handle_t board_i2c_bus(void);
void board_szpi_amp(bool on);
void board_szpi_dump(void);
esp_err_t board_szpi_lcd_select(bool selected);
bool display_szpi_configure(int spi_mode, int clock_mhz);
