#include "viewer.h"
#include "loader.h"
#include "cache.h"
#include "anim.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

struct Viewer {
    SDL_Renderer *renderer;      /* borrowed */
    SDL_Texture *texture;        /* current display texture (owned) */

    /* Original and rotated surfaces */
    SDL_Surface *original;       /* decoded image, rotation=0 */
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

/* Create the rotated surface and upload to a texture.
   Frees old rotated surface and texture first. */
static void viewer_apply_rotation(Viewer *v)
{
    SDL_DestroySurface(v->rotated);
    SDL_DestroyTexture(v->texture);
    v->rotated = NULL;
    v->texture = NULL;

    if (!v->original) {
        return;
    }

    if (v->rotation_degrees == 0) {
        /* Convert to RGBA8888 to handle indexed/palettized formats */
        v->rotated = SDL_ConvertSurface(v->original, SDL_PIXELFORMAT_RGBA8888);
    } else {
        v->rotated = rotate_surface(v->original, v->rotation_degrees);
    }

    if (v->rotated) {
        v->texture = SDL_CreateTextureFromSurface(v->renderer, v->rotated);
        if (v->texture) {
            SDL_SetTextureScaleMode(v->texture, SDL_SCALEMODE_LINEAR);
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
    v->cache = cache_create(10);
    return v;
}

void viewer_destroy(Viewer *v)
{
    if (!v) return;
    viewer_clear(v);
    cache_destroy(v->cache);
    free(v);
}

void viewer_load_image(Viewer *v, const char *path)
{
    if (!v || !path) return;

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

    /* Free old surfaces and texture */
    SDL_DestroySurface(v->rotated);
    SDL_DestroyTexture(v->texture);
    SDL_DestroySurface(v->original);
    v->rotated = NULL;
    v->texture = NULL;
    v->original = NULL;

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
        v->original = SDL_DuplicateSurface(cached);
        if (!v->original) return;
    } else {
        /* Cache miss — load from file */
        v->original = loader_load_static(path);
        if (!v->original) return;

        /* Put into cache (cache takes ownership of the duplicate) */
        SDL_Surface *cache_copy = SDL_DuplicateSurface(v->original);
        if (cache_copy) {
            cache_put(v->cache, path, cache_copy);
        }
    }

    /* Check dimensions against maximum */
    if (v->original->w > VIEWER_MAX_DIMENSION || v->original->h > VIEWER_MAX_DIMENSION) {
        fprintf(stderr, "Image dimensions (%dx%d) exceed maximum (%d)\n",
                v->original->w, v->original->h, VIEWER_MAX_DIMENSION);
        SDL_DestroySurface(v->original);
        v->original = NULL;
        return;
    }

    /* Create initial rotated surface and texture */
    viewer_apply_rotation(v);
}

void viewer_clear(Viewer *v)
{
    if (!v) return;
    SDL_DestroyTexture(v->texture);
    v->texture = NULL;
    SDL_DestroySurface(v->original);
    v->original = NULL;
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
    if (!v || !path) return;
    if (cache_get(v->cache, path)) return; /* already cached */
    SDL_Surface *s = loader_load_static(path);
    if (s) {
        cache_put(v->cache, path, s); /* cache takes ownership */
    }
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
    if (!v || v->is_animated || !v->rotated || v->viewport_w == 0 || v->viewport_h == 0) return;

    float w = (float)v->rotated->w;
    float h = (float)v->rotated->h;

    float scale_w = v->viewport_w / w;
    float scale_h = v->viewport_h / h;
    v->scale = (scale_w < scale_h) ? scale_w : scale_h;

    v->offset_x = (v->viewport_w - w * v->scale) / 2.0f;
    v->offset_y = (v->viewport_h - h * v->scale) / 2.0f;
}

void viewer_zoom_original(Viewer *v)
{
    if (!v || v->is_animated || !v->rotated) return;

    v->scale = 1.0f;
    float w = (float)v->rotated->w;
    float h = (float)v->rotated->h;

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
    if (!v->is_animated || !v->animation) return false;

    int count = anim_frame_count(v->animation);
    if (count <= 1) return false;

    int delay = anim_get_delay(v->animation, v->anim_frame);
    Uint64 now = SDL_GetTicks();

    if (now - v->anim_last_tick < (Uint64)delay) {
        return false;
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
        SDL_DestroySurface(v->original);
        v->original = SDL_DuplicateSurface(frame);

        /* Regenerate texture from the new frame */
        if (v->rotation_degrees == 0) {
            SDL_DestroyTexture(v->texture);
            v->texture = SDL_CreateTextureFromSurface(v->renderer, v->original);
            if (v->texture) {
                SDL_SetTextureScaleMode(v->texture, SDL_SCALEMODE_LINEAR);
            }
        } else {
            viewer_apply_rotation(v);
        }
    }

    return true;
}