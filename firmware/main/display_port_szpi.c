/* LCKFB ST7789 landscape output. Preserve the upstream 448x368 world. */
#include "display_port.h"
#include "board_szpi.h"
#include "tank.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define STRIPE_ROWS 32
#define VIEW_W SZPI_LCD_WIDTH
#define VIEW_X ((SZPI_LCD_WIDTH - VIEW_W) / 2)
static const char *TAG = "szpi_lcd";
static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t panel_io;
static int panel_mode = 2, panel_clock_mhz = 80;
static SemaphoreHandle_t transfer_done;
static uint16_t *stripe;
static uint8_t s_brightness = 255;
static bool awake = true, first_frame = true, probe_pattern;
static int s_quarter;
static int64_t prof_wait, prof_send;

static bool transfer_complete(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event, void *arg) {
    (void)io; (void)event; (void)arg;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(transfer_done, &wake);
    return wake == pdTRUE;
}
static void backlight(uint8_t level) {
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, level * 1023u / 255));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}
static esp_err_t configure_panel(void) {
    ESP_RETURN_ON_ERROR(board_szpi_lcd_select(false), TAG, "LCD deselect failed");
    if (panel) { ESP_ERROR_CHECK(esp_lcd_panel_del(panel)); panel = NULL; }
    if (panel_io) { ESP_ERROR_CHECK(esp_lcd_panel_io_del(panel_io)); panel_io = NULL; }
    esp_lcd_panel_io_spi_config_t cfg = {
        .cs_gpio_num = -1, .dc_gpio_num = SZPI_LCD_DC, .spi_mode = panel_mode,
        .pclk_hz = panel_clock_mhz * 1000 * 1000, .trans_queue_depth = 10,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
        .on_color_trans_done = transfer_complete,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &cfg, &panel_io));
    esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = -1, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &dev, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_RETURN_ON_ERROR(board_szpi_lcd_select(true), TAG, "LCD select failed");
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    ESP_LOGI(TAG, "SPI mode %d, clock %d MHz, CS selected after SPI setup", panel_mode, panel_clock_mhz);
    return ESP_OK;
}

bool display_szpi_configure(int spi_mode, int clock_mhz) {
    if (spi_mode < 0 || spi_mode > 3 || clock_mhz < 1 || clock_mhz > 80) return false;
    backlight(0);
    panel_mode = spi_mode; panel_clock_mhz = clock_mhz;
    ESP_ERROR_CHECK(configure_panel());
    awake = true; first_frame = true;
    return true;
}

bool display_port_init(void) {
    if (!board_szpi_init()) return false;
    gpio_hold_dis(SZPI_LCD_BACKLIGHT);
    gpio_hold_dis(SZPI_LCD_MOSI);
    gpio_hold_dis(SZPI_LCD_SCLK);
    gpio_hold_dis(SZPI_LCD_DC);
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_1, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t light = {
        .gpio_num = SZPI_LCD_BACKLIGHT, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_1,
        .duty = 0, .flags.output_invert = true,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ESP_ERROR_CHECK(ledc_channel_config(&light));
    ESP_ERROR_CHECK(gpio_input_enable(SZPI_LCD_BACKLIGHT));
    transfer_done = xSemaphoreCreateBinary();
    stripe = heap_caps_malloc(2 * SZPI_LCD_WIDTH * STRIPE_ROWS * sizeof *stripe, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!stripe || !transfer_done) { ESP_LOGE(TAG, "LCD DMA allocation failed"); return false; }
    spi_bus_config_t bus = {
        .mosi_io_num = SZPI_LCD_MOSI, .miso_io_num = -1, .sclk_io_num = SZPI_LCD_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = SZPI_LCD_WIDTH * SZPI_LCD_HEIGHT * sizeof *stripe,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(configure_panel());
    ESP_LOGI(TAG, "ST7789 320x240, world %dx%d fitted to %dx240", TANK_W, TANK_H, VIEW_W);
    return true;
}
/* Text and fish are rasterized directly on this 320x240 sample grid. */
static inline uint16_t native_pixel(const uint16_t *fb, int x, int y) {
    if (s_quarter == 2) { x = SZPI_LCD_WIDTH - 1 - x; y = SZPI_LCD_HEIGHT - 1 - y; }
    return fb[(y * 3 / 2) * TANK_W + x * 3 / 2];
}
void display_port_flush(const uint16_t *fb) {
    if (!panel || !awake) return;
    bool in_flight = false;
    for (int top = 0; top < SZPI_LCD_HEIGHT; top += STRIPE_ROWS) {
        int rows = SZPI_LCD_HEIGHT - top;
        if (rows > STRIPE_ROWS) rows = STRIPE_ROWS;
        uint16_t *output = stripe + ((top / STRIPE_ROWS) & 1) * SZPI_LCD_WIDTH * STRIPE_ROWS;
        int64_t start = esp_timer_get_time();
        for (int row = 0; row < rows; row++) {
            for (int x = 0; x < SZPI_LCD_WIDTH; x++) {
                uint16_t pixel = native_pixel(fb, x, top + row);
                if (probe_pattern) {
                    static const uint16_t bars[] = {0x00f8, 0xe007, 0x1f00, 0xffff};
                    pixel = bars[x / 80];
                }
                output[row * SZPI_LCD_WIDTH + x] = pixel;
            }
        }
        prof_send += esp_timer_get_time() - start;
        if (in_flight) {
            start = esp_timer_get_time();
            xSemaphoreTake(transfer_done, portMAX_DELAY);
            prof_wait += esp_timer_get_time() - start;
        }
        start = esp_timer_get_time();
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, top, SZPI_LCD_WIDTH, top + rows, output));
        prof_send += esp_timer_get_time() - start;
        in_flight = true;
    }
    int64_t start = esp_timer_get_time();
    xSemaphoreTake(transfer_done, portMAX_DELAY); /* The final DMA owns its buffer until completion. */
    prof_wait += esp_timer_get_time() - start;
    if (first_frame) { first_frame = false; backlight(s_brightness); }
}
void display_port_flush_prof(int64_t *wait_us, int64_t *send_us) {
    *wait_us = prof_wait; *send_us = prof_send; prof_wait = prof_send = 0;
}
bool display_port_flush_async(const uint16_t *fb, void (*done)(const uint16_t *, void *), void *arg) {
    (void)fb; (void)done; (void)arg; return false;
}
void display_port_flush_wait(const uint16_t *fb) { (void)fb; }
void display_port_flush_async_prof(int64_t *task_us, int *frames, int64_t *copy_us) {
    *task_us = 0; *frames = 0; *copy_us = 0;
}
void display_port_set_brightness(uint8_t level) {
    s_brightness = level;
    if (panel && awake && !first_frame) backlight(level);
}
uint8_t display_port_brightness(void) { return s_brightness; }
void display_port_sleep(void) {
    if (!panel) return;
    backlight(0);
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, false));
    awake = false;
}
void display_port_wake(void) {
    if (!panel) return;
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    awake = true; first_frame = true;
}
bool board_is_v2(void) { return false; }
void board_sd_select(bool on) { (void)on; }
void display_port_set_inverted(bool inverted) { s_quarter = inverted ? 2 : 0; }
void display_port_set_quarter(int q) { s_quarter = q == 2 ? 2 : 0; }
bool display_port_quarters(void) { return false; }
void display_port_side_tune(int madctl, int gx, int gy) { (void)madctl; (void)gx; (void)gy; }
void display_port_side_info(int *madctl, int *gx, int *gy, bool *on) { *madctl = 0; *gx = *gy = 0; *on = false; }
void display_port_probe(bool on) {
    probe_pattern = on;
    ESP_LOGI(TAG, "colour bars %s, brightness=%u duty=%lu BL GPIO42=%d (low lights the backlight)",
             on ? "on" : "off", s_brightness,
             (unsigned long)ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0),
             gpio_get_level(SZPI_LCD_BACKLIGHT));
    board_szpi_dump();
    gpio_dump_io_configuration(stdout, (1ULL << SZPI_LCD_BACKLIGHT) | (1ULL << SZPI_LCD_MOSI) |
                               (1ULL << SZPI_LCD_SCLK) | (1ULL << SZPI_LCD_DC));
}

bool board_is_round(void) { return false; }
bool board_is_175(void) { return false; }
void display_port_deep_sleep_pins(bool tp_awake_high) { (void)tp_awake_high; }
void display_port_deep_standby(void) { }
void display_port_deep_sleep_bus(void) { }
bool board_has_expander(void) { return false; }
bool board_is_watch(void) { return false; }
bool board_is_square(void) { return false; }
const char *board_port_hw(void) { return "szpi-s3"; }
bool board_port_probed(void) { return true; }
void display_port_set_madctl(uint8_t v) { (void)v; }
uint8_t display_port_madctl(void) { return 0; }
int  board_pwr_sense_pin(void) { return -1; }
void display_port_frame_origin(int *px, int *py) { *px = 0; *py = 0; }
void display_port_set_view(int view) { (void)view; }
int  display_port_view(void) { return DISPLAY_VIEW_FIT; }
void display_port_panel_to_tank(int px, int py, float *tx, float *ty) {
    *tx = (px - VIEW_X) * (float)(TANK_W - 1) / (VIEW_W - 1);
    *ty = py * (float)(TANK_H - 1) / (SZPI_LCD_HEIGHT - 1);
}

