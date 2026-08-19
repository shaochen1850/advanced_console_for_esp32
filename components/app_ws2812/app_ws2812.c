/*
 * SPDX-FileCopyrightText: 2026 shaochen1850
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * app_ws2812.c - Command implementation for controlling the LED.
 */

#include "app_ws2812.h"
#include "led_effects.h"
#include "esp_console.h"
#include "esp_log.h"

static const char *TAG = "LED_CMD";

static int led_cmd(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: led <subcommand> [args]\n"
               "Subcommands:\n"
               "  on                Turn on (white mode)\n"
               "  off               Turn off\n"
               "  mode <m>          Set mode: off,white,rainbow,breath,strobe,night\n"
               "  color <r> <g> <b> Set RGB color (0-255)\n"
               "  bright <0-255>    Set brightness\n"
               "  status            Show current state\n");
        return 0;
    }

    const char *sub = argv[1];

    if (strcmp(sub, "on") == 0) {
        led_effects_set_mode(LED_MODE_WHITE);
        printf("LED → ON\n");

    } else if (strcmp(sub, "off") == 0) {
        led_effects_set_mode(LED_MODE_OFF);
        printf("LED → OFF\n");

    } else if (strcmp(sub, "mode") == 0) {
        if (argc < 3) { printf("Need mode name\nAvailable modes: off, white, rainbow, breath, strobe, night\n"); return 1; }
        const char *m = argv[2];
        if      (strcmp(m, "off")     == 0) led_effects_set_mode(LED_MODE_OFF);
        else if (strcmp(m, "white")   == 0) led_effects_set_mode(LED_MODE_WHITE);
        else if (strcmp(m, "rainbow") == 0) led_effects_set_mode(LED_MODE_RAINBOW);
        else if (strcmp(m, "breath")  == 0) led_effects_set_mode(LED_MODE_BREATH);
        else if (strcmp(m, "strobe")  == 0) led_effects_set_mode(LED_MODE_STROBE);
        else if (strcmp(m, "night")   == 0) led_effects_set_mode(LED_MODE_NIGHT);
        else { printf("Unknown mode: %s\n", m); return 1; }
        printf("LED mode → %s\n", m);

    } else if (strcmp(sub, "color") == 0) {
        if (argc < 5) { printf("Usage: led color <r> <g> <b>\n"); return 1; }
        int r = atoi(argv[2]), g = atoi(argv[3]), b = atoi(argv[4]);
        if (r<0||r>255||g<0||g>255||b<0||b>255) { printf("0-255 only\n"); return 1; }
        led_effects_set_color((uint8_t)r, (uint8_t)g, (uint8_t)b);
        printf("LED color → R=%d G=%d B=%d\n", r, g, b);

    } else if (strcmp(sub, "bright") == 0 || strcmp(sub, "brightness") == 0) {
        if (argc < 3) { printf("Usage: led bright <0-255>\n"); return 1; }
        int v = atoi(argv[2]);
        if (v<0||v>255) { printf("0-255 only\n"); return 1; }
        led_effects_set_brightness((uint8_t)v);
        printf("LED brightness → %d\n", v);

    } else if (strcmp(sub, "status") == 0) {
        led_mode_t   mode = led_effects_get_mode();
        led_color_t  col  = led_effects_get_color();
        uint8_t      bri  = led_effects_get_brightness();
        const char *ms;
        switch (mode) {
            case LED_MODE_OFF:     ms = "off";     break;
            case LED_MODE_WHITE:   ms = "white";   break;
            case LED_MODE_RAINBOW: ms = "rainbow"; break;
            case LED_MODE_BREATH:  ms = "breath";  break;
            case LED_MODE_STROBE:  ms = "strobe";  break;
            case LED_MODE_NIGHT:   ms = "night";   break;
            default:               ms = "?";       break;
        }
        printf("Mode:       %s\n"
               "Color:      R=%d G=%d B=%d\n"
               "Brightness: %d/255\n",
               ms, col.r, col.g, col.b, bri);

    } else {
        printf("Unknown subcommand: %s\n", sub);
        return 1;
    }
    return 0;
}

void app_ws2812_register(void) {
    const esp_console_cmd_t led_def = {
        .command = "led",
        .help = "Control WS2812 LED: on, off, mode, color, bright, status",
        .func = &led_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&led_def));
    ESP_LOGI(TAG, "LED commands registered");
}