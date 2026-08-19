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

/* Standard C headers */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>

/* Argtable and linenoise */
#include "argtable3/argtable3.h"
#include "linenoise/linenoise.h"

#include "sdkconfig.h"
/* FreeRTOS */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/* components/cmd_xx */
#include "cmd_system.h"
#include "cmd_fs.h"
/* Espressif-idf stuff */
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"      // heap_caps_get_total_size
#include "esp_psram.h"          // esp_psram_get_size



#ifdef CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
#define WITH_TASKS_INFO 1
#endif

static const char *TAG = "cmd_system_common";

static void register_free(void);
static void register_heap(void);
static void register_version(void);
static void register_restart(void);
static void register_uptime(void);
static void register_date(void);
static void register_timezone(void);
static void register_echo(void);
static void register_uname(void);
static void register_hwinfo(void);
#if WITH_TASKS_INFO
static void register_tasks(void);
#endif
static void register_log_level(void);

void register_system_common(void)
{
    register_free();
    register_heap();
    register_version();
    register_restart();
    register_uptime();
    register_date();
    register_timezone();
    register_echo();
    register_uname();
    register_hwinfo();
    register_log_level();
}

/* ==================================================================== */
/*  'echo' — echo + file write (> = cover, >> = append)                 */
/* ==================================================================== */

static int handle_echo(int argc, char **argv)
{
    bool no_newline = false;
    int  start = 1;                         /* skip argv[0]="echo" */

    /* detect -n option */
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        no_newline = true;
        start = 2;
    }

    /* scan the last two arguments to see if they are "> file" or ">> file" */
    bool   redirect = false;
    bool   append   = false;
    const char *filename = NULL;
    int    last_idx = argc - 1;             /* index of the last valid argument */

    if (argc >= start + 2) {
        const char *op = argv[last_idx - 1];
        if (strcmp(op, ">") == 0) {
            redirect = true;
            append   = false;
            filename = argv[last_idx];
            last_idx -= 2;
        } else if (strcmp(op, ">>") == 0) {
            redirect = true;
            append   = true;
            filename = argv[last_idx];
            last_idx -= 2;
        }
    }

    /* ── redirect to file ── */
/* ── redirect to file ── */
    if (redirect) {
        char resolved[256];
        if (!resolve_path(filename, resolved, sizeof(resolved))) {
            printf("echo: path too long\n");
            return 1;
        }
        FILE *f = fopen(resolved, append ? "a" : "w");
        if (!f) {
            printf("Error: cannot open '%s' for %s\n",
                   resolved, append ? "append" : "write");
            return 1;
        }
        for (int i = start; i <= last_idx; i++) {
            if (i > start) fputc(' ', f);
            fputs(argv[i], f);
        }
        if (!no_newline) fputc('\n', f);
        fclose(f);
        return 0;
    }

    /* ── print to terminal ── */
    for (int i = start; i < argc; i++) {
        if (i > start) putchar(' ');
        printf("%s", argv[i]);
    }
    if (!no_newline) printf("\n");

    return 0;
}

static void register_echo(void)
{
    const esp_console_cmd_t cmd = {
        .command = "echo",
        .help = "Echo text to console, or use > / >> to write/append to file",
        .hint  = "[-n] [<text>...] [[>|>>] <file>]",
        .func  = &handle_echo,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}



/* command for timezone*/
static int cmd_timezone(int argc, char **argv)
{
    if (argc == 1) {
        // get current timezone 
        const char *tz = getenv("TZ");
        if (tz) {
            printf("Current timezone: %s\n", tz);
        } else {
            printf("Timezone not set (default UTC)\n");
        }
        return 0;
    }

    if (argc >= 2) {
        // set timezone
        setenv("TZ", argv[1], 1);
        tzset();
        printf("Timezone set to: %s\n", argv[1]);

        // display current local time for verification
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char buf[64];
        strftime(buf, sizeof(buf), "%c", &timeinfo);
        printf("Current local time: %s\n", buf);
        return 0;
    }

    return 1;
}

static void register_timezone(void)
{
    const esp_console_cmd_t tz_cmd = {
        .command = "timezone",
        .help = "Get or set timezone (e.g. timezone CST-8)",
        .hint = "[<tzstring>]",
        .func = &cmd_timezone,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&tz_cmd));
}

/* 'date' command */
static int handle_date(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "-q") == 0)) {
        // query
        time_t now = time(NULL);
        struct tm compile_tm = {0};
strptime(__DATE__ " " __TIME__, "%b %d %Y %H:%M:%S", &compile_tm);
compile_tm.tm_isdst = -1;
time_t compile_epoch = mktime(&compile_tm);
if (now < compile_epoch) {
    printf("System time is EARLIER THAN build time (%s %s). Calibrate if possible.\n",
           __DATE__, __TIME__);
}
        if (now == 0) {
            printf("System time not set (epoch 0). Check your RTC or use 'date -s' to set.\n");
            return 0;
        }
        struct tm *tm = localtime(&now);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
        printf("%s\n", buf);
    }
    else if (argc == 3 && strcmp(argv[1], "-s") == 0) {
        // setup
        struct tm tm = {0};
        const char *fmt = "%Y-%m-%d %H:%M:%S";
        char *ret = strptime(argv[2], fmt, &tm);
        if (ret == NULL || *ret != '\0') {
            printf("Invalid format. Expected: YYYY-MM-DD HH:MM:SS\n");
            return 1;
        }
        tm.tm_isdst = -1;

        struct timeval tv = { .tv_sec = mktime(&tm), .tv_usec = 0 };
        if (settimeofday(&tv, NULL) == 0) {
            printf("Time set to: %s\n", argv[2]);
        } else {
            printf("Failed to set time\n");
        }
    }
    else {
        printf("Usage:\n");
        printf("  date            Show current time\n");
        printf("  date -q         Show current time\n");
        printf("  date -s \"YYYY-MM-DD HH:MM:SS\"   Set time\n");
    }
    return 0;
}

static void register_date(void)
{
    const esp_console_cmd_t cmd = {
        .command = "date",
        .help = "Get the current date and time",
        .hint = "[-q] [-s \"YYYY-MM-DD HH:MM:SS\"]",
        .func = &handle_date,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/* 'uptime' command */
static int get_uptime(int argc, char **argv)
{
    uint64_t us = esp_timer_get_time();
    uint64_t sec = us / 1000000;

    uint32_t days = sec / 86400;
    uint32_t hours = (sec % 86400) / 3600;
    uint32_t minutes = (sec % 3600) / 60;
    uint32_t seconds = sec % 60;

    if (days > 0) {
        printf("Uptime: %"PRIu32" day%c %02"PRIu32":%02"PRIu32":%02"PRIu32"\n",
               days, days > 1 ? 's' : ' ', hours, minutes, seconds);
    } else {
        printf("Uptime: %02"PRIu32":%02"PRIu32":%02"PRIu32"\n", hours, minutes, seconds);
    }
    return 0;
}

static void register_uptime(void)
{
    const esp_console_cmd_t cmd = {
        .command = "uptime",
        .help = "Get the current uptime of the system",
        .hint = NULL,
        .func = &get_uptime,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/* hwinfo command*/
static int handle_hwinfo(int argc, char **argv)
{
    esp_chip_info_t info;
    esp_chip_info(&info);

    const char *model;
    const char *arch;

    switch (info.model) {
        case CHIP_ESP32:    model = "ESP32";    arch = "Xtensa LX6";    break;
        case CHIP_ESP32S2:  model = "ESP32-S2"; arch = "Xtensa LX7";    break;
        case CHIP_ESP32S3:  model = "ESP32-S3"; arch = "Xtensa LX7";    break;
        case CHIP_ESP32C3:  model = "ESP32-C3"; arch = "RISC-V 32-bit"; break;
        case CHIP_ESP32C2:  model = "ESP32-C2"; arch = "RISC-V 32-bit"; break;
        case CHIP_ESP32C5:  model = "ESP32-C5"; arch = "RISC-V 32-bit"; break;
        case CHIP_ESP32H2:  model = "ESP32-H2"; arch = "RISC-V 32-bit"; break;
        case CHIP_ESP32P4:  model = "ESP32-P4"; arch = "RISC-V 32-bit"; break;
        default:            model = "Unknown";  arch = "Unknown";       break;
    }

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }

    /* ── table output ── */
    printf("┌─────────────────────────────────────────┐\n");
    printf("│          Hardware Information           │\n");
    printf("├──────────────────┬──────────────────────┤\n");
    printf("│ Chip Model       │ %-20s │\n", model);
    printf("│ Chip Revision    │ %-20d │\n", info.revision);
    printf("│ Cores            │ %-20d │\n", info.cores);
    printf("│ CPU Architecture │ %-20s │\n", arch);

    if (flash_size > 0) {
        printf("│ Flash Size       │ %-4"PRIu32" MB %-11s │\n",
               flash_size / (1024 * 1024),
               info.features & CHIP_FEATURE_EMB_FLASH ? "(Embedded)" : "(External)");
    } else {
        printf("│ Flash Size       │ %-20s  │\n", "Unknown");
    }

    {
        char features[64] = "";
        if (info.features & CHIP_FEATURE_WIFI_BGN) strlcat(features, "WiFi ", sizeof(features));
        if (info.features & CHIP_FEATURE_BLE)      strlcat(features, "BLE ", sizeof(features));
        if (info.features & CHIP_FEATURE_BT)       strlcat(features, "BT ", sizeof(features));
        if (features[0] == '\0') strlcpy(features, "None", sizeof(features));
        else features[strlen(features) - 1] = '\0';  // remove trailing space
        printf("│ Features         │ %-20s │\n", features);
    }

    {
        size_t internal_ram = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        char ram_str[32];
        snprintf(ram_str, sizeof(ram_str), "%zu KB", internal_ram / 1024);
        printf("│ Internal RAM     │ %-20s │\n", ram_str);
    }

    {
        size_t psram = esp_psram_get_size();
        if (psram > 0) {
            char psram_str[32];
            snprintf(psram_str, sizeof(psram_str), "%zu MB", psram / (1024 * 1024));
            printf("│ PSRAM            │ %-20s │\n", psram_str);
        } else {
            printf("│ PSRAM            │ %-20s │\n", "None");
        }
    }

    printf("└──────────────────┴──────────────────────┘\n");
    return 0;
}

static void register_hwinfo(void)
{
    const esp_console_cmd_t cmd = {
        .command = "hwinfo",
        .help = "Show detailed hardware information (chip, RAM, flash, features)",
        .hint = NULL,
        .func = &handle_hwinfo,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* 'version' command */
static int get_version(int argc, char **argv)
{
printf("┌─────────────────────────────────────────┐\n");
    printf("│            Software Versions            │\n");
    printf("├──────────────────┬──────────────────────┤\n");
    printf("│ ESP-IDF Version  │ %-20s │\n", esp_get_idf_version());
    printf("│ FreeRTOS Kernel  │ %-20s │\n", tskKERNEL_VERSION_NUMBER);
    printf("│ Build Date       │ %-20s │\n", __DATE__);
    printf("│ Build Time       │ %-20s │\n", __TIME__);
#if defined(__GNUC__) && defined(__GNUC_MINOR__) && defined(__GNUC_PATCHLEVEL__)
    {
        char compiler[32];
        snprintf(compiler, sizeof(compiler), "GCC %d.%d.%d",
                 __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
        printf("│ Compiler         │ %-20s │\n", compiler);
    }
#endif
    printf("└──────────────────┴──────────────────────┘\n");
    return 0;
}

static void register_version(void)
{
    const esp_console_cmd_t cmd = {
        .command = "version",
        .help = "Get version of chip and SDK",
        .hint = NULL,
        .func = &get_version,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/** 'restart' command restarts the program */

static int restart(int argc, char **argv)
{
    char *buf = linenoise("Are you sure? [y/N]: ");
    if (buf == NULL) {
        printf("Canceled\n");
        return 0;
    }

    if (strcmp(buf, "y") == 0 || strcmp(buf, "Y") == 0) {
        printf("Restarting...\n");
        vTaskDelay(pdMS_TO_TICKS(100));
        free(buf);          // free the buffer of linenoiseLine 
        esp_restart();
    } else {
        printf("Canceled\n");
        free(buf);
    }
    return 0;
}

static void register_restart(void)
{
    const esp_console_cmd_t cmd = {
        .command = "restart",
        .help = "Software reset of the chip",
        .hint = NULL,
        .func = &restart,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/** 'free' command prints available heap memory */

static int free_mem(int argc, char **argv)
{
    printf("%"PRIu32"\n", esp_get_free_heap_size());
    return 0;
}

static void register_free(void)
{
    const esp_console_cmd_t cmd = {
        .command = "free",
        .help = "Get the current size of free heap memory",
        .hint = NULL,
        .func = &free_mem,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/* 'heap' command prints minimum heap size */
static int heap_size(int argc, char **argv)
{
    uint32_t heap_size = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    printf("min heap size: %"PRIu32"\n", heap_size);
    return 0;
}

static void register_heap(void)
{
    const esp_console_cmd_t heap_cmd = {
        .command = "heap",
        .help = "Get minimum size of free heap memory that was available during program execution",
        .hint = NULL,
        .func = &heap_size,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&heap_cmd) );

}

/** log_level command changes log level via esp_log_level_set */

static struct {
    struct arg_str *tag;
    struct arg_str *level;
    struct arg_end *end;
} log_level_args;

static const char* s_log_level_names[] = {
    "none",
    "error",
    "warn",
    "info",
    "debug",
    "verbose"
};

static int log_level(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &log_level_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, log_level_args.end, argv[0]);
        return 1;
    }
    assert(log_level_args.tag->count == 1);
    assert(log_level_args.level->count == 1);
    const char* tag = log_level_args.tag->sval[0];
    const char* level_str = log_level_args.level->sval[0];
    esp_log_level_t level;
    size_t level_len = strlen(level_str);
    for (level = ESP_LOG_NONE; level <= ESP_LOG_VERBOSE; level++) {
        if (memcmp(level_str, s_log_level_names[level], level_len) == 0) {
            break;
        }
    }
    if (level > ESP_LOG_VERBOSE) {
        printf("Invalid log level '%s', choose from none|error|warn|info|debug|verbose\n", level_str);
        return 1;
    }
    if (level > CONFIG_LOG_MAXIMUM_LEVEL) {
        printf("Can't set log level to %s, max level limited in menuconfig to %s. "
               "Please increase CONFIG_LOG_MAXIMUM_LEVEL in menuconfig.\n",
               s_log_level_names[level], s_log_level_names[CONFIG_LOG_MAXIMUM_LEVEL]);
        return 1;
    }
    esp_log_level_set(tag, level);
    return 0;
}

static void register_log_level(void)
{
    log_level_args.tag = arg_str1(NULL, NULL, "<tag|*>", "Log tag to set the level for, or * to set for all tags");
    log_level_args.level = arg_str1(NULL, NULL, "<none|error|warn|info|debug|verbose>", "Log level to set. Abbreviated words are accepted.");
    log_level_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "log_level",
        .help = "Set log level for all tags or a specific tag.",
        .hint = NULL,
        .func = &log_level,
        .argtable = &log_level_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/* ==================================================================== */
/*  'uname' — Easter egg imitating unix-like OSes                       */
/* ==================================================================== */

#ifndef CONFIG_LWIP_LOCAL_HOSTNAME
#define DEFAULT_HOSTNAME "espressif"
#else
#define DEFAULT_HOSTNAME CONFIG_LWIP_LOCAL_HOSTNAME
#endif

static int handle_uname(int argc, char **argv)
{
    /* Collect all required information(Once) */
    esp_chip_info_t info;
    esp_chip_info(&info);

    const char *model;
    const char *arch;
    switch (info.model) {
        case CHIP_ESP32:    model = "ESP32";    arch = "Xtensa LX6";    break;
        case CHIP_ESP32S2:  model = "ESP32-S2"; arch = "Xtensa LX7";    break;
        case CHIP_ESP32S3:  model = "ESP32-S3"; arch = "Xtensa LX7";    break;
        case CHIP_ESP32C3:  model = "ESP32-C3"; arch = "RISC-V 32-bit"; break;
        case CHIP_ESP32C2:  model = "ESP32-C2"; arch = "RISC-V 32-bit"; break;
        case CHIP_ESP32C5:  model = "ESP32-C5"; arch = "RISC-V 32-bit"; break;
        case CHIP_ESP32H2:  model = "ESP32-H2"; arch = "RISC-V 32-bit"; break;
        case CHIP_ESP32P4:  model = "ESP32-P4"; arch = "RISC-V 32-bit"; break;
        default:            model = "Unknown";  arch = "Unknown";       break;
    }

    const char *kernel_name    = "FreeRTOS";
    const char *nodename       = DEFAULT_HOSTNAME;
    const char *kernel_release = tskKERNEL_VERSION_NUMBER;

    /* kernel_version — imitating Linux Style */
    char kernel_version[128];
    snprintf(kernel_version, sizeof(kernel_version),
             "#1 Build %s %s", __DATE__, __TIME__);

    const char *machine         = arch;
    const char *processor       = model;
    char   platform[64];
    snprintf(platform, sizeof(platform), "%s rev %d", model, info.revision);
    const char *operating_sys   = "ESP-IDF FreeRTOS";

    /* ── parse option ── */
    bool opt_all    = false;
    bool opt_kernel = false;   /* -s */
    bool opt_node   = false;   /* -n */
    bool opt_release= false;   /* -r */
    bool opt_ver    = false;   /* -v */
    bool opt_mach   = false;   /* -m */
    bool opt_proc   = false;   /* -p */
    bool opt_plat   = false;   /* -i */
    bool opt_os     = false;   /* -o */

    /* scan argv */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (const char *p = argv[i] + 1; *p; p++) {
                switch (*p) {
                    case 'a': opt_all = true; break;
                    case 's': opt_kernel = true; break;
                    case 'n': opt_node = true; break;
                    case 'r': opt_release = true; break;
                    case 'v': opt_ver = true; break;
                    case 'm': opt_mach = true; break;
                    case 'p': opt_proc = true; break;
                    case 'i': opt_plat = true; break;
                    case 'o': opt_os = true; break;
                    default:
                        printf("uname: invalid option -- '%c'\n", *p);
                        printf("Try 'uname --help' for more information.\n");
                        return 1;
                }
            }
        } else {
            printf("uname: unexpected argument '%s'\n", argv[i]);
            return 1;
        }
    }

    /* if no option, treat as -s */
    if (!opt_all && !opt_kernel && !opt_node && !opt_release &&
        !opt_ver && !opt_mach && !opt_proc && !opt_plat && !opt_os) {
        opt_kernel = true;
    }

    /* ── output ── */
    if (opt_all) {
        printf("%s %s %s %s %s %s %s %s\n",
               kernel_name, nodename, kernel_release, kernel_version,
               machine, processor, platform, operating_sys);
        return 0;
    }

    /* output by the order */
    if (opt_kernel)  printf("%s ", kernel_name);
    if (opt_node)    printf("%s ", nodename);
    if (opt_release) printf("%s ", kernel_release);
    if (opt_ver)     printf("%s ", kernel_version);
    if (opt_mach)    printf("%s ", machine);
    if (opt_proc)    printf("%s ", processor);
    if (opt_plat)    printf("%s ", platform);
    if (opt_os)      printf("%s ", operating_sys);

    /* if at least one option is specified, add a newline at the end */
    if (opt_kernel || opt_node || opt_release || opt_ver ||
        opt_mach   || opt_proc || opt_plat   || opt_os) {
        printf("\b ");     /* remove the last extra space (cleaner output) */
        printf("\n");
    }

    return 0;
}

static void register_uname(void)
{
    const esp_console_cmd_t cmd = {
        .command = "uname",
        .help = "Print system information (Unix-style)",
        .hint = "[-asnrvmpio]",
        .func = &handle_uname,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}