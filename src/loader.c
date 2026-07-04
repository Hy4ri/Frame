#include "loader.h"
#include <SDL3_image/SDL_image.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "mmap_load: failed to open '%s'\n", path);
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        fprintf(stderr, "mmap_load: failed to stat '%s'\n", path);
        return NULL;
    }

    size_t size = (size_t)st.st_size;
    void *map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd); /* fd can be closed immediately after mmap */

    if (map == MAP_FAILED) {
        fprintf(stderr, "mmap_load: mmap failed for '%s'\n", path);
        return NULL;
    }

    SDL_IOStream *stream = SDL_IOFromConstMem(map, size);
    if (!stream) {
        munmap(map, size);
        fprintf(stderr, "mmap_load: SDL_IOFromConstMem failed for '%s'\n", path);
        return NULL;
    }

    SDL_Surface *surface = IMG_Load_IO(stream, true); /* closes stream */
    munmap(map, size);

    if (!surface) {
        fprintf(stderr, "mmap_load: IMG_Load_IO failed for '%s': %s\n", path, SDL_GetError());
        return NULL;
    }

    /* Convert all loaded surfaces to RGBA8888 to ensure GPU texture compatibility
       (e.g. for indexed colormap PNGs) and uniform format reuse. */
    if (surface->format != SDL_PIXELFORMAT_RGBA8888) {
        SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA8888);
        if (converted) {
            SDL_DestroySurface(surface);
            surface = converted;
        }
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
