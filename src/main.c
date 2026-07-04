#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "app.h"
#include "viewer.h"
#include "input.h"
#include "overlay.h"

#ifdef _WIN32
/* SDL3 requires SDL_main on some platforms, but we define it ourselves here.
   On Linux with SDL3, we can use a standard main(). */
#undef main
#endif

int main(int argc, char *argv[]) {
    const char *initial_path = (argc > 1) ? argv[1] : NULL;

    /* Initialize SDL */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Create window */
    SDL_Window *window = SDL_CreateWindow(
        "Frame", 1200, 800,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );
    if (!window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    /* Create renderer */
    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Set dark background */
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);

    /* Create application components */
    AppState *app = app_create(initial_path);
    Viewer *viewer = viewer_create(renderer);

    /* Initialize overlay system (fonts) */
    overlay_init();

    /* Load initial directory and display first image */
    if (initial_path) {
        app_load_directory(app, initial_path);
        if (app_current_path(app)) {
            viewer_load_image(viewer, app_current_path(app));
            /* Update window title for initial load */
            const char *path = app_current_path(app);
            const char *name = strrchr(path, '/');
            name = name ? name + 1 : path;
            char title[512];
            snprintf(title, sizeof(title), "%s (%d/%d) - Frame",
                     name, app_current_index(app), app_image_count(app));
            SDL_SetWindowTitle(window, title);
            printf("Loaded %d images. Current: %s\n",
                   app_image_count(app), app_current_path(app));
        } else {
            printf("No supported images found at: %s\n", initial_path);
        }
    } else {
        printf("No path provided. Drag an image onto the window or use:\n");
        printf("  frame /path/to/image.jpg\n");
    }

    /* Track mouse position for scroll zoom */
    float mouse_x = 0.0f, mouse_y = 0.0f;
    bool dragging = false;

    /* Main event loop */
    SDL_Event event;
    bool running = true;
    bool dirty = true;

    while (running) {
        int timeout_ms = viewer_is_animated(viewer) ? 10 : 250;

        if (SDL_WaitEventTimeout(&event, timeout_ms)) {
            do {
                switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;

                case SDL_EVENT_KEY_DOWN:
                    running = input_handle_keyboard(app, viewer, &event.key, window, renderer);
                    dirty = true;
                    break;

                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    viewer_handle_resize(viewer,
                        (int)event.window.data1, (int)event.window.data2);
                    dirty = true;
                    break;

                case SDL_EVENT_WINDOW_EXPOSED:
                    dirty = true;
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        viewer_begin_drag(viewer);
                        dragging = true;
                    }
                    dirty = true;
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        viewer_end_drag(viewer);
                        dragging = false;
                    }
                    dirty = true;
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    mouse_x = event.motion.x;
                    mouse_y = event.motion.y;
                    if (dragging) {
                        viewer_do_drag(viewer, event.motion.xrel, event.motion.yrel);
                        dirty = true;
                    }
                    break;

                case SDL_EVENT_MOUSE_WHEEL:
                    viewer_scroll_zoom(viewer, mouse_x, mouse_y,
                                        event.wheel.y);
                    dirty = true;
                    break;

                default:
                    break;
                }
            } while (SDL_PollEvent(&event));
        }

        /* Advance animation frames */
        if (viewer_animation_tick(viewer)) {
            dirty = true;
        }

        /* Render only if state is dirty */
        if (dirty && running) {
            viewer_render(viewer, renderer);
            overlay_render(renderer);
            SDL_RenderPresent(renderer);
            dirty = false;
        }
    }

    /* Cleanup */
    printf("Shutting down...\n");
    overlay_shutdown();
    viewer_destroy(viewer);
    app_destroy(app);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}