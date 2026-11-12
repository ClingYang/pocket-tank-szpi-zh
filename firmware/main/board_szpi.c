#include "board_szpi.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "szpi";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t expander;
static SemaphoreHandle_t expander_lock;
static uint8_t outputs = 0x05; /* LCD deselected until SPI is ready, amp off, camera powered down. */

i2c_master_bus_handle_t board_i2c_bus(void) { return bus; }

bool board_szpi_init(void) {
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SZPI_I2C_SDA,
        .scl_io_num = SZPI_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_FALSE(i2c_new_master_bus(&cfg, &bus) == ESP_OK, false, TAG, "I2C init failed");
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x19,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_FALSE(i2c_master_bus_add_device(bus, &dev, &expander) == ESP_OK, false, TAG, "PCA9557 init failed");
    expander_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(expander_lock, false, TAG, "PCA9557 mutex allocation failed");
    const uint8_t initial[] = {0x01, outputs};
    const uint8_t direction[] = {0x03, 0xf8};
    ESP_RETURN_ON_FALSE(i2c_master_transmit(expander, initial, sizeof initial, 100) == ESP_OK,
                        false, TAG, "PCA9557 output failed");
    ESP_RETURN_ON_FALSE(i2c_master_transmit(expander, direction, sizeof direction, 100) == ESP_OK,
                        false, TAG, "PCA9557 direction failed");
    return true;
}

static esp_err_t set_output(uint8_t mask, bool high) {
    if (!expander || !expander_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(expander_lock, portMAX_DELAY);
    uint8_t next = high ? outputs | mask : outputs & ~mask;
    const uint8_t command[] = {0x01, next};
    esp_err_t result = i2c_master_transmit(expander, command, sizeof command, 100);
    if (result == ESP_OK) outputs = next;
    else ESP_LOGE(TAG, "PCA9557 output write failed");
    xSemaphoreGive(expander_lock);
    return result;
}

void board_szpi_amp(bool on) {
    set_output(0x02, on);
}

esp_err_t board_szpi_lcd_select(bool selected) {
    return set_output(0x01, !selected);
}

void board_szpi_dump(void) {
    if (!expander || !expander_lock) return;
    xSemaphoreTake(expander_lock, portMAX_DELAY);
    uint8_t values[4] = {0};
    esp_err_t result = ESP_OK;
    for (uint8_t reg = 0; reg < sizeof values && result == ESP_OK; reg++) {
        result = i2c_master_transmit_receive(expander, &reg, 1, &values[reg], 1, 100);
    }
    ESP_LOGI(TAG, "PCA9557 read %s: input=%02x output=%02x polarity=%02x direction=%02x",
             esp_err_to_name(result), values[0], values[1], values[2], values[3]);
    xSemaphoreGive(expander_lock);
}
