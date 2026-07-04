#ifndef FRAME_PREFETCH_H
#define FRAME_PREFETCH_H

#include "cache.h"

/*
 * Background prefetch worker thread.
 *
 * Decodes images ahead of the user's navigation direction and inserts them
 * into the shared ImageCache (which is already mutex-protected).  The
 * prefetcher owns its own mutex for the request queue; it never holds both
 * locks at the same time.
 */
typedef struct Prefetcher Prefetcher;

/* Create a prefetcher and spawn its worker thread.
   `cache` is borrowed — must outlive the prefetcher. */
Prefetcher *prefetch_create(ImageCache *cache);

/* Signal the worker thread to stop and join it, then free resources. */
void prefetch_destroy(Prefetcher *pf);

/* Submit a new batch of paths to prefetch.  Clears any pending requests so
   rapid navigation always prioritises the latest neighbourhood.
   `paths` is an array of `count` path strings — they are copied internally. */
void prefetch_submit(Prefetcher *pf, const char **paths, int count);

#endif /* FRAME_PREFETCH_H */
