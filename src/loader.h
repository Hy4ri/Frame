#ifndef FRAME_LOADER_H
#define FRAME_LOADER_H

#include <SDL3/SDL.h>
#include <stdbool.h>

/* Supported image extensions for directory scanning */
extern const char *supported_extensions[];

/* Check if a file path has a supported image extension.
   Case-insensitive comparison. */
bool loader_is_supported(const char *path);

/* Check if a file extension indicates an animated format (GIF, APNG).
   Case-insensitive. */
bool loader_is_animated(const char *path);

/* Load a static image from a file. Returns an SDL_Surface or NULL on error.
   The caller owns the returned surface and must call SDL_DestroySurface() to free it.
   This function does NOT check the max dimension limit — the caller should do that. */
SDL_Surface *loader_load_static(const char *path);

/* Load a static image, then convert it to a texture suitable for the given renderer.
   Returns NULL on error. The caller owns the texture and must call SDL_DestroyTexture().
   This is a convenience wrapper around loader_load_static() + SDL_CreateTextureFromSurface(). */
SDL_Texture *loader_load_texture(const char *path, SDL_Renderer *renderer);

/* Maximum image dimension (width or height) allowed, to prevent OOM.
   Images exceeding this should be rejected by the caller. */
#define MAX_IMAGE_DIMENSION 16384

#endif /* FRAME_LOADER_H */
