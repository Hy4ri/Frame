#ifndef FRAME_CACHE_H
#define FRAME_CACHE_H

#include <SDL3/SDL.h>

/* Opaque cache handle */
typedef struct ImageCache ImageCache;

/* Create a cache with the given capacity (max entries).
   Returns NULL on allocation failure. */
ImageCache *cache_create(int capacity);

/* Destroy the cache, freeing all stored surfaces. */
void cache_destroy(ImageCache *cache);

/* Get a cached surface by path. Returns NULL if not found.
   On a hit, the entry is touched (moved to most-recently-used).
   The returned surface is still owned by the cache — do NOT free it. */
SDL_Surface *cache_get(ImageCache *cache, const char *path);

/* Put a surface into the cache. The cache takes ownership of the surface
   and will free it on eviction or destruction.
   If the path already exists, the old surface is freed and replaced.
   If the cache is at capacity, the least-recently-used entry is evicted. */
void cache_put(ImageCache *cache, const char *path, SDL_Surface *surface);

/* Remove a specific path from the cache, freeing its surface.
   Safe to call even if the path is not cached. */
void cache_invalidate(ImageCache *cache, const char *path);

#endif /* FRAME_CACHE_H */
