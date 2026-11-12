if(PT_LCD_TEST)
    idf_component_register(SRCS lcd_official_test.c INCLUDE_DIRS "."
                          REQUIRES esp_lcd esp_driver_spi esp_driver_i2c esp_driver_uart esp_driver_ledc esp_driver_gpio)
    return()
endif()

set(SRCS main.c director.c brightness.c batlog.c codec_port.c advisor_llm_esp.c battery_port_axp2101.c imu_port_qmi8658.c
         update_mode.c net_port_esp.c model_trailer.c sha256_port.c link_spike.c link_port_espnow.c link_dev.c)
# sd_spike.c (the tank switcher's step 2 test, `sd test`) is left out of 0.3.9.3 (docs/release-0.3.9.3.md D2): it comes
# back with the packages (step 4) - list it here and restore its three hooks in main.c / director.c
if(CONFIG_POCKET_TANK_SZPI)
    list(APPEND SRCS display_port_szpi.c board_szpi.c)
elseif(CONFIG_POCKET_TANK_DISPLAY_SH8601)
    list(APPEND SRCS display_port_sh8601.c)
else()
    list(APPEND SRCS display_port_stub.c)
endif()
if(CONFIG_POCKET_TANK_TOUCH_FT3168)
    list(APPEND SRCS touch_port_ft3168.c)
else()
    list(APPEND SRCS touch_port_stub.c)
endif()
if(CONFIG_POCKET_TANK_AUDIO_ES8311)
    list(APPEND SRCS audio_port_es8311.c)
else()
    list(APPEND SRCS audio_port_stub.c)
endif()
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(JSON_COMPONENT espressif__cjson)
    set(DMA_COMPONENT esp_driver_dma)
else()
    set(JSON_COMPONENT json)
endif()
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES tankcore llm esp_partition spi_flash esp_timer esp_driver_spi esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_driver_usb_serial_jtag esp_lcd nvs_flash esp_app_format
             esp_wifi esp_netif esp_event esp_http_client esp_https_ota app_update mbedtls bootloader_support fatfs sdmmc esp_driver_sdspi esp_driver_ledc esp_driver_uart ${JSON_COMPONENT} ${DMA_COMPONENT}
    EMBED_FILES "tokenizer.bin" "../../assets/sounds/sounds.bin"
)
# the board marker (net_port_esp.c, version.h PT_BOARD_MARKER_OFFSET): nothing
# references it - the download reads it back from flash - so it is kept by name
# or --gc-sections drops it (ESP-IDF's "custom structure" recipe, app_image_format)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u pt_board_marker")
