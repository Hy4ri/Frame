#define _DEFAULT_SOURCE
#include "app.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>

/* Supported image extensions for directory scanning */
static const char *supported_extensions[] = {
    ".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp",
    ".tiff", ".tif", ".ico", ".apng", NULL
};

struct AppState {
    char **images;       /* NULL-terminated array of full paths (dynamically allocated) */
    int count;           /* number of entries */
    int current_index;   /* 0-based index of currently displayed image, -1 if none */
    char *initial_path;  /* from CLI, may be NULL */
};

/* ---- helpers ---- */

/* Safely extract the directory name from a path.
   dirname() on Linux may modify its argument and return a static buffer,
   so we operate on a strdup'd copy. The caller must free the result. */
static char *get_dirname(const char *path) {
    char *copy = strdup(path);
    if (!copy) return NULL;
    char *dir = dirname(copy);
    char *result = strdup(dir);
    free(copy);
    return result;
}

/* Check whether a file path has a supported image extension.
   Performs case-insensitive comparison. */
static bool is_supported_extension(const char *path) {
    if (!path) return false;
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) return false;

    for (int i = 0; supported_extensions[i] != NULL; i++) {
        if (strcasecmp(dot, supported_extensions[i]) == 0)
            return true;
    }
    return false;
}

/* qsort comparison: standard strcmp for file paths. */
static int compare_paths(const void *a, const void *b) {
    const char * const *pa = (const char * const *)a;
    const char * const *pb = (const char * const *)b;
    return strcmp(*pa, *pb);
}

/* ---- public API ---- */

AppState *app_create(const char *initial_path) {
    AppState *app = (AppState *)calloc(1, sizeof(AppState));
    if (!app) return NULL;

    if (initial_path) {
        app->initial_path = strdup(initial_path);
        if (!app->initial_path) {
            free(app);
            return NULL;
        }
    } else {
        app->initial_path = NULL;
    }

    app->images = NULL;
    app->count = 0;
    app->current_index = -1;

    return app;
}

void app_destroy(AppState *app) {
    if (!app) return;

    free(app->initial_path);

    if (app->images) {
        for (int i = 0; i < app->count; i++) {
            free(app->images[i]);
        }
        free(app->images);
    }

    free(app);
}

void app_load_directory(AppState *app, const char *path) {
    if (!app || !path) return;

    /* Resolve to an absolute canonical path so relative paths like "image3.png"
       match the full paths we build during directory scanning. */
    char *resolved = realpath(path, NULL);
    if (!resolved) {
        fprintf(stderr, "app_load_directory: cannot resolve '%s'\n", path);
        return;
    }

    struct stat path_stat;
    if (stat(resolved, &path_stat) != 0) {
        fprintf(stderr, "app_load_directory: cannot stat '%s'\n", resolved);
        free(resolved);
        return;
    }

    char *dir = NULL;
    char *target_file = NULL;

    if (S_ISDIR(path_stat.st_mode)) {
        /* path is a directory */
        dir = strdup(resolved);
        target_file = NULL;
    } else if (S_ISREG(path_stat.st_mode)) {
        /* path is a regular file — extract directory and remember target */
        dir = get_dirname(resolved);
        target_file = strdup(resolved);
        if (!dir || !target_file) {
            free(dir);
            free(target_file);
            free(resolved);
            return;
        }
    } else {
        fprintf(stderr, "app_load_directory: not a file or directory: '%s'\n", resolved);
        free(resolved);
        return;
    }

    free(resolved);

    /* Scan the directory */
    DIR *dp = opendir(dir);
    if (!dp) {
        fprintf(stderr, "app_load_directory: cannot open directory '%s'\n", dir);
        free(dir);
        free(target_file);
        return;
    }

    /* Temporary dynamic array for collected paths */
    char **new_images = NULL;
    int new_count = 0;
    int new_capacity = 0;

    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        /* Skip directories */
        if (entry->d_type == DT_DIR) continue;

        const char *name = entry->d_name;

        /* Check extension */
        if (!is_supported_extension(name)) continue;

        /* Build full path: dir + "/" + name */
        size_t dir_len = strlen(dir);
        size_t name_len = strlen(name);
        char *full_path = (char *)malloc(dir_len + 1 + name_len + 1);
        if (!full_path) continue;
        memcpy(full_path, dir, dir_len);
        full_path[dir_len] = '/';
        memcpy(full_path + dir_len + 1, name, name_len + 1);

        /* Double-check it's not a directory via stat */
        struct stat st;
        if (stat(full_path, &st) != 0 || S_ISDIR(st.st_mode)) {
            free(full_path);
            continue;
        }

        /* Append to dynamic array */
        if (new_count >= new_capacity) {
            int new_cap = new_capacity ? new_capacity * 2 : 64;
            char **tmp = (char **)realloc(new_images, (size_t)new_cap * sizeof(char *));
            if (!tmp) {
                free(full_path);
                continue;
            }
            new_images = tmp;
            new_capacity = new_cap;
        }
        new_images[new_count++] = full_path;
    }

    closedir(dp);
    free(dir);

    /* Sort the collected paths */
    if (new_count > 0) {
        qsort(new_images, (size_t)new_count, sizeof(char *), compare_paths);
    }

    /* Find the target file index if we have one */
    int new_current = -1;
    if (target_file && new_count > 0) {
        for (int i = 0; i < new_count; i++) {
            if (strcmp(new_images[i], target_file) == 0) {
                new_current = i;
                break;
            }
        }
    }

    /* Replace old state */
    if (app->images) {
        for (int i = 0; i < app->count; i++) {
            free(app->images[i]);
        }
        free(app->images);
    }

    if (new_count == 0) {
        free(new_images);
        app->images = NULL;
        app->count = 0;
        app->current_index = -1;
    } else {
        /* NULL-terminate for safety */
        char **final_images = (char **)realloc(new_images, (size_t)(new_count + 1) * sizeof(char *));
        if (final_images) {
            final_images[new_count] = NULL;
            app->images = final_images;
        } else {
            app->images = new_images;
        }
        app->count = new_count;
        app->current_index = new_current >= 0 ? new_current : 0;
    }

    free(target_file);
}

int app_image_count(const AppState *app) {
    return app ? app->count : 0;
}

int app_current_index(const AppState *app) {
    if (!app || app->current_index < 0) return 0;
    return app->current_index + 1; /* 1-based for display */
}

const char *app_current_path(const AppState *app) {
    if (!app || app->current_index < 0 || !app->images) return NULL;
    return app->images[app->current_index];
}

void app_next_image(AppState *app) {
    if (!app || app->current_index < 0) return;
    if (app->current_index < app->count - 1) {
        app_display_image(app, app->current_index + 1);
    }
}

void app_prev_image(AppState *app) {
    if (!app || app->current_index <= 0) return;
    app_display_image(app, app->current_index - 1);
}

void app_first_image(AppState *app) {
    if (!app) return;
    app_display_image(app, 0);
}

void app_last_image(AppState *app) {
    if (!app) return;
    app_display_image(app, app->count - 1);
}

bool app_display_image(AppState *app, int index) {
    if (!app || app->count == 0) return false;

    if (index < 0) index = 0;
    if (index >= app->count) index = app->count - 1;

    app->current_index = index;
    return true;
}

bool app_remove_current(AppState *app) {
    if (!app || app->count == 0 || app->current_index < 0) return false;

    int idx = app->current_index;

    /* Free the path string */
    free(app->images[idx]);

    /* Shift remaining entries left */
    for (int i = idx; i < app->count - 1; i++) {
        app->images[i] = app->images[i + 1];
    }

    app->count--;

    if (app->count == 0) {
        app->current_index = -1;
    } else if (idx >= app->count) {
        /* We removed the last item */
        app->current_index = app->count - 1;
    }
    /* Otherwise, current_index stays at idx (which now holds the next image) */

    return true;
}

void app_rename_current(AppState *app, const char *new_path) {
    if (!app || app->current_index < 0 || !new_path) return;

    /* Free the old path and replace */
    free(app->images[app->current_index]);
    app->images[app->current_index] = strdup(new_path);
    if (!app->images[app->current_index]) return;

    /* Re-sort */
    qsort(app->images, (size_t)app->count, sizeof(char *), compare_paths);

    /* Find the renamed file's new index */
    for (int i = 0; i < app->count; i++) {
        if (strcmp(app->images[i], new_path) == 0) {
            app->current_index = i;
            return;
        }
    }

    /* Should not happen, but if not found, pin to 0 */
    app->current_index = 0;
}

const char *app_initial_path(const AppState *app) {
    return app ? app->initial_path : NULL;
}

const char *app_image_path(const AppState *app, int index) {
    if (!app || index < 0 || index >= app->count) return NULL;
    return app->images[index];
}
