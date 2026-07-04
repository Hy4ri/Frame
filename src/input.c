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

/* Prefetch is now handled asynchronously via viewer_prefetch_around(). */

static Uint64 last_nav_ticks = 0;
static bool nav_pending_load = false;

bool input_nav_pending(void) {
    return nav_pending_load;
}

bool input_check_and_trigger_nav(struct AppState *app, struct Viewer *viewer) {
    if (!nav_pending_load) return false;
    Uint64 now = SDL_GetTicks();
    if (now - last_nav_ticks >= 80) {
        const char *path = app_current_path(app);
        if (path) {
            viewer_load_image(viewer, path);
            viewer_prefetch_around(viewer, app);
        }
        nav_pending_load = false;
        return true;
    }
    return false;
}

/* Navigate, reload image, update title, and prefetch neighbors */
static bool do_nav(struct AppState *app, struct Viewer *viewer, SDL_Window *window) {
    const char *path = app_current_path(app);
    if (!path) return false;

    Uint64 now = SDL_GetTicks();
    Uint64 delta = now - last_nav_ticks;
    last_nav_ticks = now;

    /* If user navigates faster than every 80ms, we are in rapid scroll mode. */
    bool rapid = (delta < 80);
    bool loaded = false;

    if (!rapid) {
        viewer_load_image(viewer, path);
        nav_pending_load = false;
        loaded = true;
    } else {
        /* In rapid scroll mode, only load if we have a cached full image or a cached thumbnail */
        if (viewer_is_thumb_cached(viewer, path)) {
            viewer_load_image(viewer, path);
            loaded = true;
        }
        nav_pending_load = true;
    }

    /* Update window title: "filename (N/M) - Frame" */
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    char title[512];
    snprintf(title, sizeof(title), "%s (%d/%d) - Frame",
             name, app_current_index(app), app_image_count(app));
    SDL_SetWindowTitle(window, title);

    /* Prefetch neighbors only if we performed a full load (not in rapid scroll) */
    if (!rapid) {
        viewer_prefetch_around(viewer, app);
    }

    return loaded;
}

/* --- Main handler --- */

bool input_handle_keyboard(struct AppState *app, struct Viewer *viewer,
                           const SDL_KeyboardEvent *event,
                           SDL_Window *window, SDL_Renderer *renderer,
                           bool *out_dirty) {
    SDL_Keycode key = event->key;
    bool shift = (event->mod & SDL_KMOD_SHIFT) != 0;

    if (out_dirty) {
        *out_dirty = false;
    }

    /* Quit first (don't reset gg state for these) */
    if (key == SDLK_Q || key == SDLK_ESCAPE) {
        return false;
    }

    /* If overlay is active, any key dismisses it (without normal action) */
    if (overlay_is_active()) {
        overlay_hide();
        g_sequence = false;
        if (out_dirty) *out_dirty = true;
        return true;
    }

    /* === Navigation (arrows + vim keys) === */
    switch (key) {
    case SDLK_LEFT:
    case SDLK_H:
    case SDLK_UP:
    case SDLK_K:
        app_prev_image(app);
        if (out_dirty) *out_dirty = do_nav(app, viewer, window);
        goto reset_gg;
    case SDLK_RIGHT:
    case SDLK_L:
    case SDLK_DOWN:
    case SDLK_J:
        app_next_image(app);
        if (out_dirty) *out_dirty = do_nav(app, viewer, window);
        goto reset_gg;
    default:
        break;
    }

    /* === g/G: first/last image === */
    if (key == SDLK_G) {
        if (shift) {
            /* SHIFT+G = 'G' = last image */
            app_last_image(app);
            if (out_dirty) *out_dirty = do_nav(app, viewer, window);
            g_sequence = false;
            return true;
        }
        /* Lowercase 'g': gg sequence */
        Uint64 now = SDL_GetTicks();
        if (g_sequence && (now - g_prev_tick) < 500) {
            /* Double 'g' within 500ms */
            app_first_image(app);
            if (out_dirty) *out_dirty = do_nav(app, viewer, window);
            g_sequence = false;
            return true;
        }
        g_sequence = true;
        g_prev_tick = now;
        if (out_dirty) *out_dirty = false;
        return true;
    }

    /* Set default dirty flag for other actions */
    if (out_dirty) {
        *out_dirty = true;
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

        if (!overlay_modal_confirm("Delete Image", msg, renderer)) {
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
            viewer_prefetch_around(viewer, app);
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

        char *new_name = overlay_modal_entry("Rename Image", name, renderer, window);
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
