#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MAX_CACHE_CAPACITY 100  // sanity limit

typedef struct {
    char *path;
    SDL_Surface *surface;
} CacheEntry;

struct ImageCache {
    CacheEntry *entries;  // dynamic array of capacity entries
    int *order;           // array of indices, order[0] = LRU, order[len-1] = MRU
    int capacity;
    int len;              // current number of entries (0..capacity)
    pthread_mutex_t mutex;
};

/* --- Internal helpers (caller must hold mutex) --- */

/* Find the entry index for a given path, or -1 if not found. */
static int find_index(ImageCache *cache, const char *path)
{
    for (int i = 0; i < cache->len; i++) {
        if (strcmp(cache->entries[i].path, path) == 0)
            return i;
    }
    return -1;
}

/* Find the position in order[] for a given entry index, or -1 if not found. */
static int find_order_position(ImageCache *cache, int entry_index)
{
    for (int i = 0; i < cache->len; i++) {
        if (cache->order[i] == entry_index)
            return i;
    }
    return -1;
}

/* Move the entry with the given index to the MRU end of the order array.
   Caller must hold the mutex. */
static void touch_locked(ImageCache *cache, int entry_index)
{
    int pos = find_order_position(cache, entry_index);
    if (pos < 0)
        return; // should not happen if caller used a valid index

    if (pos == cache->len - 1)
        return; // already MRU, nothing to do

    /* Shift everything after pos left by one, then place at end */
    int idx = cache->order[pos];
    for (int i = pos; i < cache->len - 1; i++)
        cache->order[i] = cache->order[i + 1];
    cache->order[cache->len - 1] = idx;
}

/* --- Public API --- */

ImageCache *cache_create(int capacity)
{
    if (capacity < 1)
        capacity = 1;
    if (capacity > MAX_CACHE_CAPACITY)
        capacity = MAX_CACHE_CAPACITY;

    ImageCache *cache = calloc(1, sizeof(ImageCache));
    if (!cache)
        return NULL;

    cache->entries = calloc((size_t)capacity, sizeof(CacheEntry));
    cache->order = calloc((size_t)capacity, sizeof(int));
    if (!cache->entries || !cache->order) {
        free(cache->entries);
        free(cache->order);
        free(cache);
        return NULL;
    }

    cache->capacity = capacity;
    cache->len = 0;

    if (pthread_mutex_init(&cache->mutex, NULL) != 0) {
        free(cache->entries);
        free(cache->order);
        free(cache);
        return NULL;
    }

    return cache;
}

void cache_destroy(ImageCache *cache)
{
    if (!cache)
        return;

    pthread_mutex_lock(&cache->mutex);

    for (int i = 0; i < cache->len; i++) {
        free(cache->entries[i].path);
        if (cache->entries[i].surface)
            SDL_DestroySurface(cache->entries[i].surface);
    }

    pthread_mutex_unlock(&cache->mutex);

    free(cache->entries);
    free(cache->order);
    pthread_mutex_destroy(&cache->mutex);
    free(cache);
}

SDL_Surface *cache_get(ImageCache *cache, const char *path)
{
    if (!cache || !path)
        return NULL;

    pthread_mutex_lock(&cache->mutex);

    int idx = find_index(cache, path);
    if (idx < 0) {
        pthread_mutex_unlock(&cache->mutex);
        return NULL;
    }

    touch_locked(cache, idx);

    SDL_Surface *surface = cache->entries[idx].surface;

    pthread_mutex_unlock(&cache->mutex);
    return surface;
}

void cache_put(ImageCache *cache, const char *path, SDL_Surface *surface)
{
    if (!cache || !path || !surface)
        return;

    pthread_mutex_lock(&cache->mutex);

    int idx = find_index(cache, path);
    if (idx >= 0) {
        /* Entry exists — replace */
        if (cache->entries[idx].surface)
            SDL_DestroySurface(cache->entries[idx].surface);
        cache->entries[idx].surface = surface;
        touch_locked(cache, idx);
    } else {
        /* New entry */
        if (cache->len >= cache->capacity) {
            /* Evict LRU entry (order[0]) */
            int evict_idx = cache->order[0];
            free(cache->entries[evict_idx].path);
            if (cache->entries[evict_idx].surface)
                SDL_DestroySurface(cache->entries[evict_idx].surface);
            cache->entries[evict_idx].path = NULL;
            cache->entries[evict_idx].surface = NULL;

            /* Remove from order by shifting left */
            for (int i = 0; i < cache->len - 1; i++)
                cache->order[i] = cache->order[i + 1];

            cache->len--;

            /* Reuse the freed slot */
            idx = evict_idx;
        } else {
            idx = cache->len;
            cache->len++;
        }

        char *path_copy = strdup(path);
        if (!path_copy) {
            /* Allocation failure — put fails silently */
            pthread_mutex_unlock(&cache->mutex);
            return;
        }

        cache->entries[idx].path = path_copy;
        cache->entries[idx].surface = surface;
        cache->order[cache->len - 1] = idx;
    }

    pthread_mutex_unlock(&cache->mutex);
}

void cache_invalidate(ImageCache *cache, const char *path)
{
    if (!cache || !path)
        return;

    pthread_mutex_lock(&cache->mutex);

    int idx = find_index(cache, path);
    if (idx < 0) {
        pthread_mutex_unlock(&cache->mutex);
        return;
    }

    /* Free the entry's resources */
    free(cache->entries[idx].path);
    if (cache->entries[idx].surface)
        SDL_DestroySurface(cache->entries[idx].surface);
    cache->entries[idx].path = NULL;
    cache->entries[idx].surface = NULL;

    /* Remove from order array */
    int pos = find_order_position(cache, idx);
    if (pos >= 0) {
        for (int i = pos; i < cache->len - 1; i++)
            cache->order[i] = cache->order[i + 1];
    }

    cache->len--;

    /* If we removed an entry that isn't past the new length, we need to
       swap it with whatever is now at cache->order[len] to avoid gaps.
       The entry at position idx is now a hole. Copy the last entry over it. */
    if (idx < cache->len) {
        /* The last used entry in the entries array */
        int last_idx = -1;
        for (int i = 0; i < cache->len; i++) {
            if (cache->order[i] == cache->len) {  /* len is now the old last index */
                last_idx = i;
                break;
            }
        }
        if (last_idx >= 0) {
            /* Move the last actual entry into the hole at idx */
            cache->entries[idx] = cache->entries[cache->len];
            cache->entries[cache->len].path = NULL;
            cache->entries[cache->len].surface = NULL;

            /* Update order array: reference idx instead of the old index */
            for (int i = 0; i < cache->len; i++) {
                if (cache->order[i] == cache->len) {
                    cache->order[i] = idx;
                    break;
                }
            }
        }
    }

    pthread_mutex_unlock(&cache->mutex);
}
