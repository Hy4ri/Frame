// Package gui provides Fyne-based user interface components for Frame.
package gui

import (
	"image"
	"sync"
)

// defaultCacheCapacity is the number of decoded images to keep in memory.
// 10 entries covers ±5 images around the current index, making back-and-forth
// navigation instant without excessive memory use.
const defaultCacheCapacity = 10

// imageCache is a bounded LRU cache for decoded images, keyed by file path.
// It is safe for concurrent use from prefetch goroutines and the UI thread.
type imageCache struct {
	mu       sync.Mutex
	capacity int
	order    []string              // LRU order: most-recently-used at end
	entries  map[string]image.Image
}

// newImageCache creates a cache with the given capacity.
func newImageCache(capacity int) *imageCache {
	return &imageCache{
		capacity: capacity,
		order:    make([]string, 0, capacity),
		entries:  make(map[string]image.Image, capacity),
	}
}

// Get retrieves a cached image and marks it as recently used.
// Returns the image and true if found, nil and false otherwise.
func (c *imageCache) Get(path string) (image.Image, bool) {
	c.mu.Lock()
	defer c.mu.Unlock()

	img, ok := c.entries[path]
	if !ok {
		return nil, false
	}

	c.touchLocked(path)
	return img, true
}

// Put adds an image to the cache, evicting the least-recently-used entry
// if the cache is at capacity.
func (c *imageCache) Put(path string, img image.Image) {
	c.mu.Lock()
	defer c.mu.Unlock()

	// If already present, update and move to most-recent.
	if _, ok := c.entries[path]; ok {
		c.entries[path] = img
		c.touchLocked(path)
		return
	}

	// Evict LRU if at capacity.
	for len(c.order) >= c.capacity {
		evict := c.order[0]
		c.order = c.order[1:]
		delete(c.entries, evict)
	}

	c.entries[path] = img
	c.order = append(c.order, path)
}

// Invalidate removes a specific entry from the cache (e.g. after deletion or rename).
func (c *imageCache) Invalidate(path string) {
	c.mu.Lock()
	defer c.mu.Unlock()

	if _, ok := c.entries[path]; !ok {
		return
	}

	delete(c.entries, path)
	for i, p := range c.order {
		if p == path {
			c.order = append(c.order[:i], c.order[i+1:]...)
			break
		}
	}
}

// touchLocked moves path to the end (most-recently-used) of the order slice.
// Caller must hold c.mu.
func (c *imageCache) touchLocked(path string) {
	for i, p := range c.order {
		if p == path {
			c.order = append(c.order[:i], c.order[i+1:]...)
			break
		}
	}
	c.order = append(c.order, path)
}
