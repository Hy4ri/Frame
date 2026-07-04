#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include "prefetch.h"
#include "loader.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

/* Maximum paths per submit batch (defensive limit) */
#define MAX_PREFETCH_PATHS 64

#define NUM_WORKERS 3

struct Prefetcher {
    ImageCache *cache;            /* borrowed, already mutex-protected */

    /* --- request queue (protected by `mutex`) --- */
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    char **queue;                 /* array of strdup'd paths */
    int    queue_len;
    int    queue_pos;             /* next index the worker will process */
    unsigned int generation;      /* bumped on every prefetch_submit */
    bool   shutdown;

    pthread_t threads[NUM_WORKERS];
};

/* Free all paths in the queue (caller must hold mutex). */
static void clear_queue_locked(Prefetcher *pf)
{
    for (int i = 0; i < pf->queue_len; i++)
        free(pf->queue[i]);
    free(pf->queue);
    pf->queue     = NULL;
    pf->queue_len = 0;
    pf->queue_pos = 0;
}

/* Worker thread entry point. */
static void *worker_func(void *arg)
{
    Prefetcher *pf = (Prefetcher *)arg;

    pthread_mutex_lock(&pf->mutex);
    for (;;) {
        /* Wait until there is work or we are asked to shut down. */
        while (!pf->shutdown && pf->queue_pos >= pf->queue_len)
            pthread_cond_wait(&pf->cond, &pf->mutex);

        if (pf->shutdown)
            break;

        /* Pop the next path and remember the current generation. */
        char *path = strdup(pf->queue[pf->queue_pos]);
        pf->queue_pos++;
        unsigned int gen = pf->generation;
        pthread_mutex_unlock(&pf->mutex);

        if (!path)
            goto relock;

        /* Skip if already cached (cheap mutex-protected lookup). */
        if (cache_get(pf->cache, path)) {
            free(path);
            goto relock;
        }

        /* Decode the image — this is the expensive part and runs without
           holding the prefetch mutex. */
        SDL_Surface *surface = loader_load_static(path);

        /* Re-acquire the mutex and check if the generation is still current.
           If the user navigated again while we were decoding, drop the result
           so we don't pollute the cache with stale entries. */
        pthread_mutex_lock(&pf->mutex);
        if (gen != pf->generation || pf->shutdown) {
            /* Stale — discard. */
            if (surface)
                SDL_DestroySurface(surface);
            free(path);
            continue;
        }

        if (surface)
            cache_put(pf->cache, path, surface); /* cache takes ownership */
        free(path);
        continue;

relock:
        pthread_mutex_lock(&pf->mutex);
    }

    pthread_mutex_unlock(&pf->mutex);
    return NULL;
}

Prefetcher *prefetch_create(ImageCache *cache)
{
    if (!cache)
        return NULL;

    Prefetcher *pf = calloc(1, sizeof(Prefetcher));
    if (!pf)
        return NULL;

    pf->cache = cache;

    if (pthread_mutex_init(&pf->mutex, NULL) != 0) {
        free(pf);
        return NULL;
    }
    if (pthread_cond_init(&pf->cond, NULL) != 0) {
        pthread_mutex_destroy(&pf->mutex);
        free(pf);
        return NULL;
    }

    for (int i = 0; i < NUM_WORKERS; i++) {
        if (pthread_create(&pf->threads[i], NULL, worker_func, pf) != 0) {
            /* On failure, signal shutdown and clean up already created threads */
            pthread_mutex_lock(&pf->mutex);
            pf->shutdown = true;
            pthread_cond_broadcast(&pf->cond);
            pthread_mutex_unlock(&pf->mutex);

            for (int j = 0; j < i; j++) {
                pthread_join(pf->threads[j], NULL);
            }
            pthread_cond_destroy(&pf->cond);
            pthread_mutex_destroy(&pf->mutex);
            free(pf);
            return NULL;
        }
    }

    return pf;
}

void prefetch_destroy(Prefetcher *pf)
{
    if (!pf)
        return;

    /* Signal shutdown and wake all workers. */
    pthread_mutex_lock(&pf->mutex);
    pf->shutdown = true;
    pthread_cond_broadcast(&pf->cond);
    pthread_mutex_unlock(&pf->mutex);

    for (int i = 0; i < NUM_WORKERS; i++) {
        pthread_join(pf->threads[i], NULL);
    }

    /* Clean up remaining queue entries. */
    clear_queue_locked(pf);  /* safe — worker has exited */

    pthread_cond_destroy(&pf->cond);
    pthread_mutex_destroy(&pf->mutex);
    free(pf);
}

void prefetch_submit(Prefetcher *pf, const char **paths, int count)
{
    if (!pf || !paths || count <= 0)
        return;

    if (count > MAX_PREFETCH_PATHS)
        count = MAX_PREFETCH_PATHS;

    /* Build the new queue outside the lock (just strdup). */
    char **new_queue = malloc((size_t)count * sizeof(char *));
    if (!new_queue)
        return;

    int copied = 0;
    for (int i = 0; i < count; i++) {
        if (!paths[i])
            continue;
        new_queue[copied] = strdup(paths[i]);
        if (!new_queue[copied])
            continue;
        copied++;
    }

    pthread_mutex_lock(&pf->mutex);

    /* Replace the old queue. */
    clear_queue_locked(pf);
    pf->queue     = new_queue;
    pf->queue_len = copied;
    pf->queue_pos = 0;
    pf->generation++;

    pthread_cond_broadcast(&pf->cond);
    pthread_mutex_unlock(&pf->mutex);
}
