#include "cmd_system.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"

static const char *TAG = "headtail";

/* ── head ────────────────────────────────────────────── */

static int do_head(int argc, char **argv) {
    int nlines = 10;
    const char *filepath = NULL;

    /* argtable */
    struct arg_int *num = arg_int0("n", "lines", "<n>", "Number of lines (default 10)");
    struct arg_file *file = arg_file0(NULL, NULL, "<file>", "File to read (if omitted, read stdin)");
    struct arg_end *end = arg_end(2);
    void *argtable[] = { num, file, end };

    int nerrors = arg_parse(argc, argv, argtable);
    if (nerrors > 0) {
        arg_print_errors(stderr, end, argv[0]);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 1;
    }

    if (num->count > 0) nlines = num->ival[0];
    if (file->count > 0) filepath = file->filename[0];

    FILE *fp;
    if (filepath) {
        fp = fopen(filepath, "r");
        if (!fp) {
            printf("head: cannot open '%s'\n", filepath);
            arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
            return 1;
        }
    } else {
        fp = stdin;
    }

    char line_buf[512];
    int count = 0;
    while (fgets(line_buf, sizeof(line_buf), fp) && count < nlines) {
        fputs(line_buf, stdout);
        count++;
    }

    if (filepath) fclose(fp);

    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return 0;
}

/* ── tail ────────────────────────────────────────────── */

static int do_tail(int argc, char **argv) {
    int nlines = 10;
    const char *filepath = NULL;

    struct arg_int *num = arg_int0("n", "lines", "<n>", "Number of lines (default 10)");
    struct arg_file *file = arg_file0(NULL, NULL, "<file>", "File to read (if omitted, read stdin)");
    struct arg_end *end = arg_end(2);
    void *argtable[] = { num, file, end };

    int nerrors = arg_parse(argc, argv, argtable);
    if (nerrors > 0) {
        arg_print_errors(stderr, end, argv[0]);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 1;
    }

    if (num->count > 0) nlines = num->ival[0];
    if (file->count > 0) filepath = file->filename[0];

    /* circular buffer of nlines pointers */
    char **ring = calloc(nlines, sizeof(char *));
    if (!ring) {
        printf("tail: out of memory\n");
        return 1;
    }

    FILE *fp = stdin;
    if (filepath) {
        fp = fopen(filepath, "r");
        if (!fp) {
            printf("tail: cannot open '%s'\n", filepath);
            free(ring);
            return 1;
        }
    }

    char line_buf[512];
    int idx = 0, total = 0;
    while (fgets(line_buf, sizeof(line_buf), fp)) {
        free(ring[idx]);            /* free previously stored line */
        ring[idx] = strdup(line_buf);
        if (!ring[idx]) break;
        idx = (idx + 1) % nlines;
        total++;
    }
    if (filepath) fclose(fp);

    int start = (total < nlines) ? 0 : idx;
    int count = (total < nlines) ? total : nlines;
    for (int i = 0; i < count; i++) {
        int pos = (start + i) % nlines;
        if (ring[pos]) fputs(ring[pos], stdout);
    }

    for (int i = 0; i < nlines; i++) free(ring[i]);
    free(ring);

    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return 0;
}

/* ── registration ────────────────────────────────────── */

void register_head(void) {
    const esp_console_cmd_t cmd = {
        .command = "head",
        .help = "Print first N lines of file or stdin\n"
                "  head -n <lines> [file]",
        .hint = NULL,
        .func = do_head,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void register_tail(void) {
    const esp_console_cmd_t cmd = {
        .command = "tail",
        .help = "Print last N lines of file or stdin\n"
                "  tail -n <lines> [file]",
        .hint = NULL,
        .func = do_tail,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}