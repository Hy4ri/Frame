package gui

import (
	goimage "image"
	godraw "image/draw"
	"math"
	"sync"
	"sync/atomic"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/driver/desktop"
	"fyne.io/fyne/v2/widget"

	"github.com/Hy4ri/frame/internal/animation"
)

type Viewer struct {
	widget.BaseWidget
	imageCanvas *canvas.Image
	cache       *imageCache

	loadGen uint64

	mu          sync.Mutex
	originalImg goimage.Image
	scale       float32
	offsetX     float32
	offsetY     float32
	mouseX      float32
	mouseY      float32
	rotation    int
	viewportW   float32
	viewportH   float32
	needsFit    bool

	anim       *animation.Animation
	animStop   chan struct{}
	animDone   chan struct{}
	isAnimated bool

	cachedRotatedImg goimage.Image
	cachedRotation   int
	cachedOrigPtr    goimage.Image
}

func NewViewer() *Viewer {
	v := &Viewer{
		scale:          1.0,
		rotation:       0,
		cache:          newImageCache(defaultCacheCapacity),
		cachedRotation: -1,
	}
	v.ExtendBaseWidget(v)

	v.imageCanvas = canvas.NewImageFromImage(nil)
	v.imageCanvas.FillMode = canvas.ImageFillStretch
	v.imageCanvas.ScaleMode = canvas.ImageScaleSmooth

	return v
}

func (v *Viewer) Widget() fyne.CanvasObject {
	return v
}

func (v *Viewer) CreateRenderer() fyne.WidgetRenderer {
	return &viewerRenderer{viewer: v}
}

type viewerRenderer struct {
	viewer *Viewer
}

func (r *viewerRenderer) Layout(size fyne.Size) {
	v := r.viewer
	v.viewportW = float32(size.Width)
	v.viewportH = float32(size.Height)

	if v.needsFit {
		if v.originalImg != nil {
			v.needsFit = false
			v.fitToViewport()
		}
	}

	v.positionImage()
}

func (r *viewerRenderer) MinSize() fyne.Size {
	return fyne.NewSize(100, 100)
}

func (r *viewerRenderer) Refresh() {
	v := r.viewer
	if v.needsFit && v.originalImg != nil && v.viewportW > 0 && v.viewportH > 0 {
		v.needsFit = false
		v.fitToViewport()
	}
	v.positionImage()
	canvas.Refresh(v.imageCanvas)
}

func (r *viewerRenderer) Objects() []fyne.CanvasObject {
	return []fyne.CanvasObject{r.viewer.imageCanvas}
}

func (r *viewerRenderer) Destroy() {}

func (v *Viewer) positionImage() {
	if v.imageCanvas.Image == nil {
		return
	}
	bounds := v.imageCanvas.Image.Bounds()
	w := float32(bounds.Dx()) * v.scale
	h := float32(bounds.Dy()) * v.scale
	v.imageCanvas.Resize(fyne.NewSize(w, h))
	v.imageCanvas.Move(fyne.NewPos(v.offsetX, v.offsetY))
}

func (v *Viewer) MouseMoved(event *desktop.MouseEvent) {
	v.mouseX = event.Position.X
	v.mouseY = event.Position.Y
}

func (v *Viewer) MouseIn(*desktop.MouseEvent) {}
func (v *Viewer) MouseOut()                   {}

func (v *Viewer) Scrolled(ev *fyne.ScrollEvent) {
	factor := float32(1.0) + float32(ev.Scrolled.DY)*0.01
	if factor < 0.5 {
		factor = 0.5
	}
	if factor > 2.0 {
		factor = 2.0
	}

	newScale := v.scale * factor
	if newScale < 0.1 {
		newScale = 0.1
	}
	if newScale > 10.0 {
		newScale = 10.0
	}

	imgX := (v.mouseX - v.offsetX) / v.scale
	imgY := (v.mouseY - v.offsetY) / v.scale
	v.offsetX = v.mouseX - (imgX * newScale)
	v.offsetY = v.mouseY - (imgY * newScale)
	v.scale = newScale
	v.Refresh()
}

func (v *Viewer) Dragged(event *fyne.DragEvent) {
	v.offsetX += event.Dragged.DX
	v.offsetY += event.Dragged.DY
	v.Refresh()
}

func (v *Viewer) DragEnd() {}

func (v *Viewer) LoadImage(path string) {
	gen := atomic.AddUint64(&v.loadGen, 1)

	v.stopAnimation()

	v.mu.Lock()
	v.rotation = 0
	v.scale = 1.0
	v.offsetX = 0
	v.offsetY = 0
	v.needsFit = true
	v.invalidateRotationCacheLocked()
	v.mu.Unlock()

	if animation.IsAnimatable(path) {
		go func() {
			anim, err := animation.Decode(path)
			if err != nil || anim == nil {
				v.decodeAndCommitStatic(path, gen)
				return
			}
			if atomic.LoadUint64(&v.loadGen) != gen {
				return
			}
			v.mu.Lock()
			v.anim = anim
			v.isAnimated = true
			firstFrame, _ := anim.Frame(0)
			v.originalImg = firstFrame
			v.mu.Unlock()
			v.applyTransforms()
			v.fitToViewport()
			if v.viewportW > 0 && v.viewportH > 0 {
				v.needsFit = false
			}
			v.Refresh()
			v.startAnimation()
		}()
		return
	}

	if img, ok := v.cache.Get(path); ok {
		v.mu.Lock()
		v.originalImg = img
		v.invalidateRotationCacheLocked()
		v.mu.Unlock()
		v.applyTransforms()
		v.fitToViewport()
		if v.viewportW > 0 && v.viewportH > 0 {
			v.needsFit = false
		}
		v.Refresh()
		return
	}

	go v.decodeAndCommitStatic(path, gen)
}

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
		return
	}

	v.mu.Lock()
	v.originalImg = img
	v.invalidateRotationCacheLocked()
	v.mu.Unlock()
	v.applyTransforms()
	v.fitToViewport()
	if v.viewportW > 0 && v.viewportH > 0 {
		v.needsFit = false
	}
	v.Refresh()
}

func (v *Viewer) PrefetchImage(path string) {
	if _, ok := v.cache.Get(path); ok {
		return
	}
	if animation.IsAnimatable(path) {
		return
	}
	img, err := LoadImageFromFile(path)
	if err == nil {
		v.cache.Put(path, img)
	}
}

func (v *Viewer) fitToViewport() {
	v.mu.Lock()
	origImg := v.originalImg
	rotation := v.rotation
	v.mu.Unlock()

	if origImg == nil || v.viewportW == 0 || v.viewportH == 0 {
		return
	}

	bounds := origImg.Bounds()
	w := float32(bounds.Dx())
	h := float32(bounds.Dy())

	if rotation == 90 || rotation == 270 {
		w, h = h, w
	}

	scaleW := v.viewportW / w
	scaleH := v.viewportH / h
	if scaleW < scaleH {
		v.scale = scaleW
	} else {
		v.scale = scaleH
	}

	v.offsetX = (v.viewportW - w*v.scale) / 2
	v.offsetY = (v.viewportH - h*v.scale) / 2
}

func (v *Viewer) zoomFromCenter(factor float32) {
	v.mu.Lock()
	isAnimated := v.isAnimated
	v.mu.Unlock()

	if !isAnimated {
		newScale := v.scale * factor
		if newScale < 0.1 {
			newScale = 0.1
		}
		if newScale > 10.0 {
			newScale = 10.0
		}

		cx := v.viewportW / 2
		cy := v.viewportH / 2
		imgX := (cx - v.offsetX) / v.scale
		imgY := (cy - v.offsetY) / v.scale
		v.offsetX = cx - (imgX * newScale)
		v.offsetY = cy - (imgY * newScale)
		v.scale = newScale
		v.Refresh()
	}
}

func (v *Viewer) ZoomIn() {
	v.zoomFromCenter(1.05)
}

func (v *Viewer) ZoomOut() {
	v.zoomFromCenter(1.0 / 1.05)
}

func (v *Viewer) ZoomFit() {
	v.mu.Lock()
	isAnimated := v.isAnimated
	v.mu.Unlock()

	if !isAnimated {
		v.fitToViewport()
		v.applyTransforms()
		v.Refresh()
	}
}

func (v *Viewer) ZoomOriginal() {
	v.mu.Lock()
	origImg := v.originalImg
	isAnimated := v.isAnimated
	v.mu.Unlock()

	if origImg == nil || isAnimated {
		return
	}

	v.scale = 1.0
	bounds := origImg.Bounds()
	w := float32(bounds.Dx())
	h := float32(bounds.Dy())

	if v.viewportW > 0 && v.viewportH > 0 {
		v.offsetX = float32(math.Max(float64(0), float64((v.viewportW-w)/2)))
		v.offsetY = float32(math.Max(float64(0), float64((v.viewportH-h)/2)))
	} else {
		v.offsetX = 0
		v.offsetY = 0
	}
	v.applyTransforms()
	v.Refresh()
}

func (v *Viewer) applyTransforms() {
	v.mu.Lock()
	origImg := v.originalImg
	rotation := v.rotation
	v.mu.Unlock()

	if origImg == nil {
		return
	}

	displayImg := v.getRotatedImage(origImg, rotation)
	v.imageCanvas.Image = displayImg
}

func (v *Viewer) Rotate(clockwise bool) {
	v.mu.Lock()
	if clockwise {
		v.rotation = (v.rotation + 90) % 360
	} else {
		v.rotation = (v.rotation + 270) % 360
	}
	isAnimated := v.isAnimated
	v.mu.Unlock()

	if !isAnimated {
		v.applyTransforms()
		v.Refresh()
	}
}

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
				scale := v.scale
				offsetX := v.offsetX
				offsetY := v.offsetY
				v.mu.Unlock()

				displayImg := goimage.Image(frame)
				if rotation != 0 {
					displayImg = rotateImage(displayImg, rotation)
				}

				bounds := displayImg.Bounds()
				w := float32(bounds.Dx()) * scale
				h := float32(bounds.Dy()) * scale

				v.imageCanvas.Image = displayImg
				v.imageCanvas.Resize(fyne.NewSize(w, h))
				v.imageCanvas.Move(fyne.NewPos(offsetX, offsetY))
				v.imageCanvas.Refresh()

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
		<-done
	}
}

func (v *Viewer) Clear() {
	v.stopAnimation()
	v.imageCanvas.Image = nil
	v.mu.Lock()
	v.originalImg = nil
	v.scale = 1.0
	v.rotation = 0
	v.offsetX = 0
	v.offsetY = 0
	v.invalidateRotationCacheLocked()
	v.mu.Unlock()
	v.Refresh()
}

func (v *Viewer) invalidateRotationCacheLocked() {
	v.cachedRotatedImg = nil
	v.cachedOrigPtr = nil
	v.cachedRotation = -1
}

func (v *Viewer) getRotatedImage(origImg goimage.Image, rotation int) goimage.Image {
	if rotation == 0 {
		return origImg
	}

	v.mu.Lock()
	if v.cachedOrigPtr == origImg && v.cachedRotation == rotation && v.cachedRotatedImg != nil {
		cached := v.cachedRotatedImg
		v.mu.Unlock()
		return cached
	}
	v.mu.Unlock()

	rotated := rotateImage(origImg, rotation)

	v.mu.Lock()
	v.cachedOrigPtr = origImg
	v.cachedRotation = rotation
	v.cachedRotatedImg = rotated
	v.mu.Unlock()

	return rotated
}

const maxImageDimension = 16384

func rotateImage(src goimage.Image, degrees int) goimage.Image {
	rgba := toRGBA(src)
	bounds := rgba.Bounds()
	w := bounds.Dx()
	h := bounds.Dy()

	if w > maxImageDimension || h > maxImageDimension {
		return src
	}
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

func toRGBA(src goimage.Image) *goimage.RGBA {
	if rgba, ok := src.(*goimage.RGBA); ok {
		return rgba
	}
	bounds := src.Bounds()
	w := bounds.Dx()
	h := bounds.Dy()
	if w > maxImageDimension || h > maxImageDimension {
		return goimage.NewRGBA(goimage.Rect(0, 0, 1, 1))
	}
	dst := goimage.NewRGBA(bounds)
	godraw.Draw(dst, bounds, src, bounds.Min, godraw.Src)
	return dst
}
