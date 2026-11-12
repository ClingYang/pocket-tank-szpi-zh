/* LCKFB SZPI chapter 9 LCD bring-up, preserving its pin map and init order.
 * https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/lcd-display.html
 * I2C uses the ESP-IDF 6 master API. A DMA completion semaphore keeps the
 * official example's line buffer alive until each transfer completes.
 */
#include <assert.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define BSP_LCD_H_RES 320
#define BSP_LCD_V_RES 240
#define BSP_LCD_SPI_NUM SPI3_HOST
#define BSP_LCD_SPI_MOSI GPIO_NUM_40
#define BSP_LCD_SPI_CLK GPIO_NUM_41
#define BSP_LCD_DC GPIO_NUM_39
#define BSP_LCD_BACKLIGHT GPIO_NUM_42
#define BSP_LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)

static const char *TAG = "official_lcd_test";
static i2c_master_dev_handle_t expander;
static esp_lcd_panel_handle_t panel_handle;
static SemaphoreHandle_t transfer_done;
static uint16_t *line_buffer;

static void pca9557_write(uint8_t reg, uint8_t value) {
    const uint8_t bytes[] = {reg, value};
    ESP_ERROR_CHECK(i2c_master_transmit(expander, bytes, sizeof bytes, 100));
}

static void bsp_i2c_init(void) {
    i2c_master_bus_handle_t bus;
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_1,
        .scl_io_num = GPIO_NUM_2,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&config, &bus));
    const i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x19,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &device, &expander));
}

static void pca9557_init(void) {
    pca9557_write(0x01, 0x05); /* Camera off, amplifier off, LCD deselected. */
    pca9557_write(0x03, 0xf8);
}

static void lcd_cs(uint8_t level) {
    pca9557_write(0x01, level ? 0x05 : 0x04);
}

static void bsp_display_brightness_init(void) {
    const ledc_channel_config_t channel = {
        .gpio_num = BSP_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = true,
    };
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static bool lcd_transfer_done(esp_lcd_panel_io_handle_t io,
                              esp_lcd_panel_io_event_data_t *event, void *context) {
    (void)io; (void)event; (void)context;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(transfer_done, &wake);
    return wake == pdTRUE;
}

static void bsp_display_new(void) {
    bsp_display_brightness_init();
    const spi_bus_config_t bus = {
        .sclk_io_num = BSP_LCD_SPI_CLK,
        .mosi_io_num = BSP_LCD_SPI_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BSP_LCD_SPI_NUM, &bus, SPI_DMA_CH_AUTO));
    const esp_lcd_panel_io_spi_config_t config = {
        .dc_gpio_num = BSP_LCD_DC,
        .cs_gpio_num = -1,
        .pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 2,
        .trans_queue_depth = 10,
        .on_color_trans_done = lcd_transfer_done,
    };
    esp_lcd_panel_io_handle_t io;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(BSP_LCD_SPI_NUM, &config, &io));
    const esp_lcd_panel_dev_config_t panel = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel, &panel_handle));
    /* Chapter 9 performs the reset call before selecting the LCD. */
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    lcd_cs(0);
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
}

static void lcd_set_color(uint16_t color, bool bars) {
    const uint16_t colours[] = {0xf800, 0x07e0, 0x001f, 0xffff};
    for (int x = 0; x < BSP_LCD_H_RES; x++) {
        uint16_t pixel = bars ? colours[x / 80] : color;
        line_buffer[x] = (pixel << 8) | (pixel >> 8);
    }
    for (int y = 0; y < BSP_LCD_V_RES; y++) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, y, BSP_LCD_H_RES, y + 1, line_buffer));
        xSemaphoreTake(transfer_done, portMAX_DELAY);
    }
    ESP_LOGI(TAG, "draw complete: %s, RGB565=%04x", bars ? "four colour bars" : "solid colour", color);
}

void app_main(void) {
    ESP_LOGI(TAG, "LCKFB official chapter 9 LCD test, SPI3 mode 2 at 80 MHz, amplifier off");
    const gpio_num_t pins[] = {BSP_LCD_DC, BSP_LCD_SPI_MOSI, BSP_LCD_SPI_CLK, BSP_LCD_BACKLIGHT};
    gpio_deep_sleep_hold_dis();
    for (size_t i = 0; i < sizeof pins / sizeof pins[0]; i++) gpio_hold_dis(pins[i]);
    transfer_done = xSemaphoreCreateBinary();
    line_buffer = heap_caps_malloc(BSP_LCD_H_RES * sizeof *line_buffer, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(transfer_done && line_buffer);
    bsp_i2c_init();
    pca9557_init();
    bsp_display_new();
    lcd_set_color(0, false);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 1023));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
    lcd_set_color(0, true);
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0));
    ESP_LOGI(TAG, "UART commands r/g/b/w/k/p show red/green/blue/white/black/bars");
    for (;;) {
        uint8_t command;
        if (uart_read_bytes(UART_NUM_0, &command, 1, pdMS_TO_TICKS(1000)) != 1) continue;
        switch (command) {
        case 'r': lcd_set_color(0xf800, false); break;
        case 'g': lcd_set_color(0x07e0, false); break;
        case 'b': lcd_set_color(0x001f, false); break;
        case 'w': lcd_set_color(0xffff, false); break;
        case 'k': lcd_set_color(0, false); break;
        case 'p': lcd_set_color(0, true); break;
        default: break;
        }
    }
}
