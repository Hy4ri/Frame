#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef __linux__
#include <malloc.h>
#endif

#define MAX_CACHE_CAPACITY 200  // sanity limit

typedef struct {
    char *path;
    SDL_Surface *surface;
    size_t surface_bytes;  // w * h * bytes_per_pixel
} CacheEntry;

struct ImageCache {
    CacheEntry *entries;  // dynamic array of capacity entries
    int *order;           // array of indices, order[0] = LRU, order[len-1] = MRU
    int capacity;
    int len;              // current number of entries (0..capacity)
    pthread_mutex_t mutex;

    int *hash_table;      // maps path to entry index
    int hash_capacity;
    char *pinned_path;    // path of currently displayed image, must not be evicted

    size_t total_bytes;   // sum of all cached surface bytes
    size_t max_bytes;     // memory budget (0 = unlimited)
};

static size_t surface_byte_size(SDL_Surface *s)
{
    if (!s) return 0;
    return (size_t)s->h * (size_t)s->pitch;
}

/* --- Internal helpers (caller must hold mutex) --- */

/* Find the entry index for a given path, or -1 if not found. */
static uint32_t fnv1a_hash(const char *str)
{
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (unsigned char)*str++;
        hash *= 16777619u;
    }
    return hash;
}

static int get_hash_capacity(int capacity)
{
    int size = 16;
    while (size < capacity * 2) {
        size *= 2;
    }
    return size;
}

static void hash_table_insert(ImageCache *cache, const char *path, int entry_idx)
{
    if (cache->hash_capacity <= 0 || !cache->hash_table) return;
    uint32_t h = fnv1a_hash(path);
    int mask = cache->hash_capacity - 1;
    int idx = (int)(h & mask);
    while (cache->hash_table[idx] != -1) {
        idx = (idx + 1) & mask;
    }
    cache->hash_table[idx] = entry_idx;
}

static void rebuild_hash_table(ImageCache *cache)
{
    if (!cache->hash_table) return;
    memset(cache->hash_table, -1, (size_t)cache->hash_capacity * sizeof(int));
    for (int i = 0; i < cache->len; i++) {
        if (cache->entries[i].path) {
            hash_table_insert(cache, cache->entries[i].path, i);
        }
    }
}

static int find_index(ImageCache *cache, const char *path)
{
    if (!cache || !path || cache->hash_capacity <= 0 || !cache->hash_table)
        return -1;

    uint32_t h = fnv1a_hash(path);
    int mask = cache->hash_capacity - 1;
    int idx = (int)(h & mask);

    while (cache->hash_table[idx] != -1) {
        int entry_idx = cache->hash_table[idx];
        if (cache->entries[entry_idx].path && strcmp(cache->entries[entry_idx].path, path) == 0) {
            return entry_idx;
        }
        idx = (idx + 1) & mask;
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

ImageCache *cache_create(int capacity, size_t max_bytes)
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
    cache->max_bytes = max_bytes;
    cache->total_bytes = 0;

    cache->hash_capacity = get_hash_capacity(capacity);
    cache->hash_table = malloc((size_t)cache->hash_capacity * sizeof(int));
    if (!cache->hash_table) {
        free(cache->entries);
        free(cache->order);
        free(cache);
        return NULL;
    }
    memset(cache->hash_table, -1, (size_t)cache->hash_capacity * sizeof(int));

    if (pthread_mutex_init(&cache->mutex, NULL) != 0) {
        free(cache->hash_table);
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

    free(cache->pinned_path);
    free(cache->hash_table);
    free(cache->entries);
    free(cache->order);
    pthread_mutex_destroy(&cache->mutex);
    free(cache);

#ifdef __linux__
    malloc_trim(0);
#endif
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

/* Evict a single LRU entry (caller must hold mutex).  Returns false if
   nothing could be evicted (e.g. everything is pinned). */
static bool evict_one_locked(ImageCache *cache)
{
    if (cache->len <= 0) return false;

    /* Find first unpinned entry in LRU order */
    int evict_order_pos = 0;
    while (evict_order_pos < cache->len) {
        int o_idx = cache->order[evict_order_pos];
        if (cache->pinned_path && cache->entries[o_idx].path &&
            strcmp(cache->entries[o_idx].path, cache->pinned_path) == 0) {
            evict_order_pos++;
            continue;
        }
        break;
    }
    if (evict_order_pos >= cache->len)
        return false;  /* everything is pinned */

    int evict_idx = cache->order[evict_order_pos];
    cache->total_bytes -= cache->entries[evict_idx].surface_bytes;
    free(cache->entries[evict_idx].path);
    if (cache->entries[evict_idx].surface)
        SDL_DestroySurface(cache->entries[evict_idx].surface);
    cache->entries[evict_idx].path = NULL;
    cache->entries[evict_idx].surface = NULL;
    cache->entries[evict_idx].surface_bytes = 0;

    /* Swap with last element if needed to prevent gaps */
    if (evict_idx != cache->len - 1) {
        int last_idx = cache->len - 1;
        cache->entries[evict_idx] = cache->entries[last_idx];
        cache->entries[last_idx].path = NULL;
        cache->entries[last_idx].surface = NULL;
        cache->entries[last_idx].surface_bytes = 0;

        /* Update references in order array */
        for (int i = 0; i < cache->len; i++) {
            if (cache->order[i] == last_idx) {
                cache->order[i] = evict_idx;
                break;
            }
        }
    }

    /* Shift elements in order array left to remove the evicted entry */
    for (int i = evict_order_pos; i < cache->len - 1; i++)
        cache->order[i] = cache->order[i + 1];

    cache->len--;
    rebuild_hash_table(cache);
    return true;
}

void cache_put(ImageCache *cache, const char *path, SDL_Surface *surface)
{
    if (!cache || !path || !surface)
        return;

    size_t new_bytes = surface_byte_size(surface);

    pthread_mutex_lock(&cache->mutex);

    int idx = find_index(cache, path);
    if (idx >= 0) {
        /* Entry exists — do not replace to prevent overwriting active surfaces
           that are currently being used by the main thread. Just discard. */
        SDL_DestroySurface(surface);
    } else {
        /* Evict until we are under both capacity and memory budget */
        while (cache->len >= cache->capacity ||
               (cache->max_bytes > 0 && cache->len > 0 &&
                cache->total_bytes + new_bytes > cache->max_bytes)) {
            if (!evict_one_locked(cache)) {
                /* Cannot evict anything (all pinned) — accept the entry anyway */
                break;
            }
        }

        if (cache->len >= cache->capacity) {
            /* Still at capacity after trying to evict (all pinned) — force evict LRU */
            int evict_idx = cache->order[0];
            cache->total_bytes -= cache->entries[evict_idx].surface_bytes;
            free(cache->entries[evict_idx].path);
            if (cache->entries[evict_idx].surface)
                SDL_DestroySurface(cache->entries[evict_idx].surface);
            cache->entries[evict_idx].path = NULL;
            cache->entries[evict_idx].surface = NULL;
            cache->entries[evict_idx].surface_bytes = 0;

            if (evict_idx != cache->len - 1) {
                int last_idx = cache->len - 1;
                cache->entries[evict_idx] = cache->entries[last_idx];
                cache->entries[last_idx].path = NULL;
                cache->entries[last_idx].surface = NULL;
                cache->entries[last_idx].surface_bytes = 0;
                for (int i = 0; i < cache->len; i++) {
                    if (cache->order[i] == last_idx) {
                        cache->order[i] = evict_idx;
                        break;
                    }
                }
            }

            for (int i = 0; i < cache->len - 1; i++)
                cache->order[i] = cache->order[i + 1];

            cache->len--;
            rebuild_hash_table(cache);
        }

        idx = cache->len;
        cache->len++;

        char *path_copy = strdup(path);
        if (!path_copy) {
            cache->len--;
            rebuild_hash_table(cache);
            pthread_mutex_unlock(&cache->mutex);
            return;
        }

        cache->entries[idx].path = path_copy;
        cache->entries[idx].surface = surface;
        cache->entries[idx].surface_bytes = new_bytes;
        cache->total_bytes += new_bytes;
        cache->order[cache->len - 1] = idx;
        rebuild_hash_table(cache);
    }

    pthread_mutex_unlock(&cache->mutex);

#ifdef __linux__
    malloc_trim(0);
#endif
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
    cache->total_bytes -= cache->entries[idx].surface_bytes;
    free(cache->entries[idx].path);
    if (cache->entries[idx].surface)
        SDL_DestroySurface(cache->entries[idx].surface);
    cache->entries[idx].path = NULL;
    cache->entries[idx].surface = NULL;
    cache->entries[idx].surface_bytes = 0;

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

    rebuild_hash_table(cache);

    pthread_mutex_unlock(&cache->mutex);

#ifdef __linux__
    malloc_trim(0);
#endif
}

void cache_pin(ImageCache *cache, const char *path)
{
    if (!cache) return;
    pthread_mutex_lock(&cache->mutex);
    free(cache->pinned_path);
    if (path) {
        cache->pinned_path = strdup(path);
    } else {
        cache->pinned_path = NULL;
    }
    pthread_mutex_unlock(&cache->mutex);
}
