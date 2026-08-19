/*
 * SPDX-FileCopyrightText: 2026 shaochen1850
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * led_effects.h - Provides various effects for the led.
 */
#pragma once
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    LED_MODE_OFF,
    LED_MODE_WHITE,
    LED_MODE_RAINBOW,
    LED_MODE_BREATH,
    LED_MODE_STROBE,
    LED_MODE_NIGHT
} led_mode_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_color_t;

esp_err_t led_effects_init(void);
void led_effects_set_mode(led_mode_t mode);
void led_effects_set_color(uint8_t r, uint8_t g, uint8_t b);
void led_effects_set_brightness(uint8_t brightness);
led_mode_t led_effects_get_mode(void);
led_color_t led_effects_get_color(void);
uint8_t  led_effects_get_brightness(void);