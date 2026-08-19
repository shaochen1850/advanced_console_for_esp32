idf_component_register(SRCS "cmd_system.c" "cmd_system_common.c" "cmd_edit.c" "cmd_head_tail.c" "cmd_watch.c"
                    INCLUDE_DIRS .
                    REQUIRES console spi_flash esp_driver_uart esp_driver_gpio esp_timer esp_psram heap cmd_fs)

if(CONFIG_SOC_DEEP_SLEEP_SUPPORTED OR CONFIG_SOC_LIGHT_SLEEP_SUPPORTED)
    target_sources(${COMPONENT_LIB} PRIVATE cmd_system_sleep.c)
endif()
