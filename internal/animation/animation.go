// Package animation provides decoding and frame compositing for animated
// image formats (GIF, APNG).
package animation

import (
	"fmt"
	"image"
	"image/draw"
	"image/gif"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/kettek/apng"
)

// minFrameDelay is the minimum delay between frames to prevent CPU spinning.
// Browsers typically clamp GIF delays below 20ms to 100ms; we use 10ms as a
// reasonable floor.
const minFrameDelay = 10 * time.Millisecond

// maxCachedFrames is the sliding-window size for composited frames.
// Only this many composited frames are held in memory at once; older ones
// are evicted and re-composited on demand. 30 frames ≈ 1 second of typical
// GIF playback, keeping memory bounded even for very long animations.
const maxCachedFrames = 30

// animatableExtensions lists extensions that may contain animation.
// Note: .png is intentionally excluded — static PNGs are loaded directly via
// image.Decode. Only files with the explicit .apng extension use the APNG path.
var animatableExtensions = map[string]bool{
	".gif":  true,
	".apng": true,
}

// IsAnimatable reports whether the file extension could contain animation.
func IsAnimatable(path string) bool {
	ext := strings.ToLower(filepath.Ext(path))
	return animatableExtensions[ext]
}

// composited holds a single composited frame and its display duration.
type composited struct {
	img   image.Image
	delay time.Duration
}

// Animation holds the raw decoded data and a sliding-window cache of
// composited frames. Frames are composited on demand instead of all up-front,
// capping memory usage to O(maxCachedFrames) instead of O(totalFrames).
type Animation struct {
	// Public read-only
	FrameCount int
	LoopCount  int

	// Source data (exactly one is non-nil)
	rawGIF  *gif.GIF
	rawAPNG *apng.APNG

	// Canvas dimensions
	width  int
	height int

	// Sliding-window frame cache
	mu         sync.Mutex
	frameCache map[int]*composited
	cacheOrder []int // LRU order: most-recently-used at end
}

// Frame returns the composited image and delay for frame at the given index.
// Frames are composited on demand and cached in a bounded sliding window.
func (a *Animation) Frame(index int) (image.Image, time.Duration) {
	if index < 0 || index >= a.FrameCount {
		return nil, 0
	}

	a.mu.Lock()
	defer a.mu.Unlock()

	// Cache hit — touch and return.
	if c, ok := a.frameCache[index]; ok {
		a.touchLocked(index)
		return c.img, c.delay
	}

	// Cache miss — composite the frame.
	img, delay := a.compositeFrame(index)
	a.frameCache[index] = &composited{img: img, delay: delay}
	a.cacheOrder = append(a.cacheOrder, index)
	a.evictLocked()

	return img, delay
}

// Decode attempts to decode an animated image from the given path.
// It returns nil (without error) if the image has only a single frame or
// the format is not animatable. Static images should be loaded via the
// normal image.Decode path.
func Decode(path string) (*Animation, error) {
	ext := strings.ToLower(filepath.Ext(path))
	switch ext {
	case ".gif":
		return decodeGIF(path)
	case ".apng":
		return decodeAPNG(path)
	default:
		return nil, nil
	}
}

// decodeGIF decodes an animated GIF file, storing the raw data for
// on-demand frame compositing.
func decodeGIF(path string) (*Animation, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open gif: %w", err)
	}
	defer f.Close()

	g, err := gif.DecodeAll(f)
	if err != nil {
		return nil, fmt.Errorf("decode gif: %w", err)
	}

	// Single-frame GIF: not animated.
	if len(g.Image) <= 1 {
		return nil, nil
	}

	width := g.Config.Width
	height := g.Config.Height

	// If config dimensions are zero (rare), infer from first frame.
	if width == 0 || height == 0 {
		bounds := g.Image[0].Bounds()
		width = bounds.Max.X
		height = bounds.Max.Y
	}

	return &Animation{
		FrameCount: len(g.Image),
		LoopCount:  g.LoopCount,
		rawGIF:     g,
		width:      width,
		height:     height,
		frameCache: make(map[int]*composited, maxCachedFrames),
		cacheOrder: make([]int, 0, maxCachedFrames),
	}, nil
}

// decodeAPNG decodes an animated PNG file, storing the raw data for
// on-demand frame compositing.
func decodeAPNG(path string) (*Animation, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open apng: %w", err)
	}
	defer f.Close()

	a, err := apng.DecodeAll(f)
	if err != nil {
		return nil, fmt.Errorf("decode apng: %w", err)
	}

	// Single-frame PNG: not animated.
	if len(a.Frames) <= 1 {
		return nil, nil
	}

	firstBounds := a.Frames[0].Image.Bounds()

	return &Animation{
		FrameCount: len(a.Frames),
		LoopCount:  int(a.LoopCount),
		rawAPNG:    &a,
		width:      firstBounds.Dx(),
		height:     firstBounds.Dy(),
		frameCache: make(map[int]*composited, maxCachedFrames),
		cacheOrder: make([]int, 0, maxCachedFrames),
	}, nil
}

// compositeFrame builds the composited image for a given frame index by
// replaying all frames from 0..index. This is the expected cost of a cache
// miss in the sliding window.
func (a *Animation) compositeFrame(index int) (image.Image, time.Duration) {
	if a.rawGIF != nil {
		return a.compositeGIFFrame(index)
	}
	return a.compositeAPNGFrame(index)
}

// compositeGIFFrame replays GIF frames 0..index onto a canvas.
func (a *Animation) compositeGIFFrame(index int) (image.Image, time.Duration) {
	g := a.rawGIF
	canvasRect := image.Rect(0, 0, a.width, a.height)
	canvas := image.NewRGBA(canvasRect)
	var prevCanvas *image.RGBA

	for i := 0; i <= index; i++ {
		frame := g.Image[i]

		// Handle disposal from the PREVIOUS frame before drawing the current one.
		if i > 0 {
			switch g.Disposal[i-1] {
			case gif.DisposalBackground:
				prevBounds := g.Image[i-1].Bounds()
				clearRect(canvas, prevBounds)
			case gif.DisposalPrevious:
				if prevCanvas != nil {
					copy(canvas.Pix, prevCanvas.Pix)
				}
			}
		}

		// Save canvas state if this frame's disposal is DisposalPrevious.
		if i < len(g.Disposal) && g.Disposal[i] == gif.DisposalPrevious {
			prevCanvas = cloneRGBA(canvas)
		}

		draw.Draw(canvas, frame.Bounds(), frame, frame.Bounds().Min, draw.Over)
	}

	delay := time.Duration(g.Delay[index]) * 10 * time.Millisecond
	if delay < minFrameDelay {
		delay = minFrameDelay
	}

	return cloneRGBA(canvas), delay
}

// compositeAPNGFrame replays APNG frames 0..index onto a canvas.
func (a *Animation) compositeAPNGFrame(index int) (image.Image, time.Duration) {
	ap := a.rawAPNG
	canvasRect := image.Rect(0, 0, a.width, a.height)
	canvas := image.NewRGBA(canvasRect)
	var prevCanvas *image.RGBA

	for i := 0; i <= index; i++ {
		frame := ap.Frames[i]

		// Save canvas state before drawing if dispose op is "previous".
		if frame.DisposeOp == apng.DISPOSE_OP_PREVIOUS {
			prevCanvas = cloneRGBA(canvas)
		}

		frameRect := image.Rect(
			frame.XOffset, frame.YOffset,
			frame.XOffset+frame.Image.Bounds().Dx(),
			frame.YOffset+frame.Image.Bounds().Dy(),
		)

		switch frame.BlendOp {
		case apng.BLEND_OP_SOURCE:
			draw.Draw(canvas, frameRect, frame.Image, frame.Image.Bounds().Min, draw.Src)
		default:
			draw.Draw(canvas, frameRect, frame.Image, frame.Image.Bounds().Min, draw.Over)
		}

		// Handle disposal for the current frame.
		switch frame.DisposeOp {
		case apng.DISPOSE_OP_BACKGROUND:
			clearRect(canvas, frameRect)
		case apng.DISPOSE_OP_PREVIOUS:
			if prevCanvas != nil {
				copy(canvas.Pix, prevCanvas.Pix)
			}
		}
	}

	delay := time.Duration(float64(time.Second) * ap.Frames[index].GetDelay())
	if delay < minFrameDelay {
		delay = minFrameDelay
	}

	return cloneRGBA(canvas), delay
}

// touchLocked moves index to the end (most-recently-used) of the order slice.
// Caller must hold a.mu.
func (a *Animation) touchLocked(index int) {
	for i, idx := range a.cacheOrder {
		if idx == index {
			a.cacheOrder = append(a.cacheOrder[:i], a.cacheOrder[i+1:]...)
			break
		}
	}
	a.cacheOrder = append(a.cacheOrder, index)
}

// evictLocked removes the least-recently-used entries if the cache exceeds
// maxCachedFrames. Caller must hold a.mu.
func (a *Animation) evictLocked() {
	for len(a.cacheOrder) > maxCachedFrames {
		evict := a.cacheOrder[0]
		a.cacheOrder = a.cacheOrder[1:]
		delete(a.frameCache, evict)
	}
}

// cloneRGBA creates a deep copy of an RGBA image.
func cloneRGBA(src *image.RGBA) *image.RGBA {
	dst := image.NewRGBA(src.Bounds())
	copy(dst.Pix, src.Pix)
	return dst
}

// clearRect fills a rectangle in the canvas with transparent pixels.
func clearRect(canvas *image.RGBA, rect image.Rectangle) {
	draw.Draw(canvas, rect, image.Transparent, image.Point{}, draw.Src)
}
