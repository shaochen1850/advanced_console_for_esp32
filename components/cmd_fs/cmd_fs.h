/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 shaochen1850
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/* Console example — declarations of File System command registration functions.

 * The original example code is in the Public Domain (or CC0 licensed, at your option.)
*/


#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Register NETUTILS functions
void register_fs(void);
bool resolve_path(const char *input, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
