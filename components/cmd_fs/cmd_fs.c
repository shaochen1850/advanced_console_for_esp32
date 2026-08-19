/*
 * SPDX-FileCopyrightText: 2026 shaochen1850
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/* cmd_fs.c
 * POSIX-style file system management commands for console
 *
 * Provides: ls, cd, pwd, cat, rm, touch, mkdir, cp, mv, df
 * All commands route through ESP-IDF's VFS layer and work with
 * SPIFFS, FAT, LittleFS, or any registered filesystem.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>          /* chdir() */

#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_console.h"
#include "esp_err.h"

#include "cmd_fs.h"

/* ================================================================
 * Internal state
 * ================================================================ */

static char cwd[256];           // current working directory
static char old_cwd[256];       // previous directory (for "cd -")

/* Known mount points — probed at runtime to see which are active.
 * Extend this list if your project uses custom mount paths. */
static const char *MOUNT_POINTS[] = {
    "/spiffs",
    "/sdcard",
    "/littlefs",
    "/flash",
    "/www",
    "/data",
    "/storage",
    NULL
};

/* ================================================================
 * Forward declarations
 * ================================================================ */

static int handle_ls(int argc, char **argv);
static int handle_cd(int argc, char **argv);
static int handle_pwd(int argc, char **argv);
static int handle_cat(int argc, char **argv);
static int handle_rm(int argc, char **argv);
static int handle_touch(int argc, char **argv);
static int handle_mkdir(int argc, char **argv);
static int handle_cp(int argc, char **argv);
static int handle_mv(int argc, char **argv);
static int handle_df(int argc, char **argv);

/* Path helpers */
bool resolve_path(const char *input, char *out, size_t out_size);
static void normalize_path(char *path);
static bool is_directory(const char *path);
static const char *path_basename(const char *path);
static const char *format_size(size_t bytes);

/* Diagnostic */
static bool is_mounted(const char *path);
static void print_mount_points(void);

/* ================================================================
 * Path resolution
 * ================================================================ */

/*
 * normalize_path — collapse ".", "..", and "//" in-place.
 *
 * Algorithm: tokenise on '/', use a stack to track components.
 * This mirrors what every Unix kernel does at VFS level.
 */
static void normalize_path(char *path)
{
    char  temp[256];
    char *comps[32];            // stack of component pointers
    int   depth = 0;
    bool  absolute;

    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    absolute = (temp[0] == '/');

    /* Tokenise */
    char *saveptr;
    char *tok = strtok_r(temp, "/", &saveptr);
    while (tok != NULL) {
        if (strcmp(tok, ".") == 0) {
            /* skip */
        } else if (strcmp(tok, "..") == 0) {
            if (depth > 0) depth--;
        } else {
            if (depth < (int)(sizeof(comps) / sizeof(comps[0]))) {
                comps[depth++] = tok;
            }
        }
        tok = strtok_r(NULL, "/", &saveptr);
    }

    /* Reconstruct */
    char *p = path;
    if (absolute) *p++ = '/';
    for (int i = 0; i < depth; i++) {
        size_t len = strlen(comps[i]);
        memcpy(p, comps[i], len);
        p += len;
        if (i < depth - 1) *p++ = '/';
    }
    if (p == path) {
        strcpy(path, absolute ? "/" : ".");
    } else {
        *p = '\0';
    }
}

/*
 * resolve_path — turn user input (relative or absolute) into a
 *                clean absolute path using cwd and normalize.
 *
 * Returns true on success, false if the output buffer is too small.
 */
bool resolve_path(const char *input, char *out, size_t out_size)
{
    if (input == NULL || input[0] == '\0') {
        strncpy(out, cwd, out_size);
        out[out_size - 1] = '\0';
        return true;
    }

    if (input[0] == '/') {
        /* Absolute path */
        strncpy(out, input, out_size);
        out[out_size - 1] = '\0';
    } else {
        /* Relative path: cwd + "/" + input */
        int len = snprintf(out, out_size, "%s/%s", cwd, input);
        if (len < 0 || (size_t)len >= out_size) {
            return false;
        }
    }

    normalize_path(out);
    return true;
}

/* ================================================================
 * Small utility functions
 * ================================================================ */

static const char *path_basename(const char *path)
{
    const char *last = strrchr(path, '/');
    return last ? last + 1 : path;
}

static bool is_directory(const char *path)
{
    /* VFS Namespace treats "/" as always a directory */
    if (strcmp(path, "/") == 0) {
        return true;
    }

    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    /* Fallback: opendir */
    DIR *d = opendir(path);
    if (d) {
        closedir(d);
        return true;
    }
    return false;
}

static bool is_mounted(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static const char *format_size(size_t bytes)
{
    static char buf[16];
    if (bytes < 1024) {
        snprintf(buf, sizeof(buf), "%u B", (unsigned)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    } else {
        snprintf(buf, sizeof(buf), "%.1f MB",
                 bytes / (1024.0 * 1024.0));
    }
    return buf;
}

/*
 * print_mount_points — list which predefined mount points are
 * currently mounted. Used by "ls /" and "df".
 */
static void print_mount_points(void)
{
    printf("%-24s %s\n", "Mount Point", "Status");
    printf("------------------------- --------\n");
    int found = 0;
    for (int i = 0; MOUNT_POINTS[i] != NULL; i++) {
        if (is_mounted(MOUNT_POINTS[i])) {
            printf("%-24s mounted\n", MOUNT_POINTS[i]);
            found++;
        }
    }
    if (found == 0) {
        printf("  (no filesystems mounted)\n");
    }
}

/* ================================================================
 * Command: ls [path]
 *
 * If path is a directory, list its contents.
 * If path is a file, show its size.
 * If path is "/" (root), list available mount points.
 * With no argument, list cwd.
 * ================================================================ */
static int handle_ls(int argc, char **argv)
{
    const char *target;
    char path[256];

    if (argc < 2) {
        target = cwd;
    } else {
        if (!resolve_path(argv[1], path, sizeof(path))) {
            printf("ls: path too long\n");
            return 1;
        }
        target = path;
    }

    /* Special case: root — show mount points */
    if (strcmp(target, "/") == 0) {
        print_mount_points();
        return 0;
    }

    /* Single file? */
    if (!is_directory(target)) {
        struct stat st;
        if (stat(target, &st) == 0 && S_ISREG(st.st_mode)) {
            printf("%s  %s\n", target, format_size(st.st_size));
            return 0;
        }
        printf("ls: cannot access '%s': No such file or directory\n", target);
        return 1;
    }

    /* Directory listing */
    DIR *dir = opendir(target);
    if (dir == NULL) {
        printf("ls: cannot open directory '%s': %s\n",
               target, strerror(errno));
        return 1;
    }

    printf("%s:\n", target);

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip the mandatory . and .. entries */
        if (strcmp(entry->d_name, ".")  == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* Build full path for stat */
        char full[320];
        int len = snprintf(full, sizeof(full), "%s/%s",
                           target, entry->d_name);
        if (len < 0 || (size_t)len >= sizeof(full)) {
            printf("  %-24s  (path too long)\n", entry->d_name);
            continue;
        }

        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            printf("  %-24s  <DIR>\n", entry->d_name);
        } else if (stat(full, &st) == 0) {
            printf("  %-24s  %s\n", entry->d_name, format_size(st.st_size));
        } else {
            printf("  %-24s  (unknown)\n", entry->d_name);
        }
        count++;
    }
    closedir(dir);

    if (count == 0) {
        printf("  (empty)\n");
    }
    return 0;
}

/* ================================================================
 * Command: cd [path | -]
 *
 * Without argument: go to "/" (root).
 * "cd -" : swap with the previous working directory.
 * ================================================================ */
static int handle_cd(int argc, char **argv)
{
    char path[256];

    if (argc < 2) {
        strcpy(path, "/");
    } else if (strcmp(argv[1], "-") == 0) {
        strncpy(path, old_cwd, sizeof(path));
        path[sizeof(path) - 1] = '\0';
    } else {
        if (!resolve_path(argv[1], path, sizeof(path))) {
            printf("cd: path too long\n");
            return 1;
        }
    }

    if (!is_directory(path)) {
        printf("cd: '%s': Not a directory\n", path);
        return 1;
    }

    /* Remember where we came from */
    strncpy(old_cwd, cwd, sizeof(old_cwd));
    old_cwd[sizeof(old_cwd) - 1] = '\0';

    /* Update cwd */
    strncpy(cwd, path, sizeof(cwd));
    cwd[sizeof(cwd) - 1] = '\0';

    return 0;
}

/* ================================================================
 * Command: pwd
 *
 * Print current working directory.
 * ================================================================ */
static int handle_pwd(int argc, char **argv)
{
    printf("%s\n", cwd);
    return 0;
}

/* ================================================================
 * Command: cat <file>
 *
 * Dump file contents to stdout (handles binary as well).
 * ================================================================ */
static int handle_cat(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: cat <file>\n");
        return 1;
    }

    char path[256];
    if (!resolve_path(argv[1], path, sizeof(path))) {
        printf("cat: path too long\n");
        return 1;
    }

    if (is_directory(path)) {
        printf("cat: '%s': Is a directory\n", path);
        return 1;
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        printf("cat: cannot open '%s': %s\n", path, strerror(errno));
        return 1;
    }

    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        fwrite(buf, 1, n, stdout);
    }
    fclose(fp);
    return 0;
}


/* ================================================================
 * Helper: recursively remove a directory
 * Returns 0 on success, -1 on error (prints own error messages)
 * ================================================================ */
static int remove_recursive(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) {
        printf("rm: cannot open directory '%s': %s\n", path, strerror(errno));
        return -1;
    }

    struct dirent *entry;
    char child[256];
    int ret = 0;

    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        /* combines a full subpath */
        int len = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (len >= (int)sizeof(child)) {
            printf("rm: path too long: %s/%s\n", path, entry->d_name);
            ret = -1;
            break;
        }

        if (entry->d_type == DT_DIR) {
            /* recursively remove subdirectory */
            if (remove_recursive(child) != 0) {
                ret = -1;
                break;
            }
        } else {
            /* remove a file */
            if (unlink(child) != 0) {
                printf("rm: cannot remove '%s': %s\n", child, strerror(errno));
                ret = -1;
                break;
            }
        }
    }

    closedir(dir);

    if (ret == 0) {
        /* recursively remove the empty directory itself */
        if (rmdir(path) != 0) {
            printf("rm: cannot remove directory '%s': %s\n", path, strerror(errno));
            return -1;
        }
    }

    return ret;
}

/* ================================================================
 * Command: rm [-r] <file|dir>
 *
 * Delete a file. With -r, recursively delete a directory.
 * ================================================================ */
static int handle_rm(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage:\n");
        printf("  rm <file>          # delete a file\n");
        printf("  rm -r <dir>        # recursively delete a directory\n");
        return 1;
    }

    int recursive = 0;
    const char *target = NULL;

    /* dealing with -r argument */
    if (strcmp(argv[1], "-r") == 0) {
        recursive = 1;
        if (argc < 3) {
            printf("rm: missing operand after -r\n");
            return 1;
        }
        target = argv[2];
    } else {
        target = argv[1];
    }

    /* dealing with path */
    char path[256];
    if (!resolve_path(target, path, sizeof(path))) {
        printf("rm: path too long\n");
        return 1;
    }

    /* check whether object exists */
    struct stat st;
    if (stat(path, &st) != 0) {
        printf("rm: cannot remove '%s': %s\n", path, strerror(errno));
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!recursive) {
            printf("rm: '%s': Is a directory (use rm -r to remove)\n", path);
            return 1;
        }
        /* recursively delete */
        return remove_recursive(path);
    } else {
        /* regular file */
        if (unlink(path) != 0) {
            printf("rm: cannot remove '%s': %s\n", path, strerror(errno));
            return 1;
        }
        return 0;
    }
}

/* ================================================================
 * Command: touch <file>
 *
 * Create an empty file, or update timestamp of existing file.
 * Uses "a" (append) mode: creates if missing, does NOT truncate
 * an existing file.
 * ================================================================ */
static int handle_touch(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: touch <file>\n");
        return 1;
    }

    char path[256];
    if (!resolve_path(argv[1], path, sizeof(path))) {
        printf("touch: path too long\n");
        return 1;
    }

    FILE *fp = fopen(path, "a");
    if (fp == NULL) {
        printf("touch: cannot create '%s': %s\n",
               path, strerror(errno));
        return 1;
    }
    fclose(fp);
    return 0;
}

/* ================================================================
 * Command: mkdir <directory>
 *
 * Create a new directory.
 * ================================================================ */
static int handle_mkdir(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: mkdir <directory>\n");
        return 1;
    }

    char path[256];
    if (!resolve_path(argv[1], path, sizeof(path))) {
        printf("mkdir: path too long\n");
        return 1;
    }

    if (mkdir(path, 0755) != 0) {
        printf("mkdir: cannot create '%s': %s\n",
               path, strerror(errno));
        return 1;
    }
    return 0;
}

/* ================================================================
 * Command: cp <source> <destination>
 *
 * Copy a file. If destination is an existing directory the source
 * filename is kept and the copy is placed inside that directory.
 * ================================================================ */
static int handle_cp(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: cp <source> <destination>\n");
        return 1;
    }

    char src[256], dst[256], final_dst[320];

    if (!resolve_path(argv[1], src, sizeof(src))) {
        printf("cp: source path too long\n");
        return 1;
    }
    if (!resolve_path(argv[2], dst, sizeof(dst))) {
        printf("cp: destination path too long\n");
        return 1;
    }

    /* Source must exist and be a regular file */
    struct stat st;
    if (stat(src, &st) != 0) {
        printf("cp: cannot stat '%s': %s\n", src, strerror(errno));
        return 1;
    }
    if (S_ISDIR(st.st_mode)) {
        printf("cp: '%s' is a directory (recursive copy not supported)\n",
               src);
        return 1;
    }

    /* If destination is a directory, place the file inside it */
    if (is_directory(dst)) {
        snprintf(final_dst, sizeof(final_dst), "%s/%s",
                 dst, path_basename(src));
    } else {
        strncpy(final_dst, dst, sizeof(final_dst));
        final_dst[sizeof(final_dst) - 1] = '\0';
    }

    FILE *sfp = fopen(src, "r");
    if (sfp == NULL) {
        printf("cp: cannot open source '%s': %s\n", src, strerror(errno));
        return 1;
    }

    FILE *dfp = fopen(final_dst, "w");
    if (dfp == NULL) {
        printf("cp: cannot open destination '%s': %s\n",
               final_dst, strerror(errno));
        fclose(sfp);
        return 1;
    }

    /* Streaming copy in 1 KiB chunks */
    char buf[1024];
    size_t total = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), sfp)) > 0) {
        if (fwrite(buf, 1, n, dfp) != n) {
            printf("cp: write error\n");
            fclose(sfp);
            fclose(dfp);
            unlink(final_dst);   // clean up partial file
            return 1;
        }
        total += n;
    }

    fclose(sfp);
    fclose(dfp);

    printf("Copied %s (%s) -> %s\n", src, format_size(total), final_dst);
    return 0;
}

/* ================================================================
 * Command: mv <source> <destination>
 *
 * Move / rename a file or directory. Uses the POSIX rename()
 * underneath, which the VFS layer routes to the correct FS driver.
 * If destination is an existing directory the source is moved
 * inside it, keeping its original name.
 * ================================================================ */
static int handle_mv(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: mv <source> <destination>\n");
        return 1;
    }

    char src[256], dst[256], final_dst[320];

    if (!resolve_path(argv[1], src, sizeof(src))) {
        printf("mv: source path too long\n");
        return 1;
    }
    if (!resolve_path(argv[2], dst, sizeof(dst))) {
        printf("mv: destination path too long\n");
        return 1;
    }

    if (is_directory(dst)) {
        snprintf(final_dst, sizeof(final_dst), "%s/%s",
                 dst, path_basename(src));
    } else {
        strncpy(final_dst, dst, sizeof(final_dst));
        final_dst[sizeof(final_dst) - 1] = '\0';
    }

    if (rename(src, final_dst) != 0) {
        printf("mv: cannot move '%s' to '%s': %s\n",
               src, final_dst, strerror(errno));
        return 1;
    }
    return 0;
}

/* ================================================================
 * Command: df
 *
 * Show disk usage for all mounted filesystems.
 * Uses POSIX statvfs() where available; falls back to just
 * reporting the mount point as "mounted".
 * ================================================================ */
static int handle_df(int argc, char **argv)
{
    printf("%-20s %10s %10s %10s %6s\n",
           "Filesystem", "Total", "Used", "Free", "Use%");
    printf("-------------------- ---------- ---------- ---------- ------\n");

    int found = 0;
    for (int i = 0; MOUNT_POINTS[i] != NULL; i++) {
        if (!is_mounted(MOUNT_POINTS[i])) {
            continue;
        }

        bool     have_info = false;
        size_t   total = 0, used = 0, free = 0;

        /* ── Method 1: POSIX statvfs ── */
        struct statvfs sv;
        if (statvfs(MOUNT_POINTS[i], &sv) == 0) {
            size_t frag = sv.f_frsize > 0 ? sv.f_frsize : sv.f_bsize;
            if (frag > 0 && sv.f_blocks > 0) {
                total = (size_t)sv.f_blocks * frag;
                free  = (size_t)sv.f_bfree  * frag;
                used  = total - free;
                have_info = true;
            }
        }

        /* ── Method 2: FAT‑specific (esp_vfs_fat_info) ── */
        if (!have_info) {
            uint64_t fat_total, fat_free;
            if (esp_vfs_fat_info(MOUNT_POINTS[i],
                                 &fat_total, &fat_free) == ESP_OK) {
                total = (size_t)fat_total;
                free  = (size_t)fat_free;
                used  = total - free;
                have_info = true;
            }
        }

        if (have_info) {
            int pct = (total > 0) ? (int)((used * 100) / total) : 0;
            printf("%-20s %10s %10s %10s %5d%%\n",
                   MOUNT_POINTS[i],
                   format_size(total),
                   format_size(used),
                   format_size(free),
                   pct);
        } else {
            printf("%-20s %10s %10s %10s %6s  (%s)\n",
                   MOUNT_POINTS[i],
                   "N/A", "N/A", "N/A", "N/A",
                   strerror(errno));
        }
        found++;
    }

    if (found == 0) {
        printf("  (no filesystems mounted)\n");
    }
    return 0;
}

/* ================================================================
 * Command registration — called once from main during init
 * ================================================================ */

void register_fs(void)
{
    /* Initialise working directories */
    strcpy(cwd, "/");
    strcpy(old_cwd, "/");

    const esp_console_cmd_t cmds[] = {
        {
            .command = "ls",
            .help    = "List directory contents",
            .hint    = "[path]",
            .func    = handle_ls,
        },
        {
            .command = "cd",
            .help    = "Change working directory (cd - to go back)",
            .hint    = "[path|-]",
            .func    = handle_cd,
        },
        {
            .command = "pwd",
            .help    = "Print current working directory",
            .hint    = NULL,
            .func    = handle_pwd,
        },
        {
            .command = "cat",
            .help    = "Print file contents to stdout",
            .hint    = "<file>",
            .func    = handle_cat,
        },
        {
            .command = "rm",
            .help    = "Delete a file",
            .hint    = "<file>",
            .func    = handle_rm,
        },
        {
            .command = "touch",
            .help    = "Create an empty file / update timestamp",
            .hint    = "<file>",
            .func    = handle_touch,
        },
        {
            .command = "mkdir",
            .help    = "Create a new directory",
            .hint    = "<dir>",
            .func    = handle_mkdir,
        },
        {
            .command = "cp",
            .help    = "Copy a file",
            .hint    = "<src> <dst>",
            .func    = handle_cp,
        },
        {
            .command = "mv",
            .help    = "Move / rename a file or directory",
            .hint    = "<src> <dst>",
            .func    = handle_mv,
        },
        {
            .command = "df",
            .help    = "Show disk usage of mounted filesystems",
            .hint    = NULL,
            .func    = handle_df,
        },
    };

    int n = sizeof(cmds) / sizeof(cmds[0]);
    for (int i = 0; i < n; i++) {
        esp_err_t ret = esp_console_cmd_register(&cmds[i]);
        if (ret != ESP_OK) {
            printf("register_fs: failed to register '%s' (err %d)\n",
                   cmds[i].command, ret);
        }
    }
}