idf_component_register(SRCS
    led_strip_encoder.c
    led_driver.c
    led_effects.c
    app_ws2812.c
    INCLUDE_DIRS "."
    REQUIRES esp_driver_rmt nvs_flash console
)