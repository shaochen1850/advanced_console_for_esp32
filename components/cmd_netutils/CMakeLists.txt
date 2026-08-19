idf_component_register(SRCS "cmd_netutils.c"
                    INCLUDE_DIRS .
                    PRIV_REQUIRES console
        esp_netif
        esp_wifi
        esp_http_client
        freertos
        esp-tls
        lwip)