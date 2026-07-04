#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#include "viewer.h"
#include "loader.h"
#include "cache.h"
#include "prefetch.h"
#include "anim.h"
#include "app.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

struct Viewer {
    SDL_Renderer *renderer;      /* borrowed */
    SDL_Texture *texture;        /* current display texture (owned) */

    /* Original and rotated surfaces */
    SDL_Surface *original;       /* decoded image, rotation=0 */
    bool owns_original;          /* true if the viewer owns original and must free it */
    SDL_Surface *rotated;        /* cached rotated version */
    int rotation_degrees;        /* 0, 90, 180, 270 */

    /* Zoom/Pan */
    float scale;                 /* 0.1 to 10.0 */
    float offset_x, offset_y;    /* pixel offset of texture in window */

    /* Viewport */
    int viewport_w, viewport_h;

    /* State flags */
    bool needs_fit;              /* recompute fit on next render */
    bool is_animated;

    /* Animation state */
    struct Animation *animation;
    int anim_frame;
    Uint64 anim_last_tick;

    /* Cache for prefetching */
    struct ImageCache *cache;
    struct ImageCache *thumb_cache;
    struct Prefetcher *prefetcher;

    /* Texture reuse size/format tracking */
    int texture_w, texture_h;
    SDL_PixelFormat texture_format;

    /* Thumbnail display tracking */
    char *current_path;
    bool showing_thumbnail;
};

/* ---- internal helpers ---- */

/* Rotate an SDL_Surface by 90, 180, or 270 degrees.
   Returns a new surface that the caller owns, or NULL on failure. */
static SDL_Surface *rotate_surface(SDL_Surface *src, int degrees)
{
    if (degrees == 0) {
        return SDL_DuplicateSurface(src);
    }

    /* Convert to RGBA8888 for uniform pixel handling */
    SDL_Surface *rgb = SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA8888);
    if (!rgb) {
        return NULL;
    }

    int w = rgb->w;
    int h = rgb->h;
    int new_w = w;
    int new_h = h;

    if (degrees == 90 || degrees == 270) {
        new_w = h;
        new_h = w;
    }

    SDL_Surface *dst = SDL_CreateSurface(new_w, new_h, SDL_PIXELFORMAT_RGBA8888);
    if (!dst) {
        SDL_DestroySurface(rgb);
        return NULL;
    }

    SDL_LockSurface(rgb);
    SDL_LockSurface(dst);

    uint8_t *src_pix = (uint8_t *)rgb->pixels;
    uint8_t *dst_pix = (uint8_t *)dst->pixels;
    int sp = rgb->pitch;  /* bytes per row in source */
    int dp = dst->pitch;  /* bytes per row in dest */

    switch (degrees) {
    case 90:
        /* dst[x][y] = src[h-1-y][x] */
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint32_t px = *(uint32_t *)(src_pix + y * sp + x * 4);
                *(uint32_t *)(dst_pix + x * dp + (h - 1 - y) * 4) = px;
            }
        }
        break;
    case 180:
        /* dst[x][y] = src[w-1-x][h-1-y] */
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint32_t px = *(uint32_t *)(src_pix + y * sp + x * 4);
                *(uint32_t *)(dst_pix + (h - 1 - y) * dp + (w - 1 - x) * 4) = px;
            }
        }
        break;
    case 270:
        /* dst[x][y] = src[y][w-1-x] */
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint32_t px = *(uint32_t *)(src_pix + y * sp + x * 4);
                *(uint32_t *)(dst_pix + (w - 1 - x) * dp + y * 4) = px;
            }
        }
        break;
    }

    SDL_UnlockSurface(dst);
    SDL_UnlockSurface(rgb);
    SDL_DestroySurface(rgb);

    return dst;
}

static void update_texture_from_surface(Viewer *v, SDL_Surface *surface)
{
    if (!v || !surface) return;

    if (v->texture && v->texture_w == surface->w && v->texture_h == surface->h && v->texture_format == surface->format) {
        SDL_UpdateTexture(v->texture, NULL, surface->pixels, surface->pitch);
    } else {
        if (v->texture) {
            SDL_DestroyTexture(v->texture);
            v->texture = NULL;
        }

        v->texture = SDL_CreateTexture(v->renderer, surface->format, SDL_TEXTUREACCESS_STATIC, surface->w, surface->h);
        if (v->texture) {
            v->texture_w = surface->w;
            v->texture_h = surface->h;
            v->texture_format = surface->format;
            SDL_UpdateTexture(v->texture, NULL, surface->pixels, surface->pitch);
            SDL_SetTextureScaleMode(v->texture, SDL_SCALEMODE_LINEAR);
        } else {
            v->texture_w = 0;
            v->texture_h = 0;
            v->texture_format = SDL_PIXELFORMAT_UNKNOWN;
        }
    }
}

/* Create the rotated surface and upload to a texture.
   Frees old rotated surface first. Reuses the texture if size/format matches. */
static void viewer_apply_rotation(Viewer *v)
{
    SDL_DestroySurface(v->rotated);
    v->rotated = NULL;

    if (!v->original) {
        if (v->texture) {
            SDL_DestroyTexture(v->texture);
            v->texture = NULL;
            v->texture_w = 0;
            v->texture_h = 0;
            v->texture_format = SDL_PIXELFORMAT_UNKNOWN;
        }
        return;
    }

    if (v->rotation_degrees == 0) {
        update_texture_from_surface(v, v->original);
    } else {
        v->rotated = rotate_surface(v->original, v->rotation_degrees);
        if (v->rotated) {
            update_texture_from_surface(v, v->rotated);
        }
    }
}

/* Zoom from the center of the viewport by a given factor.
   factor > 1.0 = zoom in, factor < 1.0 = zoom out. */
static void zoom_from_center(Viewer *v, float factor)
{
    if (v->is_animated) return;
    float new_scale = v->scale * factor;
    if (new_scale < 0.1f) new_scale = 0.1f;
    if (new_scale > 10.0f) new_scale = 10.0f;

    float cx = v->viewport_w / 2.0f;
    float cy = v->viewport_h / 2.0f;

    float img_x = (cx - v->offset_x) / v->scale;
    float img_y = (cy - v->offset_y) / v->scale;

    v->offset_x = cx - (img_x * new_scale);
    v->offset_y = cy - (img_y * new_scale);
    v->scale = new_scale;
}

/* ---- public API ---- */

Viewer *viewer_create(SDL_Renderer *renderer)
{
    Viewer *v = (Viewer *)calloc(1, sizeof(Viewer));
    if (!v) {
        return NULL;
    }
    v->renderer = renderer;
    v->scale = 1.0f;
    v->rotation_degrees = 0;
    v->needs_fit = true;
    v->offset_x = 0.0f;
    v->offset_y = 0.0f;
    v->cache = cache_create(50, 128 * 1024 * 1024);       /* 128 MB budget */
    v->thumb_cache = cache_create(500, 32 * 1024 * 1024);  /* 32 MB budget */
    v->prefetcher = prefetch_create(v->cache, v->thumb_cache);
    return v;
}

void viewer_destroy(Viewer *v)
{
    if (!v) return;
    viewer_clear(v);
    prefetch_destroy(v->prefetcher);
    cache_destroy(v->cache);
    cache_destroy(v->thumb_cache);
    free(v->current_path);
    free(v);
}

void viewer_load_image(Viewer *v, const char *path)
{
    if (!v || !path) return;

    free(v->current_path);
    v->current_path = strdup(path);
    v->showing_thumbnail = false;

    /* Pin the path in the cache so background prefetch worker won't evict it */
    cache_pin(v->cache, path);

    /* Free existing animation */
    if (v->animation) {
        anim_free(v->animation);
        v->animation = NULL;
    }
    v->is_animated = false;

    /* Reset state */
    v->rotation_degrees = 0;
    v->scale = 1.0f;
    v->offset_x = 0.0f;
    v->offset_y = 0.0f;
    v->needs_fit = true;

    /* Free old surfaces, keeping texture for reuse */
    SDL_DestroySurface(v->rotated);
    if (v->owns_original && v->original) {
        SDL_DestroySurface(v->original);
    }
    v->rotated = NULL;
    v->original = NULL;
    v->owns_original = false;

    /* Try animation first */
    if (loader_is_animated(path)) {
        v->animation = anim_load(path);
        if (v->animation) {
            v->anim_frame = 0;
            v->anim_last_tick = SDL_GetTicks();
            /* Load first frame (before setting is_animated so zoom_fit works) */
            SDL_Surface *frame = anim_get_frame(v->animation, 0);
            if (frame) {
                v->original = SDL_DuplicateSurface(frame);
                if (v->original) {
                    v->owns_original = true;
                    viewer_apply_rotation(v);
                    if (v->viewport_w > 0) {
                        viewer_zoom_fit(v);
                        v->needs_fit = false;
                    }
                }
            }
            v->is_animated = true;
            return;
        }
        /* Animation failed to load — fall through to static loading */
    }

    /* Try cache first */
    SDL_Surface *cached = cache_get(v->cache, path);
    if (cached) {
        v->original = cached;
        v->owns_original = false;
    } else {
        /* Check if a downsampled thumbnail is already in thumb_cache */
        SDL_Surface *thumb = cache_get(v->thumb_cache, path);
        if (thumb) {
            v->original = thumb;
            v->owns_original = false;
            v->showing_thumbnail = true;
        } else {
            /* Cache miss & thumbnail miss — load from file synchronously */
            v->original = loader_load_static(path);
            if (!v->original) {
                viewer_clear(v);
                return;
            }
            v->owns_original = true;

            /* Put into cache only if another thread hasn't cached it in the meantime.
               If successfully cached, the cache takes ownership. */
            SDL_Surface *existing = cache_get(v->cache, path);
            if (!existing) {
                cache_put(v->cache, path, v->original);
                if (cache_get(v->cache, path) == v->original) {
                    v->owns_original = false;
                }
            } else {
                /* Already cached by prefetcher thread, keep our loaded copy as owned */
                v->owns_original = true;
            }
        }
    }

    /* Check dimensions against maximum */
    if (v->original->w > VIEWER_MAX_DIMENSION || v->original->h > VIEWER_MAX_DIMENSION) {
        fprintf(stderr, "Image dimensions (%dx%d) exceed maximum (%d)\n",
                v->original->w, v->original->h, VIEWER_MAX_DIMENSION);
        if (v->owns_original) {
            SDL_DestroySurface(v->original);
        }
        v->original = NULL;
        v->owns_original = false;
        return;
    }

    /* Create initial rotated surface and texture */
    viewer_apply_rotation(v);
}

void viewer_clear(Viewer *v)
{
    if (!v) return;
    cache_pin(v->cache, NULL);
    SDL_DestroyTexture(v->texture);
    v->texture = NULL;
    v->texture_w = 0;
    v->texture_h = 0;
    v->texture_format = SDL_PIXELFORMAT_UNKNOWN;
    if (v->owns_original && v->original) {
        SDL_DestroySurface(v->original);
    }
    v->original = NULL;
    v->owns_original = false;
    SDL_DestroySurface(v->rotated);
    v->rotated = NULL;
    v->rotation_degrees = 0;
    v->scale = 1.0f;
    v->offset_x = 0.0f;
    v->offset_y = 0.0f;
    v->needs_fit = true;
    if (v->animation) {
        anim_free(v->animation);
        v->animation = NULL;
    }
    v->is_animated = false;
}

void viewer_render(Viewer *v, SDL_Renderer *renderer)
{
    if (!v) return;

    /* Clear with dark background */
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderClear(renderer);

    if (!v->texture) return;

    /* If needs_fit and we have a viewport, recompute fit */
    if (v->needs_fit && v->viewport_w > 0 && v->viewport_h > 0) {
        viewer_zoom_fit(v);
        v->needs_fit = false;
    }

    /* Get texture size */
    float tex_w, tex_h;
    SDL_GetTextureSize(v->texture, &tex_w, &tex_h);

    float w = tex_w * v->scale;
    float h = tex_h * v->scale;

    SDL_FRect dst = { v->offset_x, v->offset_y, w, h };
    SDL_RenderTexture(renderer, v->texture, NULL, &dst);
}

void viewer_handle_resize(Viewer *v, int new_w, int new_h)
{
    if (!v) return;
    bool changed = (v->viewport_w != new_w || v->viewport_h != new_h);
    v->viewport_w = new_w;
    v->viewport_h = new_h;
    if (changed && v->original) {
        v->needs_fit = true;
    }
}

void viewer_prefetch(Viewer *v, const char *path)
{
    /* Legacy synchronous prefetch — now a no-op.
       Background prefetching is handled by viewer_prefetch_around(). */
    (void)v;
    (void)path;
}

void viewer_prefetch_around(Viewer *v, struct AppState *app)
{
    if (!v || !app || !v->prefetcher) return;

    int count = app_image_count(app);
    if (count <= 1) return;

    int center = app_current_index(app) - 1; /* convert 1-based to 0-based */

    /* Build nearest-first list: current first (if not cached), then +1, -1, +2, -2, … +5, -5 */
    const char *paths[11];
    int n = 0;

    const char *current_path = app_image_path(app, center);
    if (current_path && !cache_get(v->cache, current_path)) {
        paths[n++] = current_path;
    }

    for (int d = 1; d <= 5; d++) {
        int fwd = center + d;
        if (fwd >= 0 && fwd < count)
            paths[n++] = app_image_path(app, fwd);

        int bwd = center - d;
        if (bwd >= 0 && bwd < count)
            paths[n++] = app_image_path(app, bwd);
    }

    if (n > 0)
        prefetch_submit(v->prefetcher, paths, n);
}

bool viewer_is_cached(Viewer *v, const char *path)
{
    if (!v || !path) return false;
    return cache_get(v->cache, path) != NULL;
}

/* ---- Zoom ---- */

void viewer_zoom_in(Viewer *v)
{
    if (!v) return;
    zoom_from_center(v, 1.05f);
}

void viewer_zoom_out(Viewer *v)
{
    if (!v) return;
    zoom_from_center(v, 1.0f / 1.05f);
}

void viewer_scroll_zoom(Viewer *v, float mx, float my, float dy)
{
    if (!v || v->is_animated) return;

    float factor = 1.0f + dy * 0.01f;
    if (factor < 0.5f) factor = 0.5f;
    if (factor > 2.0f) factor = 2.0f;

    float new_scale = v->scale * factor;
    if (new_scale < 0.1f) new_scale = 0.1f;
    if (new_scale > 10.0f) new_scale = 10.0f;

    /* Zoom toward mouse cursor */
    float img_x = (mx - v->offset_x) / v->scale;
    float img_y = (my - v->offset_y) / v->scale;

    v->offset_x = mx - (img_x * new_scale);
    v->offset_y = my - (img_y * new_scale);
    v->scale = new_scale;
}

void viewer_zoom_fit(Viewer *v)
{
    if (!v || v->is_animated || v->viewport_w == 0 || v->viewport_h == 0) return;
    SDL_Surface *ref = v->rotated ? v->rotated : v->original;
    if (!ref) return;

    float w = (float)ref->w;
    float h = (float)ref->h;

    float scale_w = v->viewport_w / w;
    float scale_h = v->viewport_h / h;
    v->scale = (scale_w < scale_h) ? scale_w : scale_h;

    v->offset_x = (v->viewport_w - w * v->scale) / 2.0f;
    v->offset_y = (v->viewport_h - h * v->scale) / 2.0f;
}

void viewer_zoom_original(Viewer *v)
{
    SDL_Surface *ref = v->rotated ? v->rotated : v->original;
    if (!v || v->is_animated || !ref) return;

    v->scale = 1.0f;
    float w = (float)ref->w;
    float h = (float)ref->h;

    /* Center if image is smaller than viewport */
    v->offset_x = (v->viewport_w > w) ? (v->viewport_w - w) / 2.0f : 0.0f;
    v->offset_y = (v->viewport_h > h) ? (v->viewport_h - h) / 2.0f : 0.0f;
}

/* ---- Rotation ---- */

void viewer_rotate(Viewer *v, bool clockwise)
{
    if (!v || v->is_animated || !v->original) return;

    if (clockwise) {
        v->rotation_degrees = (v->rotation_degrees + 90) % 360;
    } else {
        v->rotation_degrees = (v->rotation_degrees + 270) % 360;
    }

    viewer_apply_rotation(v);
}

/* ---- Pan (drag) ---- */

void viewer_begin_drag(Viewer *v)
{
    (void)v;
    /* Nothing to initialize — we track deltas, not absolute positions */
}

void viewer_do_drag(Viewer *v, float dx, float dy)
{
    if (!v) return;
    v->offset_x += dx;
    v->offset_y += dy;
}

void viewer_end_drag(Viewer *v)
{
    (void)v;
    /* Nothing to clean up */
}

/* ---- Info ---- */

bool viewer_get_dimensions(const Viewer *v, int *out_w, int *out_h)
{
    if (!v || !out_w || !out_h) return false;
    if (v->original) {
        *out_w = v->original->w;
        *out_h = v->original->h;
        return true;
    }
    return false;
}

/* ---- Animation ---- */

bool viewer_is_animated(const Viewer *v)
{
    return v->is_animated;
}

bool viewer_animation_tick(Viewer *v)
{
    if (!v) return false;

    bool dirty = false;

    /* Check if we can swap the thumbnail for the full resolution image */
    if (v->showing_thumbnail && v->current_path) {
        SDL_Surface *full = cache_get(v->cache, v->current_path);
        if (full) {
            v->original = full;
            v->owns_original = false;
            v->showing_thumbnail = false;
            viewer_apply_rotation(v);
            if (v->viewport_w > 0) {
                viewer_zoom_fit(v);
            }
            dirty = true;
        }
    }

    if (!v->is_animated || !v->animation) {
        return dirty;
    }

    int count = anim_frame_count(v->animation);
    if (count <= 1) return dirty;

    int delay = anim_get_delay(v->animation, v->anim_frame);
    Uint64 now = SDL_GetTicks();

    if (now - v->anim_last_tick < (Uint64)delay) {
        return dirty;
    }

    v->anim_last_tick += delay;

    /* Advance to next frame, wrapping around (infinite loop) */
    v->anim_frame++;
    if (v->anim_frame >= count) {
        v->anim_frame = 0;
    }

    /* Update the display with the new frame */
    SDL_Surface *frame = anim_get_frame(v->animation, v->anim_frame);
    if (frame) {
        if (v->owns_original && v->original) {
            SDL_DestroySurface(v->original);
        }
        v->original = SDL_DuplicateSurface(frame);
        v->owns_original = true;

        /* Regenerate texture from the new frame, reusing it if possible */
        if (v->rotation_degrees == 0) {
            update_texture_from_surface(v, v->original);
        } else {
            viewer_apply_rotation(v);
        }
        dirty = true;
    }

    return dirty;
}