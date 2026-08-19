/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 shaochen1850
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/* Console example — various system commands and modified by shaochen1850

 * The original example code is in the Public Domain (or CC0 licensed, at your option.)
*/

//Standard C library includes
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
//Espressif includes
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_fat.h"
//Linenoise and argtable3 includes
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
//NVS includes
#include "nvs.h"
#include "nvs_flash.h"
//soc includes
#include "soc/soc_caps.h"
//Freertos includes
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//cmd and app includes
//Remember to add the header files here if add a new cmd_xxx
#include "cmd_system.h"
#include "cmd_wifi.h"
#include "cmd_nvs.h"
#include "cmd_ble.h"
#include "cmd_netutils.h"
#include "cmd_fs.h"
#include "cmd_sysmgmt.h"
//App ws2812 includes
#include "led_driver.h"
#include "led_effects.h"
#include "app_ws2812.h"
/*managed components*/
#include "console_ping.h"
#include "console_settings.h"

/*
 * We warn if a secondary serial console is enabled. A secondary serial console is always output-only and
 * hence not very useful for interactive console applications. If you encounter this warning, consider disabling
 * the secondary serial console in menuconfig unless you know what you are doing.
 */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#if !CONFIG_ESP_CONSOLE_SECONDARY_NONE
#warning "A secondary serial console is not useful when using the console component. Please disable it in menuconfig."
#endif
#endif

static const char* TAG = "example";
#define PROMPT_STR CONFIG_IDF_TARGET

/* Console command history can be stored to and loaded from a file.
 * The easiest way to do this is to use FATFS filesystem on top of
 * wear_levelling library.
 */
#if CONFIG_CONSOLE_STORE_HISTORY

#define MOUNT_PATH "/data"
#define HISTORY_PATH MOUNT_PATH "/history.txt"

static void initialize_filesystem(void)
{
    static wl_handle_t wl_handle;
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 4,
            .format_if_mount_failed = true
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_PATH, "storage", &mount_config, &wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }
}
#else
#define HISTORY_PATH NULL
#endif // CONFIG_CONSOLE_STORE_HISTORY

//Support for command line syntax: ;  &  #  and | (pipe) parsing.
/* ── helpers ─────────────────────────────────────────── */

static char *trim(char *s) {
    while (*s == ' ') s++;
    char *e = s + strlen(s) - 1;
    while (e > s && *e == ' ') e--;
    *(e + 1) = '\0';
    return s;
}

static void cleanup_trailing_spaces(char *s) {
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ') {
        s[len - 1] = '\0';
        len--;
    }
}

/* ── background task wrapper ─────────────────────────── */

static void bg_task(void *arg) {
    char *cmd = (char *)arg;
    int ret;
    esp_err_t err = esp_console_run(cmd, &ret);
    if (err != ESP_OK) {
        printf("[bg] %s: error %s\n", cmd, esp_err_to_name(err));
    }
    free(cmd);
    vTaskDelete(NULL);
}

/* ── run a single command (handle | ) ────────────────── */

#define PIPE_TEMP_FILE "/data/.pipe"

static int run_single_command(const char *cmd) {
    char *buf = strdup(cmd);
    if (!buf) return -1;

    /* look for | (first occurrence) */
    char *pipe_pos = strstr(buf, "|");
    if (!pipe_pos) {
        /* no pipe — normal run */
        int ret;
        esp_err_t err = esp_console_run(buf, &ret);
        free(buf);
        if (err == ESP_ERR_NOT_FOUND) return -2;
        if (err == ESP_ERR_INVALID_ARG) return 0;
        if (err != ESP_OK) {
            printf("Error: %s\n", esp_err_to_name(err));
            return -1;
        }
        return ret;
    }

    /* ── pipe ── */
    *pipe_pos = '\0';
    char *left  = trim(buf);
    char *right = trim(pipe_pos + 1);

    if (!*left || !*right) {
        printf("Syntax error: empty pipe segment\n");
        free(buf);
        return -1;
    }

    /* 1. check that /data exists */
    struct stat st;
    if (stat("/data", &st) != 0 || !S_ISDIR(st.st_mode)) {
        printf("Pipe error: /data not mounted. Enable CONFIG_CONSOLE_STORE_HISTORY or mount a FATFS.\n");
        free(buf);
        return -1;
    }

    /* 2. run LEFT command, stdout → temp file */
    FILE *save_stdout = stdout;
    stdout = freopen(PIPE_TEMP_FILE, "w", stdout);
    if (!stdout) {
        printf("Pipe error: cannot write temp file\n");
        stdout = save_stdout;
        free(buf);
        return -1;
    }

    int ret_left;
    esp_console_run(left, &ret_left);

    fflush(stdout);
    fclose(stdout);
    stdout = save_stdout;

    /* 3. run RIGHT command, stdin ← temp file */
    FILE *save_stdin = stdin;
    stdin = freopen(PIPE_TEMP_FILE, "r", stdin);
    if (!stdin) {
        printf("Pipe error: cannot read temp file\n");
        stdin = save_stdin;
        unlink(PIPE_TEMP_FILE);
        free(buf);
        return -1;
    }

    int ret_right;
    esp_console_run(right, &ret_right);

    fclose(stdin);
    stdin = save_stdin;

    /* 4. clean up */
    unlink(PIPE_TEMP_FILE);

    free(buf);
    return ret_right;
}

/* ── Important: process a full command line (;  &  #) ───────────── */

int shell_run_line(const char *line) {
    char *copy = strdup(line);
    if (!copy) return -1;

    /* ① strip comment # */
    char *hash = strchr(copy, '#');
    if (hash) *hash = '\0';

    /* ② split by ; */
    char *saveptr;
    char *segment = strtok_r(copy, ";", &saveptr);
    int final_ret = 0;

    while (segment) {
        segment = trim(segment);
        if (*segment == '\0') {
            segment = strtok_r(NULL, ";", &saveptr);
            continue;
        }

        /* ③ background & */
        int bg = 0;
        cleanup_trailing_spaces(segment);
        size_t len = strlen(segment);
        if (len > 0 && segment[len - 1] == '&') {
            bg = 1;
            segment[len - 1] = '\0';
            cleanup_trailing_spaces(segment);
        }

        if (bg) {
            char *cmd_copy = strdup(segment);
            if (cmd_copy) {
                BaseType_t x = xTaskCreate(bg_task, "bgcmd", 4608, cmd_copy, 1, NULL);
                if (x == pdPASS) {
                    printf("[bg] %s &\n", cmd_copy);
                } else {
                    printf("[bg] Failed to create task for: %s\n", cmd_copy);
                    free(cmd_copy);
                }
            }
        } else {
            int ret = run_single_command(segment);
            if (ret == -2) {
                /* command not found — report once */
                printf("Unrecognized command: %s\n", segment);
            }
            final_ret = ret;
        }

        segment = strtok_r(NULL, ";", &saveptr);
    }

    free(copy);
    return final_ret;
}

static void initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    initialize_nvs();

#if CONFIG_CONSOLE_STORE_HISTORY
    initialize_filesystem();
    ESP_LOGI(TAG, "Command history enabled");
#else
    ESP_LOGI(TAG, "Command history disabled");
#endif

    /* Initialize console output periheral (UART, USB_OTG, USB_JTAG) */
    initialize_console_peripheral();

    /* Initialize linenoise library and esp_console*/
    initialize_console_library(HISTORY_PATH);

    /* Initialize WS2812 LED driver and effects */
    ESP_ERROR_CHECK(led_driver_init(48));     // Mofify GPIO number here if you use a different GPIO for WS2812 data line
    ESP_ERROR_CHECK(led_effects_init());

    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    const char *prompt = setup_prompt(PROMPT_STR "> ");

    /* Register commands */
    /*remember to add the command registration below if you add a new cmd_xxx */
    /* This does register help command, didn't change */
    esp_console_register_help_command();

    /* This does register system command */
    register_system_common();
    register_edit();
    register_head();
    register_tail();
    register_watch();

    /* This does register sleep command, didn't change*/
#if SOC_LIGHT_SLEEP_SUPPORTED
    register_system_light_sleep();
#endif
#if SOC_DEEP_SLEEP_SUPPORTED
    register_system_deep_sleep();
#endif

    /* This does register WiFi command*/
#if (CONFIG_ESP_WIFI_ENABLED || CONFIG_ESP_HOST_WIFI_ENABLED)
    register_wifi();
#endif

    /* This does register BLE command*/
    register_ble();

    /* This does register NVS command*/ 
    register_nvs();

    /* This does register NETUTILS command*/
    register_netutils();

    /*This does register File System command*/
    register_fs();

    /*This does register System Management command*/
    register_sysmgmt();

    /*This does register WS2812 command*/
    app_ws2812_register();

    /*Register depended commands*/
    console_cmd_ping_register();
    console_cmd_getaddrinfo_register();
    console_cmd_setdnsserver_register();
    console_cmd_getdnsserver_register();

    /* End of command registration */

    /* Welcome MOTD */
    printf("\n"
           "This is an example of ESP-IDF console component.\n"
           "Type 'help' to get the list of commands.\n"
           "Use UP/DOWN arrows to navigate through command history.\n"
           "Press TAB when typing command name to auto-complete.\n"
           "Ctrl+C will terminate the console environment.\n"
           "Modified by bddx.\n");

    if (linenoiseIsDumbMode()) {
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Windows Terminal or Putty instead.\n");
    }

    /* Main loop */
    while(true) {
        /* Get a line using linenoise.
         * The line is returned when ENTER is pressed.
         */
        char* line = linenoise(prompt);

#if CONFIG_CONSOLE_IGNORE_EMPTY_LINES
        if (line == NULL) { /* Ignore empty lines */
            continue;;
        }
#else
        if (line == NULL) { /* Break on EOF or error */
            break;
        }
#endif // CONFIG_CONSOLE_IGNORE_EMPTY_LINES

        /* Add the command to the history if not empty*/
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
#if CONFIG_CONSOLE_STORE_HISTORY
            /* Save command history to filesystem */
            linenoiseHistorySave(HISTORY_PATH);
#endif // CONFIG_CONSOLE_STORE_HISTORY
        }

        /* Try to run the command */
        /* Original loop
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command. Check spelling or type 'help' for available commands.\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }
        */

        /* Now it runs through our shell parser (supports ; | & #) */
        int ret = shell_run_line(line);
        if (ret == -2) {
            printf("Unrecognized command. Check spelling or type 'help' for available commands.\n");
        }

        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }

    ESP_LOGE(TAG, "Error or end-of-input, terminating console");
    esp_console_deinit();
}
