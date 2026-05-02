package gui

import (
	goimage "image"
	godraw "image/draw"
	"sync"
	"sync/atomic"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/widget"

	"github.com/Hy4ri/frame/internal/animation"
)

// zoomScrollContainer wraps a scroll container and intercepts mouse wheel
// events to perform zoom instead of scrolling. This lets the user zoom with
// the scroll wheel while still supporting drag-to-pan via the inner Scroll.
type zoomScrollContainer struct {
	widget.BaseWidget
	scroll *container.Scroll
	viewer *Viewer
}

func newZoomScrollContainer(scroll *container.Scroll, viewer *Viewer) *zoomScrollContainer {
	z := &zoomScrollContainer{scroll: scroll, viewer: viewer}
	z.ExtendBaseWidget(z)
	return z
}

// Scrolled intercepts mouse wheel events and delegates to zoom.
func (z *zoomScrollContainer) Scrolled(ev *fyne.ScrollEvent) {
	if !z.viewer.ctrlHeld.Load() {
		z.scroll.Scrolled(ev)
		return
	}

	if ev.Scrolled.DY > 0 {
		z.viewer.ZoomOut()
	} else if ev.Scrolled.DY < 0 {
		z.viewer.ZoomIn()
	}
}

// CreateRenderer returns a renderer that simply wraps the inner scroll.
func (z *zoomScrollContainer) CreateRenderer() fyne.WidgetRenderer {
	return widget.NewSimpleRenderer(z.scroll)
}

// Viewer handles image display with zoom, rotation, and animation support.
type Viewer struct {
	outerWidget *zoomScrollContainer
	scroll      *container.Scroll
	imageCanvas *canvas.Image
	cache       *imageCache

	// loadGen is atomically incremented on each LoadImage call.
	// Background decode goroutines check this before committing their
	// result, so stale loads from rapid navigation are silently discarded.
	loadGen uint64

	// mu protects all fields below it, as they are read by the animation
	// goroutine and written by UI-thread methods.
	mu          sync.Mutex
	originalImg goimage.Image
	zoomLevel   float64
	rotation    int // 0, 90, 180, 270 degrees
	fitMode     bool

	// Cached rotated image to avoid re-rotating on every zoom step.
	cachedRotatedImg goimage.Image
	cachedRotation   int
	cachedOrigPtr    goimage.Image // identity check for cache invalidation

	// zoomTimer coalesces rapid zoom events (e.g. scroll wheel) into a
	// single applyTransforms call, avoiding redundant work.
	zoomTimer *time.Timer

	// ctrlHeld is set by the window key handlers so scroll only zooms while
	// the user is holding Ctrl.
	ctrlHeld atomic.Bool

	// Animation state (also protected by mu)
	anim       *animation.Animation
	animStop   chan struct{} // closed to signal the playback goroutine to exit
	animDone   chan struct{} // closed by the goroutine when it exits
	isAnimated bool
}

// NewViewer creates a new image viewer widget.
func NewViewer() *Viewer {
	v := &Viewer{
		zoomLevel:      1.0,
		rotation:       0,
		fitMode:        true,
		cache:          newImageCache(defaultCacheCapacity),
		cachedRotation: -1, // force first computation
	}

	v.imageCanvas = canvas.NewImageFromImage(nil)
	v.imageCanvas.FillMode = canvas.ImageFillContain
	v.imageCanvas.ScaleMode = canvas.ImageScaleSmooth

	v.scroll = container.NewScroll(v.imageCanvas)
	v.outerWidget = newZoomScrollContainer(v.scroll, v)

	return v
}

// SetCtrlHeld updates whether Ctrl is currently pressed.
func (v *Viewer) SetCtrlHeld(held bool) {
	v.ctrlHeld.Store(held)
}

// Widget returns the zoomable container that wraps the scroll view.
func (v *Viewer) Widget() fyne.CanvasObject {
	return v.outerWidget
}

// LoadImage loads and displays an image from the given path.
// If the image is animated (multi-frame GIF or APNG), it starts playback.
// For static images, a cache hit displays instantly; a cache miss triggers
// an async decode guarded by a generation counter.
func (v *Viewer) LoadImage(path string) {
	gen := atomic.AddUint64(&v.loadGen, 1)

	v.stopAnimation()

	v.mu.Lock()
	v.rotation = 0
	v.zoomLevel = 1.0
	v.fitMode = true
	v.invalidateRotationCacheLocked()
	v.mu.Unlock()

	// Animated images are never cached (they have their own frame buffer).
	if animation.IsAnimatable(path) {
		// Animated decode can be expensive — run in background.
		go func() {
			anim, err := animation.Decode(path)
			if err != nil || anim == nil {
				// Not animated or decode error — fall through to static.
				// Inline the decode here instead of calling loadStaticAsync
				// to avoid spawning a redundant nested goroutine.
				v.decodeAndCommitStatic(path, gen)
				return
			}
			if atomic.LoadUint64(&v.loadGen) != gen {
				return // user navigated away
			}
			v.mu.Lock()
			v.anim = anim
			v.isAnimated = true
			firstFrame, _ := anim.Frame(0)
			v.originalImg = firstFrame
			v.mu.Unlock()
			v.startAnimation()
		}()
		return
	}

	// Static image: check cache first for instant display.
	if img, ok := v.cache.Get(path); ok {
		v.mu.Lock()
		v.originalImg = img
		v.invalidateRotationCacheLocked()
		v.mu.Unlock()
		v.applyTransforms()
		return
	}

	// Cache miss — decode in the background.
	go v.decodeAndCommitStatic(path, gen)
}

// decodeAndCommitStatic decodes a static image and commits it only if the
// generation counter still matches. Called directly (not via goroutine)
// when already running in a background goroutine.
func (v *Viewer) decodeAndCommitStatic(path string, gen uint64) {
	img, err := LoadImageFromFile(path)
	if err != nil {
		if atomic.LoadUint64(&v.loadGen) == gen {
			v.imageCanvas.Image = nil
			v.imageCanvas.Refresh()
		}
		return
	}

	v.cache.Put(path, img)

	if atomic.LoadUint64(&v.loadGen) != gen {
		return // stale — user already moved on
	}

	v.mu.Lock()
	v.originalImg = img
	v.invalidateRotationCacheLocked()
	v.mu.Unlock()
	v.applyTransforms()
}

// PrefetchImage decodes an image into the cache without displaying it.
// Called by the app layer to warm the cache for adjacent images.
func (v *Viewer) PrefetchImage(path string) {
	if _, ok := v.cache.Get(path); ok {
		return // already cached
	}
	if animation.IsAnimatable(path) {
		return // don't prefetch animations
	}
	img, err := LoadImageFromFile(path)
	if err == nil {
		v.cache.Put(path, img)
	}
}

// startAnimation launches a goroutine that cycles through animation frames
// using the streaming Animation.Frame() API.
func (v *Viewer) startAnimation() {
	v.mu.Lock()
	anim := v.anim
	v.mu.Unlock()

	if anim == nil || anim.FrameCount <= 1 {
		return
	}

	stop := make(chan struct{})
	done := make(chan struct{})

	v.mu.Lock()
	v.animStop = stop
	v.animDone = done
	v.mu.Unlock()

	go func() {
		defer close(done)

		loops := 0
		for {
			for i := 0; i < anim.FrameCount; i++ {
				// Check for stop signal before processing the frame.
				select {
				case <-stop:
					return
				default:
				}

				frame, delay := anim.Frame(i)
				if frame == nil {
					continue
				}

				v.mu.Lock()
				rotation := v.rotation
				fitMode := v.fitMode
				zoomLevel := v.zoomLevel
				v.mu.Unlock()

				displayImg := goimage.Image(frame)
				if rotation != 0 {
					displayImg = rotateImage(displayImg, rotation)
				}

				v.imageCanvas.Image = displayImg

				if fitMode {
					v.imageCanvas.FillMode = canvas.ImageFillContain
					v.imageCanvas.SetMinSize(fyne.NewSize(0, 0))
				} else {
					bounds := displayImg.Bounds()
					w := float32(float64(bounds.Dx()) * zoomLevel)
					h := float32(float64(bounds.Dy()) * zoomLevel)
					v.imageCanvas.FillMode = canvas.ImageFillOriginal
					v.imageCanvas.SetMinSize(fyne.NewSize(w, h))
				}

				v.imageCanvas.Refresh()

				// Wait for the frame delay, but remain responsive to stop.
				select {
				case <-stop:
					return
				case <-time.After(delay):
				}
			}

			loops++
			if anim.LoopCount > 0 && loops >= anim.LoopCount {
				return
			}
		}
	}()
}

// stopAnimation signals the playback goroutine to exit and waits for it to finish.
func (v *Viewer) stopAnimation() {
	v.mu.Lock()
	stop := v.animStop
	done := v.animDone
	v.animStop = nil
	v.animDone = nil
	v.anim = nil
	v.isAnimated = false
	v.mu.Unlock()

	if stop != nil {
		close(stop)
	}
	if done != nil {
		<-done // wait for goroutine to exit before proceeding
	}
}

// invalidateRotationCacheLocked clears the cached rotated image.
// Caller must hold v.mu.
func (v *Viewer) invalidateRotationCacheLocked() {
	v.cachedRotatedImg = nil
	v.cachedOrigPtr = nil
	v.cachedRotation = -1
}

// getRotatedImage returns the rotated version of the original image, using
// a cache to avoid expensive re-rotation when only zoom changes.
// Caller must hold v.mu when reading origImg/rotation, but this method
// does NOT hold the lock during the (potentially slow) rotation itself.
func (v *Viewer) getRotatedImage(origImg goimage.Image, rotation int) goimage.Image {
	// Fast path: no rotation needed.
	if rotation == 0 {
		return origImg
	}

	// Check if we already have the rotated version cached.
	v.mu.Lock()
	if v.cachedOrigPtr == origImg && v.cachedRotation == rotation && v.cachedRotatedImg != nil {
		cached := v.cachedRotatedImg
		v.mu.Unlock()
		return cached
	}
	v.mu.Unlock()

	// Rotate (expensive) without holding the lock.
	rotated := rotateImage(origImg, rotation)

	// Store in cache.
	v.mu.Lock()
	v.cachedOrigPtr = origImg
	v.cachedRotation = rotation
	v.cachedRotatedImg = rotated
	v.mu.Unlock()

	return rotated
}

// applyTransforms applies rotation and zoom to the current static image.
// Rotation results are cached so that rapid zoom changes (e.g. scroll wheel)
// skip the expensive rotation step.
func (v *Viewer) applyTransforms() {
	v.mu.Lock()
	origImg := v.originalImg
	rotation := v.rotation
	fitMode := v.fitMode
	zoomLevel := v.zoomLevel
	v.mu.Unlock()

	if origImg == nil {
		return
	}

	displayImg := v.getRotatedImage(origImg, rotation)

	v.imageCanvas.Image = displayImg

	if fitMode {
		v.imageCanvas.FillMode = canvas.ImageFillContain
		v.imageCanvas.SetMinSize(fyne.NewSize(0, 0))
	} else {
		bounds := displayImg.Bounds()
		w := float32(float64(bounds.Dx()) * zoomLevel)
		h := float32(float64(bounds.Dy()) * zoomLevel)
		v.imageCanvas.FillMode = canvas.ImageFillOriginal
		v.imageCanvas.SetMinSize(fyne.NewSize(w, h))
	}

	v.imageCanvas.Refresh()
}

// scheduleApplyTransforms debounces rapid zoom changes (e.g. from scroll
// wheel) so that applyTransforms runs at most once per 16ms (~60fps).
func (v *Viewer) scheduleApplyTransforms() {
	v.mu.Lock()
	isAnimated := v.isAnimated
	if v.zoomTimer != nil {
		v.zoomTimer.Stop()
	}
	v.zoomTimer = time.AfterFunc(16*time.Millisecond, func() {
		v.applyTransforms()
	})
	v.mu.Unlock()

	// For animated images, the playback goroutine picks up new values
	// on the next frame, so we don't need to do anything extra.
	_ = isAnimated
}

// Rotate rotates the image by 90 degrees.
func (v *Viewer) Rotate(clockwise bool) {
	v.mu.Lock()
	if clockwise {
		v.rotation = (v.rotation + 90) % 360
	} else {
		v.rotation = (v.rotation + 270) % 360
	}
	isAnimated := v.isAnimated
	v.mu.Unlock()

	// For animated images the playback goroutine picks up the new rotation on
	// the next frame. For static images we apply immediately.
	if !isAnimated {
		v.applyTransforms()
	}
}

// ZoomIn increases zoom by 10%.
func (v *Viewer) ZoomIn() {
	v.mu.Lock()
	if v.fitMode {
		v.zoomLevel = v.computeFitZoomLocked()
		v.fitMode = false
	}
	v.zoomLevel *= 1.1
	if v.zoomLevel > 10.0 {
		v.zoomLevel = 10.0
	}
	v.mu.Unlock()

	v.scheduleApplyTransforms()
}

// ZoomOut decreases zoom by 10%.
func (v *Viewer) ZoomOut() {
	v.mu.Lock()
	if v.fitMode {
		v.zoomLevel = v.computeFitZoomLocked()
		v.fitMode = false
	}
	v.zoomLevel /= 1.1
	if v.zoomLevel < 0.1 {
		v.zoomLevel = 0.1
	}
	v.mu.Unlock()

	v.scheduleApplyTransforms()
}

// computeFitZoomLocked estimates the zoom level that corresponds to the
// current fit-to-window view, so that switching from fit mode to manual
// zoom starts at the perceived scale. Caller must hold v.mu.
func (v *Viewer) computeFitZoomLocked() float64 {
	if v.originalImg == nil {
		return 1.0
	}
	img := v.originalImg
	if v.rotation == 90 || v.rotation == 270 {
		// Dimensions swap when rotated
		bounds := img.Bounds()
		imgW := float64(bounds.Dy())
		imgH := float64(bounds.Dx())
		viewW := float64(v.scroll.Size().Width)
		viewH := float64(v.scroll.Size().Height)
		if imgW == 0 || imgH == 0 {
			return 1.0
		}
		scaleW := viewW / imgW
		scaleH := viewH / imgH
		if scaleW < scaleH {
			return scaleW
		}
		return scaleH
	}
	bounds := img.Bounds()
	imgW := float64(bounds.Dx())
	imgH := float64(bounds.Dy())
	viewW := float64(v.scroll.Size().Width)
	viewH := float64(v.scroll.Size().Height)
	if imgW == 0 || imgH == 0 {
		return 1.0
	}
	scaleW := viewW / imgW
	scaleH := viewH / imgH
	if scaleW < scaleH {
		return scaleW
	}
	return scaleH
}

// ZoomFit fits the image to the window.
func (v *Viewer) ZoomFit() {
	v.mu.Lock()
	v.zoomLevel = 1.0
	v.fitMode = true
	isAnimated := v.isAnimated
	v.mu.Unlock()

	if !isAnimated {
		v.applyTransforms()
	}
}

// ZoomOriginal displays the image at its original size (100%).
func (v *Viewer) ZoomOriginal() {
	v.mu.Lock()
	v.zoomLevel = 1.0
	v.fitMode = false
	isAnimated := v.isAnimated
	v.mu.Unlock()

	if !isAnimated {
		v.applyTransforms()
	}
}

// Clear clears the current image and stops any animation.
func (v *Viewer) Clear() {
	v.stopAnimation()
	v.imageCanvas.Image = nil
	v.imageCanvas.SetMinSize(fyne.NewSize(0, 0))
	v.imageCanvas.Refresh()
	v.mu.Lock()
	v.originalImg = nil
	v.zoomLevel = 1.0
	v.rotation = 0
	v.fitMode = true
	v.invalidateRotationCacheLocked()
	v.mu.Unlock()
}

// rotateImage rotates a Go image by the specified degrees (90, 180, 270).
// Uses direct RGBA buffer manipulation for performance.
func rotateImage(src goimage.Image, degrees int) goimage.Image {
	rgba := toRGBA(src)
	bounds := rgba.Bounds()
	w := bounds.Dx()
	h := bounds.Dy()
	srcStride := rgba.Stride
	srcPix := rgba.Pix

	switch degrees {
	case 90:
		dst := goimage.NewRGBA(goimage.Rect(0, 0, h, w))
		dstStride := dst.Stride
		for y := 0; y < h; y++ {
			for x := 0; x < w; x++ {
				srcOff := y*srcStride + x*4
				dstOff := x*dstStride + (h-1-y)*4
				copy(dst.Pix[dstOff:dstOff+4], srcPix[srcOff:srcOff+4])
			}
		}
		return dst

	case 180:
		dst := goimage.NewRGBA(goimage.Rect(0, 0, w, h))
		dstStride := dst.Stride
		for y := 0; y < h; y++ {
			for x := 0; x < w; x++ {
				srcOff := y*srcStride + x*4
				dstOff := (h-1-y)*dstStride + (w-1-x)*4
				copy(dst.Pix[dstOff:dstOff+4], srcPix[srcOff:srcOff+4])
			}
		}
		return dst

	case 270:
		dst := goimage.NewRGBA(goimage.Rect(0, 0, h, w))
		dstStride := dst.Stride
		for y := 0; y < h; y++ {
			for x := 0; x < w; x++ {
				srcOff := y*srcStride + x*4
				dstOff := (w-1-x)*dstStride + y*4
				copy(dst.Pix[dstOff:dstOff+4], srcPix[srcOff:srcOff+4])
			}
		}
		return dst

	default:
		return src
	}
}

// toRGBA converts any image.Image to *image.RGBA for direct pixel access.
// Uses draw.Draw which has optimised fast-paths for common source types
// (YCbCr from JPEG, Paletted from GIF, NRGBA from PNG) — ~3-4× faster than
// per-pixel Set/At.
func toRGBA(src goimage.Image) *goimage.RGBA {
	if rgba, ok := src.(*goimage.RGBA); ok {
		return rgba
	}
	bounds := src.Bounds()
	dst := goimage.NewRGBA(bounds)
	godraw.Draw(dst, bounds, src, bounds.Min, godraw.Src)
	return dst
}
