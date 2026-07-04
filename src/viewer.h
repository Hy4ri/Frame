#ifndef FRAME_VIEWER_H
#define FRAME_VIEWER_H

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct Viewer Viewer;
struct AppState;

/* Maximum image dimension to prevent OOM */
#define VIEWER_MAX_DIMENSION 16384

/* Create a viewer. The renderer is borrowed (not owned) — must outlive the viewer. */
Viewer *viewer_create(SDL_Renderer *renderer);

/* Destroy and free all resources. */
void viewer_destroy(Viewer *v);

/* Load and display a static image. If the path is animated, load the first frame.
   Triggers fit-to-window if needs_fit is set. */
void viewer_load_image(Viewer *v, const char *path);

/* Clear the current image (shows dark background). */
void viewer_clear(Viewer *v);

/* Render the current image. Call once per frame from the main loop. */
void viewer_render(Viewer *v, SDL_Renderer *renderer);

/* Notify viewer that the window was resized (for fit recalculation). */
void viewer_handle_resize(Viewer *v, int new_w, int new_h);

/* Legacy single-path prefetch (synchronous, kept for compatibility). */
void viewer_prefetch(Viewer *v, const char *path);

/* Submit the ±5 neighbourhood around the current image for background
   prefetching.  Call after every navigation or deletion. */
void viewer_prefetch_around(Viewer *v, struct AppState *app);

/* Check if an image is already in the cache (non-blocking). */
bool viewer_is_cached(Viewer *v, const char *path);

/* --- Zoom --- */
void viewer_zoom_in(Viewer *v);       /* +5% from center */
void viewer_zoom_out(Viewer *v);      /* -5% from center */
void viewer_zoom_fit(Viewer *v);      /* fit to viewport, preserving aspect ratio */
void viewer_zoom_original(Viewer *v); /* 1:1 pixel mapping */

/* Zoom toward a specific point (mouse wheel zoom).
   mx, my: mouse position in window coordinates.
   dy > 0: zoom in, dy < 0: zoom out */
void viewer_scroll_zoom(Viewer *v, float mx, float my, float dy);

/* --- Rotation --- */
/* Rotate 90 degrees clockwise (true) or counter-clockwise (false).
   For animated images, rotation is ignored. */
void viewer_rotate(Viewer *v, bool clockwise);

/* --- Pan (drag) --- */
void viewer_begin_drag(Viewer *v);
void viewer_do_drag(Viewer *v, float dx, float dy);
void viewer_end_drag(Viewer *v);

/* --- Info --- */
/* Get the dimensions of the currently loaded image.
   Returns false if no image is loaded. */
bool viewer_get_dimensions(const Viewer *v, int *out_w, int *out_h);

/* --- Animation support --- */
/* Check if the currently loaded image is animated. */
bool viewer_is_animated(const Viewer *v);

/* Handle animation frame advancement. Call once per frame BEFORE viewer_render.
   Returns true if the frame changed (caller should re-render). */
bool viewer_animation_tick(Viewer *v);

/* Check if the viewer needs active background ticking (for animation or thumbnail swap). */
bool viewer_needs_tick(const Viewer *v);

#endif /* FRAME_VIEWER_H */
