/*
 * SPDX-FileCopyrightText: 2026 shaochen1850
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * led_driver.c - LED driver implementation for the console.
 */
#include "led_driver.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"   // original encoder component
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"    // portMAX_DELAY here

static const char *TAG = "LED_DRV";
static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_handle_t led_encoder = NULL;

esp_err_t led_driver_init(int gpio_num) {
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = gpio_num,
        .mem_block_symbols = 64,
        .resolution_hz = 10000000,   // 10MHz
        .trans_queue_depth = 4,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_chan_config, &led_chan), TAG, "create TX ch");

    led_strip_encoder_config_t encoder_config = {
        .resolution = 10000000,
    };
    ESP_RETURN_ON_ERROR(rmt_new_led_strip_encoder(&encoder_config, &led_encoder), TAG, "create enc");
    ESP_RETURN_ON_ERROR(rmt_enable(led_chan), TAG, "enable ch");

    ESP_LOGI(TAG, "LED driver init OK, GPIO=%d", gpio_num);
    return ESP_OK;
}

esp_err_t led_driver_send(const uint8_t *pixels, size_t len) {
    rmt_transmit_config_t tx_config = { .loop_count = 0 };
    ESP_RETURN_ON_ERROR(rmt_transmit(led_chan, led_encoder, pixels, len, &tx_config), TAG, "tx");
    ESP_RETURN_ON_ERROR(rmt_tx_wait_all_done(led_chan, portMAX_DELAY), TAG, "wait");
    return ESP_OK;
}