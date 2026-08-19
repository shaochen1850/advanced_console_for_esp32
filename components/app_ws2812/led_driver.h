/*
 * SPDX-FileCopyrightText: 2026 shaochen1850
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * led_driver.h - LED driver implementation for the console.
 */
#pragma once
#include <stdint.h>
#include "esp_err.h"

esp_err_t led_driver_init(int gpio_num);
esp_err_t led_driver_send(const uint8_t *pixels, size_t len);