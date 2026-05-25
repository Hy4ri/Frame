#define _GNU_SOURCE
#include "input.h"
#include "app.h"
#include "viewer.h"
#include "fileops.h"
#include "overlay.h"
#include "utils.h"
#include "exif.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* 'gg' double-tap state */
static bool g_sequence = false;
static Uint64 g_prev_tick = 0;

/* Fullscreen state */
static bool fullscreen_active = false;

void input_reset_gg(void) {
    g_sequence = false;
}

/* --- Keybinding action helpers --- */

/* Prefetch adjacent images into the LRU cache for instant navigation */
static void prefetch_around(struct AppState *app, struct Viewer *viewer) {
    int center = app_current_index(app) - 1; /* 0-based */
    int offsets[] = {1, 2, 3, -1, -2, -3};
    int count = app_image_count(app);

    for (int i = 0; i < 6; i++) {
        int idx = center + offsets[i];
        if (idx >= 0 && idx < count) {
            viewer_prefetch(viewer, app_image_path(app, idx));
        }
    }
}

/* Navigate, reload image, update title, and prefetch neighbors */
static void do_nav(struct AppState *app, struct Viewer *viewer, SDL_Window *window) {
    const char *path = app_current_path(app);
    if (!path) return;
    viewer_load_image(viewer, path);

    /* Update window title: "filename (N/M) - Frame" */
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    char title[512];
    snprintf(title, sizeof(title), "%s (%d/%d) - Frame",
             name, app_current_index(app), app_image_count(app));
    SDL_SetWindowTitle(window, title);

    prefetch_around(app, viewer);
}

/* Run zenity --question for delete confirmation. Returns true if confirmed. */
static bool zenity_confirm(const char *title, const char *text, const char *ok_label) {
    char cmd[1024];
    int ret = snprintf(cmd, sizeof(cmd),
        "zenity --question --title=\"%s\" --text=\"%s\" "
        "--ok-label=\"%s\" --cancel-label=\"Cancel\" 2>/dev/null",
        title, text, ok_label);
    if (ret < 0 || (size_t)ret >= sizeof(cmd)) return false;
    FILE *fp = popen(cmd, "r");
    if (!fp) return false;
    int status = pclose(fp);
    return (status == 0);
}

/* Run zenity --entry for rename. Returns dynamically allocated string, or NULL. */
static char *zenity_entry(const char *title, const char *text, const char *default_text) {
    char cmd[4096];
    int ret = snprintf(cmd, sizeof(cmd),
        "zenity --entry --title=\"%s\" --text=\"%s\" "
        "--entry-text=\"%s\" 2>/dev/null",
        title, text, default_text);
    if (ret < 0 || (size_t)ret >= sizeof(cmd)) return NULL;
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    char buf[1024];
    if (!fgets(buf, sizeof(buf), fp)) {
        pclose(fp);
        return NULL;
    }
    pclose(fp);
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    if (buf[0] == '\0') return NULL;
    return strdup(buf);
}

/* --- Main handler --- */

bool input_handle_keyboard(struct AppState *app, struct Viewer *viewer,
                           const SDL_KeyboardEvent *event, SDL_Window *window) {
    SDL_Keycode key = event->key;
    bool shift = (event->mod & SDL_KMOD_SHIFT) != 0;

    /* Quit first (don't reset gg state for these) */
    if (key == SDLK_Q || key == SDLK_ESCAPE) {
        return false;
    }

    /* If overlay is active, any key dismisses it (without normal action) */
    if (overlay_is_active()) {
        overlay_hide();
        g_sequence = false;
        return true;
    }

    /* === Navigation (arrows + vim keys) === */
    switch (key) {
    case SDLK_LEFT:
    case SDLK_H:
    case SDLK_UP:
    case SDLK_K:
        app_prev_image(app);
        do_nav(app, viewer, window);
        goto reset_gg;
    case SDLK_RIGHT:
    case SDLK_L:
    case SDLK_DOWN:
    case SDLK_J:
        app_next_image(app);
        do_nav(app, viewer, window);
        goto reset_gg;
    default:
        break;
    }

    /* === g/G: first/last image === */
    if (key == SDLK_G) {
        if (shift) {
            /* SHIFT+G = 'G' = last image */
            app_last_image(app);
            do_nav(app, viewer, window);
            g_sequence = false;
            return true;
        }
        /* Lowercase 'g': gg sequence */
        Uint64 now = SDL_GetTicks();
        if (g_sequence && (now - g_prev_tick) < 500) {
            /* Double 'g' within 500ms */
            app_first_image(app);
            do_nav(app, viewer, window);
            g_sequence = false;
            return true;
        }
        g_sequence = true;
        g_prev_tick = now;
        return true;
    }

    /* === View controls === */
    switch (key) {
    case SDLK_F:
        fullscreen_active = !fullscreen_active;
        SDL_SetWindowFullscreen(window, fullscreen_active);
        goto reset_gg;
    case SDLK_EQUALS:
    case SDLK_PLUS:
    case SDLK_Z:
        viewer_zoom_in(viewer);
        goto reset_gg;
    case SDLK_MINUS:
    case SDLK_X:
        viewer_zoom_out(viewer);
        goto reset_gg;
    case SDLK_0:
        viewer_zoom_fit(viewer);
        goto reset_gg;
    case SDLK_1:
        viewer_zoom_original(viewer);
        goto reset_gg;
    default:
        break;
    }

    /* === Rotation === */
    if (key == SDLK_R) {
        viewer_rotate(viewer, !shift); /* r = clockwise, R(shift+r) = counter-clockwise */
        goto reset_gg;
    }

    /* === Delete (d or Delete key) === */
    if (key == SDLK_D || key == SDLK_DELETE) {
        const char *path = app_current_path(app);
        if (!path) goto reset_gg;

        /* 'D' (shift+d) does nothing */
        if (key == SDLK_D && shift) goto reset_gg;

        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;

        char msg[512];
        int ret = snprintf(msg, sizeof(msg), "Move \"%s\" to trash?", name);
        if (ret < 0 || (size_t)ret >= sizeof(msg)) goto reset_gg;

        if (!zenity_confirm("Delete Image", msg, "Move to Trash")) {
            goto reset_gg;
        }

        if (fileops_trash(path) != 0) {
            fprintf(stderr, "input: fileops_trash failed\n");
            goto reset_gg;
        }

        viewer_clear(viewer);
        app_remove_current(app);

        /* Reload next image (if any) and update title */
        const char *next = app_current_path(app);
        if (next) {
            viewer_load_image(viewer, next);
            /* Update window title */
            const char *name = strrchr(next, '/');
            name = name ? name + 1 : next;
            char title[512];
            snprintf(title, sizeof(title), "%s (%d/%d) - Frame",
                     name, app_current_index(app), app_image_count(app));
            SDL_SetWindowTitle(window, title);
            prefetch_around(app, viewer);
        } else {
            SDL_SetWindowTitle(window, "Frame");
        }
        goto reset_gg;
    }

    /* === Rename (F2) === */
    if (key == SDLK_F2) {
        const char *path = app_current_path(app);
        if (!path) goto reset_gg;

        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;

        char *new_name = zenity_entry("Rename Image", "New name:", name);
        if (!new_name) goto reset_gg;

        /* Validate name */
        if (new_name[0] == '\0' || strchr(new_name, '/') ||
            strcmp(new_name, ".") == 0 || strcmp(new_name, "..") == 0) {
            fprintf(stderr, "Invalid filename\n");
            free(new_name);
            goto reset_gg;
        }

        char *new_path = fileops_rename(path, new_name);
        if (!new_path) {
            fprintf(stderr, "Rename failed\n");
            free(new_name);
            goto reset_gg;
        }

        app_rename_current(app, new_path);
        do_nav(app, viewer, window);
        free(new_name);
        free(new_path);
        goto reset_gg;
    }

    /* === Info (i) === */
    if (key == SDLK_I) {
        if (shift) goto reset_gg; /* 'I' does nothing */
        const char *path = app_current_path(app);
        if (!path) goto reset_gg;

        char info_text[4096];
        info_text[0] = '\0';

        /* Get file stats */
        struct stat st;
        if (stat(path, &st) == 0) {
            char *size_str = format_file_size((long long)st.st_size);
            char time_buf[64];
            struct tm *tm_info = localtime(&st.st_mtime);
            if (tm_info) {
                strftime(time_buf, sizeof(time_buf), "%a, %d %b %Y %H:%M:%S %Z", tm_info);
            } else {
                time_buf[0] = '\0';
            }

            const char *name = strrchr(path, '/');
            name = name ? name + 1 : path;
            const char *ext = strrchr(name, '.');
            const char *format_name = ext ? format_from_ext(ext) : "Unknown";

            /* Get image dimensions */
            int img_w = 0, img_h = 0;
            viewer_get_dimensions(viewer, &img_w, &img_h);

            /* Get EXIF data */
            char *exif_text = exif_get_data(path);

            snprintf(info_text, sizeof(info_text),
                "File:       %s\n"
                "Size:       %s\n"
                "Dimensions: %dx%d\n"
                "Format:     %s\n"
                "Modified:   %s\n"
                "Index:      %d / %d\n"
                "%s%s",
                name, size_str,
                img_w, img_h,
                format_name, time_buf,
                app_current_index(app), app_image_count(app),
                exif_text ? "EXIF:\n" : "",
                exif_text ? exif_text : "");

            free(size_str);
            free(exif_text);
        }

        overlay_show_info("Image Information", info_text);
        goto reset_gg;
    }

    /* === Help (?) === */
    if (key == SDLK_SLASH) {
        if (!shift) goto reset_gg; /* '/' does nothing, only '?' (shift+/) */
        overlay_show_help();
        goto reset_gg;
    }

    /* Fallthrough for unknown keys */
    goto reset_gg;

reset_gg:
    g_sequence = false;
    return true;
}
