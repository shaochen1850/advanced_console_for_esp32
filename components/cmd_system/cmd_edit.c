#include "cmd_system.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "linenoise/linenoise.h"

#define EDIT_MAX_LINES 2000
#define EDIT_MAX_LINE_LEN 256

typedef struct {
    char **lines;
    int   count;
    int   capacity;
    int   modified;
    char  filename[128];
} EditBuffer;

static EditBuffer eb;

/* ── buffer helpers ──────────────────────────────────── */

static void eb_init(void) {
    eb.capacity = 64;
    eb.lines = calloc(eb.capacity, sizeof(char *));
    eb.count = 0;
    eb.modified = 0;
    eb.filename[0] = '\0';
}

static void eb_free(void) {
    for (int i = 0; i < eb.count; i++) free(eb.lines[i]);
    free(eb.lines);
}

static int eb_add_line(const char *text) {
    if (eb.count >= EDIT_MAX_LINES) return -1;
    if (eb.count >= eb.capacity) {
        eb.capacity *= 2;
        eb.lines = realloc(eb.lines, eb.capacity * sizeof(char *));
        if (!eb.lines) return -1;
    }
    eb.lines[eb.count] = strdup(text);
    return eb.lines[eb.count] ? eb.count++ : -1;
}

static int eb_insert_line(int at, const char *text) {
    if (at < 0 || at > eb.count) return -1;
    if (eb.count >= EDIT_MAX_LINES) return -1;
    if (eb.count >= eb.capacity) {
        eb.capacity *= 2;
        eb.lines = realloc(eb.lines, eb.capacity * sizeof(char *));
    }
    memmove(&eb.lines[at + 1], &eb.lines[at],
            (eb.count - at) * sizeof(char *));
    eb.lines[at] = strdup(text);
    if (!eb.lines[at]) return -1;
    eb.count++;
    return 0;
}

static int eb_delete_line(int at) {
    if (at < 0 || at >= eb.count) return -1;
    free(eb.lines[at]);
    memmove(&eb.lines[at], &eb.lines[at + 1],
            (eb.count - at - 1) * sizeof(char *));
    eb.count--;
    return 0;
}

static int eb_load_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char buf[EDIT_MAX_LINE_LEN];
    while (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        if (eb_add_line(buf) < 0) break;
    }
    fclose(f);
    strncpy(eb.filename, path, sizeof(eb.filename) - 1);
    return 0;
}

static int eb_save_file(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < eb.count; i++) {
        fprintf(f, "%s\n", eb.lines[i]);
    }
    fclose(f);
    eb.modified = 0;
    return 0;
}

/* ── multi-line input, with optional insert position ── */
/*  position < 0  →  append
 *  position >= 0 → Insert before line number (0-based)
 */

static int read_multiline_at(const char *prompt, int position) {
    printf("%s (type '.' on a line by itself to finish, Ctrl+C to cancel)\n", prompt);
    int added = 0;
    int insert_at = (position >= 0) ? position : eb.count;
    while (1) {
        char prompt_buf[40];
        snprintf(prompt_buf, sizeof(prompt_buf), " > ");
        char *line = linenoise(prompt_buf);
        if (line == NULL) {          /* Ctrl+C */
            printf("^C\n");
            return -1;
        }
        if (strcmp(line, ".") == 0) {
            linenoiseFree(line);
            break;
        }
        if (position >= 0) {
            eb_insert_line(insert_at, line);
            insert_at++;
        } else {
            eb_add_line(line);
        }
        linenoiseFree(line);
        added++;
    }
    if (added > 0) eb.modified = 1;
    return 0;
}

/* ── edit command loop (linenoise version) ───────────── */

static void edit_loop(void) {
    for (;;) {
        char *cmd = linenoise(":");      /* linenoise echo */
        if (cmd == NULL) {               /* Ctrl+C —→ Equals to :q! */
            printf("^C\n");
            break;                       /* Unsaved exit */
        }
        if (strlen(cmd) == 0) {
            linenoiseFree(cmd);
            continue;
        }

        /* quit commands */
        if (strcmp(cmd, "q") == 0) {
            if (eb.modified) {
                printf("No write since last change. Use :q! to force quit.\n");
            } else {
                linenoiseFree(cmd);
                break;
            }
        }
        else if (strcmp(cmd, "q!") == 0) {
            linenoiseFree(cmd);
            break;
        }
        else if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) {
            if (eb.filename[0] && eb_save_file(eb.filename) == 0) {
                printf("Saved %d lines to %s\n", eb.count, eb.filename);
                linenoiseFree(cmd);
                break;
            } else printf("Save failed.\n");
        }
        else if (strcmp(cmd, "w") == 0) {
            if (eb.filename[0] && eb_save_file(eb.filename) == 0)
                printf("Saved %d lines to %s\n", eb.count, eb.filename);
            else printf("Save failed. Use :w <filename> to specify path.\n");
        }
        else if (strncmp(cmd, "w ", 2) == 0) {
            char *path = cmd + 2;
            if (eb_save_file(path) == 0) {
                printf("Saved %d lines to %s\n", eb.count, path);
                strncpy(eb.filename, path, sizeof(eb.filename) - 1);
            } else printf("Save failed.\n");
        }
        /* help */
        else if (strcmp(cmd, "h") == 0) {
            printf(
                "Commands:\n"
                "  :w [file]   save\n"
                "  :q           quit (if no changes)\n"
                "  :q! / Ctrl+C quit without saving\n"
                "  :wq / :x     save and quit\n"
                "  :p [N[-M]]   print line(s)\n"
                "  :d N[-M]     delete line(s)\n"
                "  :i N         insert before line N\n"
                "  :a           append to end\n"
                "  :c N         change line N\n"
                "  :h           this help\n"
                "  <num>        print that line\n"
            );
        }
        /* print */
        else if (strcmp(cmd, "p") == 0) {
            for (int i = 0; i < eb.count; i++)
                printf("%4d  %s\n", i + 1, eb.lines[i]);
        }
        else if (cmd[0] == 'p' && cmd[1] == ' ') {
            char *range = cmd + 2;
            int from = 0, to = eb.count - 1;
            char *dash = strchr(range, '-');
            if (dash) {
                *dash = '\0';
                to = atoi(dash + 1) - 1;
            }
            from = atoi(range) - 1;
            if (from < 0) from = 0;
            if (to >= eb.count) to = eb.count - 1;
            if (from > to) { int t = from; from = to; to = t; }
            for (int i = from; i <= to; i++)
                printf("%4d  %s\n", i + 1, eb.lines[i]);
        }
        /* delete */
        else if (cmd[0] == 'd' && cmd[1] == ' ') {
            char *range = cmd + 2;
            int from = 0, to = eb.count - 1;
            char *dash = strchr(range, '-');
            if (dash) {
                *dash = '\0';
                to = atoi(dash + 1) - 1;
            }
            from = atoi(range) - 1;
            if (from < 0) from = 0;
            if (to >= eb.count) to = eb.count - 1;
            if (from > to) { int t = from; from = to; to = t; }
            for (int i = to; i >= from; i--) eb_delete_line(i);
            eb.modified = 1;
            printf("Deleted lines %d-%d\n", from + 1, to + 1);
        }
        /* insert */
        else if (cmd[0] == 'i' && cmd[1] == ' ') {
            int at = atoi(cmd + 2) - 1;
            if (at < 0) at = 0;
            if (at > eb.count) at = eb.count;
            char prompt[32];
            snprintf(prompt, sizeof(prompt), "Insert before line %d", at + 1);
            read_multiline_at(prompt, at);       // ← position
        }
        /* append */
        else if (strcmp(cmd, "a") == 0) {
            read_multiline_at("Append to end", -1);  // ← -1 for append
        }
        /* change */
        else if (cmd[0] == 'c' && cmd[1] == ' ') {
            int at = atoi(cmd + 2) - 1;
            if (at < 0 || at >= eb.count) {
                printf("Invalid line number\n");
                linenoiseFree(cmd);
                continue;
            }
            printf("Old: %s\n", eb.lines[at]);
            printf("New: ");
            fflush(stdout);
            char newbuf[EDIT_MAX_LINE_LEN];
            if (!fgets(newbuf, sizeof(newbuf), stdin)) {
                linenoiseFree(cmd);
                continue;
            }
            newbuf[strcspn(newbuf, "\r\n")] = '\0';
            free(eb.lines[at]);
            eb.lines[at] = strdup(newbuf);
            eb.modified = 1;
        }
        /* just a number */
        else if (isdigit((unsigned char)cmd[0])) {
            int n = atoi(cmd) - 1;
            if (n >= 0 && n < eb.count)
                printf("%4d  %s\n", n + 1, eb.lines[n]);
            else printf("Invalid line number\n");
        }
        else {
            printf("Unknown command. Type :h for help.\n");
        }
        linenoiseFree(cmd);
    }
}

/* ── do_edit ─────────────────────────────────────────── */

static int do_edit(int argc, char **argv) {
    struct arg_file *file = arg_file1(NULL, NULL, "<file>", "File to edit");
    struct arg_end *end = arg_end(2);
    void *argtable[] = { file, end };

    int nerrors = arg_parse(argc, argv, argtable);
    if (nerrors > 0) {
        arg_print_errors(stderr, end, argv[0]);
        arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
        return 1;
    }

    const char *path = file->filename[0];

    eb_init();
    eb_load_file(path);   /* may be empty — that's ok */
    snprintf(eb.filename, sizeof(eb.filename), "%s", path);

    printf("Editing %s (%d lines). Type :h for help, :q to quit.\n",
           path, eb.count);

    edit_loop();

    eb_free();
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    return 0;
}

void register_edit(void) {
    const esp_console_cmd_t cmd = {
        .command = "edit",
        .help = "Simple line editor\n"
                "  edit <file>",
        .hint = NULL,
        .func = do_edit,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}