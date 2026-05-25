#define _DEFAULT_SOURCE
#include "fileops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <libgen.h>
#include <pwd.h>

/* ---- helpers ---- */

/* Safely extract the directory name from a path.
   dirname() on Linux may modify its argument, so we operate on a strdup'd copy.
   The caller must free the result. */
static char *get_dirname_safe(const char *path) {
    char *copy = strdup(path);
    if (!copy) return NULL;
    char *dir = dirname(copy);
    char *result = strdup(dir);
    free(copy);
    return result;
}

/* Get the home directory. Returns a pointer to a static string (do not free)
   or NULL on failure. */
static const char *get_home_dir(void) {
    const char *home = getenv("HOME");
    if (home) return home;

    struct passwd *pw = getpwuid(getuid());
    if (pw) return pw->pw_dir;

    return NULL;
}

/* Ensure a directory exists. Returns 0 on success, -1 on failure.
   Does NOT fail if the directory already exists. */
static int ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        fprintf(stderr, "trash: '%s' exists but is not a directory\n", path);
        return -1;
    }
    if (mkdir(path, 0700) != 0) {
        perror("trash: mkdir");
        return -1;
    }
    return 0;
}

/* ---- public API ---- */

int fileops_trash(const char *path) {
    if (!path) {
        fprintf(stderr, "trash: path is NULL\n");
        return -1;
    }

    const char *home = get_home_dir();
    if (!home) {
        fprintf(stderr, "trash: cannot determine home directory\n");
        return -1;
    }

    /* Build trash directory paths */
    char trash_files[4096];
    char trash_info[4096];
    int ret = snprintf(trash_files, sizeof(trash_files), "%s/.local/share/Trash/files", home);
    if (ret < 0 || (size_t)ret >= sizeof(trash_files)) {
        fprintf(stderr, "trash: trash files path too long\n");
        return -1;
    }
    ret = snprintf(trash_info, sizeof(trash_info), "%s/.local/share/Trash/info", home);
    if (ret < 0 || (size_t)ret >= sizeof(trash_info)) {
        fprintf(stderr, "trash: trash info path too long\n");
        return -1;
    }

    /* Create directories if needed */
    if (ensure_dir(trash_files) != 0) return -1;
    if (ensure_dir(trash_info) != 0) return -1;

    /* Extract the filename from the path */
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;

    /* Find the basename and extension for collision handling */
    const char *dot = strrchr(filename, '.');
    char *base = NULL;
    const char *ext = NULL;
    if (dot && dot != filename) {
        /* Has extension: split at dot */
        size_t base_len = (size_t)(dot - filename);
        base = (char *)malloc(base_len + 1);
        if (!base) {
            fprintf(stderr, "trash: allocation failed\n");
            return -1;
        }
        memcpy(base, filename, base_len);
        base[base_len] = '\0';
        ext = dot;
    } else {
        /* No extension */
        base = strdup(filename);
        if (!base) {
            fprintf(stderr, "trash: allocation failed\n");
            return -1;
        }
        ext = "";
    }

    /* Build the destination path, handling collisions */
    char dest[4096];
    char info_path[4096];
    int suffix = 0;
    struct stat st;

    do {
        if (suffix == 0) {
            ret = snprintf(dest, sizeof(dest), "%s/%s%s", trash_files, base, ext);
        } else {
            ret = snprintf(dest, sizeof(dest), "%s/%s_%d%s", trash_files, base, suffix, ext);
        }
        if (ret < 0 || (size_t)ret >= sizeof(dest)) {
            fprintf(stderr, "trash: destination path too long\n");
            free(base);
            return -1;
        }
        suffix++;
    } while (stat(dest, &st) == 0);

    /* Build .trashinfo path (mirroring the destination filename) */
    const char *dest_filename = strrchr(dest, '/');
    dest_filename = dest_filename ? dest_filename + 1 : dest;

    ret = snprintf(info_path, sizeof(info_path), "%s/%s.trashinfo", trash_info, dest_filename);
    if (ret < 0 || (size_t)ret >= sizeof(info_path)) {
        fprintf(stderr, "trash: info path too long\n");
        free(base);
        return -1;
    }

    /* Write .trashinfo file (write to temp, then rename for atomicity) */
    char info_tmp[4096];
    ret = snprintf(info_tmp, sizeof(info_tmp), "%s/%s.trashinfo.tmp", trash_info, dest_filename);
    if (ret < 0 || (size_t)ret >= sizeof(info_tmp)) {
        fprintf(stderr, "trash: temp info path too long\n");
        free(base);
        return -1;
    }

    /* Get current time in ISO 8601 format */
    char time_buf[64];
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    if (tm_now) {
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S", tm_now);
    } else {
        strcpy(time_buf, "unknown");
    }

    FILE *fp = fopen(info_tmp, "w");
    if (!fp) {
        perror("trash: cannot create trashinfo.tmp");
        free(base);
        return -1;
    }
    fprintf(fp, "[Trash Info]\n");
    fprintf(fp, "Path=%s\n", path);
    fprintf(fp, "DeletionDate=%s\n", time_buf);
    fclose(fp);

    /* Rename temp -> final .trashinfo */
    if (rename(info_tmp, info_path) != 0) {
        perror("trash: rename trashinfo");
        unlink(info_tmp);
        free(base);
        return -1;
    }

    /* Move the file to the trash */
    if (rename(path, dest) != 0) {
        perror("trash: rename file");
        /* Clean up the orphaned .trashinfo */
        unlink(info_path);
        free(base);
        return -1;
    }

    free(base);
    return 0;
}

char *fileops_rename(const char *old_path, const char *new_name) {
    if (!old_path || !new_name) {
        fprintf(stderr, "rename: NULL argument\n");
        return NULL;
    }

    /* Extract directory from old_path */
    char *dir = get_dirname_safe(old_path);
    if (!dir) {
        fprintf(stderr, "rename: failed to extract directory\n");
        return NULL;
    }

    /* Build new full path: dir + "/" + new_name */
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(new_name);
    char *new_path = (char *)malloc(dir_len + 1 + name_len + 1);
    if (!new_path) {
        free(dir);
        fprintf(stderr, "rename: allocation failed\n");
        return NULL;
    }
    memcpy(new_path, dir, dir_len);
    new_path[dir_len] = '/';
    memcpy(new_path + dir_len + 1, new_name, name_len + 1);
    free(dir);

    /* Check if destination already exists */
    struct stat st;
    if (stat(new_path, &st) == 0) {
        fprintf(stderr, "rename: file already exists: %s\n", new_path);
        free(new_path);
        return NULL;
    }

    /* Perform the rename */
    if (rename(old_path, new_path) != 0) {
        perror("rename");
        free(new_path);
        return NULL;
    }

    return new_path; /* caller must free */
}
