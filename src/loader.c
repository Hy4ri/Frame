#include "loader.h"
#include <SDL3_image/SDL_image.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* NULL-terminated array of supported image extensions */
const char *supported_extensions[] = {
    ".jpg", ".jpeg", ".png", ".gif", ".webp",
    ".bmp", ".tiff", ".tif", ".ico", ".apng",
    NULL
};

/* Extract the file extension from a path (char after last '.', with '.').
   Returns NULL if no extension found. */
static const char *get_ext(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path)
        return NULL;
    return dot;
}

bool loader_is_supported(const char *path)
{
    const char *ext = get_ext(path);
    if (!ext)
        return false;

    for (int i = 0; supported_extensions[i] != NULL; i++) {
        if (strcasecmp(ext, supported_extensions[i]) == 0)
            return true;
    }
    return false;
}

bool loader_is_animated(const char *path)
{
    const char *ext = get_ext(path);
    if (!ext)
        return false;

    return (strcasecmp(ext, ".gif") == 0 ||
            strcasecmp(ext, ".apng") == 0);
}

SDL_Surface *loader_load_static(const char *path)
{
    SDL_Surface *surface = IMG_Load(path);
    if (!surface) {
        fprintf(stderr, "IMG_Load failed for '%s': %s\n", path, SDL_GetError());
        return NULL;
    }
    return surface;
}

SDL_Texture *loader_load_texture(const char *path, SDL_Renderer *renderer)
{
    SDL_Surface *surface = loader_load_static(path);
    if (!surface)
        return NULL;

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    if (!texture) {
        fprintf(stderr, "SDL_CreateTextureFromSurface failed for '%s': %s\n", path, SDL_GetError());
        return NULL;
    }

    return texture;
}
