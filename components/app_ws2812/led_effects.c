/*
 * SPDX-FileCopyrightText: 2026 shaochen1850
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * led_effects.c - Provides various effects for the led.
 */
#include "led_effects.h"
#include "led_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "LED_FX";
static const char *NVS_NS = "led_store";

// --- Mutex lock protected global status ---
static SemaphoreHandle_t state_lock = NULL;
static led_mode_t   g_mode       = LED_MODE_OFF;
static led_color_t  g_color      = {102, 204, 255};
static uint8_t      g_brightness = 128;

static TaskHandle_t g_task = NULL;

// ---------- HSV to RGB ----------
static void hsv2rgb(uint32_t h, uint32_t s, uint32_t v,
                    uint32_t *r, uint32_t *g, uint32_t *b) {
    h %= 360;
    uint32_t max = v * 2.55f;
    uint32_t min = max * (100 - s) / 100.0f;
    uint32_t i = h / 60, diff = h % 60;
    uint32_t adj = (max - min) * diff / 60;
    switch (i) {
        case 0: *r = max; *g = min + adj; *b = min; break;
        case 1: *r = max - adj; *g = max; *b = min; break;
        case 2: *r = min; *g = max; *b = min + adj; break;
        case 3: *r = min; *g = max - adj; *b = max; break;
        case 4: *r = min + adj; *g = min; *b = max; break;
        default:*r = max; *g = min; *b = max - adj; break;
    }
}

// ---------- NVS Memorization ----------
static void load_state(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v;
    if (nvs_get_u8(h, "mode", &v) == ESP_OK) g_mode = (led_mode_t)v;
    if (nvs_get_u8(h, "bright", &v) == ESP_OK) g_brightness = v;
    if (nvs_get_u8(h, "r", &v) == ESP_OK) g_color.r = v;
    if (nvs_get_u8(h, "g", &v) == ESP_OK) g_color.g = v;
    if (nvs_get_u8(h, "b", &v) == ESP_OK) g_color.b = v;
    nvs_close(h);
    ESP_LOGI(TAG, "loaded: mode=%d bright=%d rgb=(%d,%d,%d)",
             g_mode, g_brightness, g_color.r, g_color.g, g_color.b);
}

static void save_state(void) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "mode",    (uint8_t)g_mode);
    nvs_set_u8(h, "bright",  g_brightness);
    nvs_set_u8(h, "r",       g_color.r);
    nvs_set_u8(h, "g",       g_color.g);
    nvs_set_u8(h, "b",       g_color.b);
    nvs_commit(h);
    nvs_close(h);
}

// ---------- Effect task (runs independent, 20ms/frame) ----------
static void effects_task(void *arg) {
    uint8_t px[3] = {0, 0, 0};   // [G, R, B]  WS2812 
    uint16_t hue = 0;
    uint32_t last_change = 0;
    srand(time(NULL));

    // Load last state from NVS
    xSemaphoreTake(state_lock, portMAX_DELAY);
    load_state();
    xSemaphoreGive(state_lock);

    while (1) {
        uint32_t now = esp_log_timestamp();

        // Snapshot current settings
        xSemaphoreTake(state_lock, portMAX_DELAY);
        led_mode_t   mode  = g_mode;
        led_color_t  color = g_color;
        uint8_t      bright = g_brightness;
        xSemaphoreGive(state_lock);

        // --- calculate pixels according to mode ---
        switch (mode) {
        case LED_MODE_OFF:
            px[0] = px[1] = px[2] = 0;
            break;

        case LED_MODE_WHITE:
            px[0] = (color.g * bright) / 255;
            px[1] = (color.r * bright) / 255;
            px[2] = (color.b * bright) / 255;
            break;

        case LED_MODE_BREATH: {
            float a = (now % 6000) / 6000.0f * M_PI * 2;
            uint8_t v = (uint8_t)(127.5f + 127.5f * sinf(a));
            px[0] = (color.g * v * bright) / (255 * 255);
            px[1] = (color.r * v * bright) / (255 * 255);
            px[2] = (color.b * v * bright) / (255 * 255);
            break;
        }

        case LED_MODE_STROBE:
            if ((now % 500) < 100) {
                px[0] = (color.g * bright) / 255;
                px[1] = (color.r * bright) / 255;
                px[2] = (color.b * bright) / 255;
            } else {
                px[0] = px[1] = px[2] = 0;
            }
            break;

        case LED_MODE_RAINBOW:
            if (now - last_change > 50) {
                hue = (hue + 1) % 360;
                last_change = now;
            }
            uint32_t rr, gg, bb;
            hsv2rgb(hue, 100, 100, &rr, &gg, &bb);
            px[0] = (gg * bright) / 255;
            px[1] = (rr * bright) / 255;
            px[2] = (bb * bright) / 255;
            break;

        case LED_MODE_NIGHT:
            if (rand() % 100 < 5) {         // 5% chance to light up a star
                uint16_t rh = rand() % 360;
                hsv2rgb(rh, 100, 100, &rr, &gg, &bb);
                px[0] = (gg * bright) / 255;
                px[1] = (rr * bright) / 255;
                px[2] = (bb * bright) / 255;
            }
            // Otherwise keep the px value from the previous frame (star fade-out effect)
            break;
        }

        led_driver_send(px, sizeof(px));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ---------- Open API ----------
esp_err_t led_effects_init(void) {
    state_lock = xSemaphoreCreateMutex();
    if (!state_lock) return ESP_ERR_NO_MEM;

    BaseType_t ret = xTaskCreate(effects_task, "led_fx", 4096, NULL, 5, &g_task);
    if (ret != pdPASS) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "effects task started");
    return ESP_OK;
}

void led_effects_set_mode(led_mode_t mode) {
    xSemaphoreTake(state_lock, portMAX_DELAY);
    g_mode = mode;
    save_state();
    xSemaphoreGive(state_lock);
}

void led_effects_set_color(uint8_t r, uint8_t g, uint8_t b) {
    xSemaphoreTake(state_lock, portMAX_DELAY);
    g_color.r = r; g_color.g = g; g_color.b = b;
    save_state();
    xSemaphoreGive(state_lock);
}

void led_effects_set_brightness(uint8_t brightness) {
    xSemaphoreTake(state_lock, portMAX_DELAY);
    g_brightness = brightness;
    save_state();
    xSemaphoreGive(state_lock);
}

led_mode_t led_effects_get_mode(void) {
    led_mode_t m;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    m = g_mode;
    xSemaphoreGive(state_lock);
    return m;
}

led_color_t led_effects_get_color(void) {
    led_color_t c;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    c = g_color;
    xSemaphoreGive(state_lock);
    return c;
}

uint8_t led_effects_get_brightness(void) {
    uint8_t b;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    b = g_brightness;
    xSemaphoreGive(state_lock);
    return b;
}