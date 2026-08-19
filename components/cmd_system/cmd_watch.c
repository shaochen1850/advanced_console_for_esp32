#include "cmd_system.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

static int watch_running = 1;

static int do_watch(int argc, char **argv) {
    int interval = 2;          /* seconds */
    const char *cmdline = NULL;

    struct arg_int *sec = arg_int0("n", "interval", "<sec>",
                                   "Interval in seconds (default 2)");
    struct arg_str *cmd = arg_strn(NULL, NULL, "<command...>", 1, 32,
                                   "Command to execute");
    struct arg_end *end = arg_end(2);
    void *argtable[] = { sec, cmd, end };

    /* We need to parse the raw line ourselves because esp_console
     * splits arguments, but we want the whole command string.
     * We'll reconstruct from argv. */
    int nerrors = arg_parse(argc, argv, argtable);
    if (nerrors > 0) {
        arg_print_errors(stderr, end, argv[0]);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 1;
    }

    if (sec->count > 0) interval = sec->ival[0];
    if (interval < 1) interval = 1;

    /* Rebuild command string from cmd->sval and remaining args */
    char cmd_buf[256] = "";
    for (int i = 0; i < cmd->count; i++) {
        if (i > 0) strlcat(cmd_buf, " ", sizeof(cmd_buf));
        strlcat(cmd_buf, cmd->sval[i], sizeof(cmd_buf));
    }
    cmdline = cmd_buf;

    if (strlen(cmdline) == 0) {
        printf("watch: no command specified\n");
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 1;
    }

    printf("Every %ds: %s\n", interval, cmdline);
    printf("Press Ctrl+C to stop.\n\n");

    watch_running = 1;

    while (watch_running) {
        /* Clear screen */
        printf("\033[2J\033[H");

        /* Header */
        printf("Every %ds: %s\n", interval, cmdline);
        printf("Press Ctrl+C to stop.\n\n");

        /* Run command */
        int ret;
        esp_console_run(cmdline, &ret);

        printf("\n");

        /* Wait with non-blocking stdin check */
        for (int i = 0; i < interval * 10; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));

            /* Check if user pressed a key */
            fd_set rfds;
            struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
            FD_ZERO(&rfds);
            FD_SET(STDIN_FILENO, &rfds);
            if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) > 0) {
                char ch;
                if (read(STDIN_FILENO, &ch, 1) > 0 && ch == 0x03) {
                    watch_running = 0;
                    printf("^C\n");
                }
            }
        }
    }

    printf("watch stopped.\n");
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return 0;
}

void register_watch(void) {
    const esp_console_cmd_t cmd = {
        .command = "watch",
        .help = "Execute a command periodically\n"
                "  watch -n <sec> <command> [args...]",
        .hint = NULL,
        .func = do_watch,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}