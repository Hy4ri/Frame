package gui

import (
	goimage "image"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/container"
)

// Viewer handles image display with zoom and rotation support.
type Viewer struct {
	scroll      *container.Scroll
	imageCanvas *canvas.Image
	zoomLevel   float64
	rotation    int // 0, 90, 180, 270 degrees
	originalImg goimage.Image
	fitMode     bool
}

// NewViewer creates a new image viewer widget.
func NewViewer() *Viewer {
	v := &Viewer{
		zoomLevel: 1.0,
		rotation:  0,
		fitMode:   true,
	}

	v.imageCanvas = canvas.NewImageFromImage(nil)
	v.imageCanvas.FillMode = canvas.ImageFillContain
	v.imageCanvas.ScaleMode = canvas.ImageScaleSmooth

	v.scroll = container.NewScroll(v.imageCanvas)

	return v
}

// Widget returns the scroll container that holds the image.
func (v *Viewer) Widget() fyne.CanvasObject {
	return v.scroll
}

// LoadImage loads and displays an image from the given path.
func (v *Viewer) LoadImage(path string) {
	v.rotation = 0
	v.zoomLevel = 1.0
	v.fitMode = true

	img, err := LoadImageFromFile(path)
	if err != nil {
		v.imageCanvas.Image = nil
		v.imageCanvas.Refresh()
		return
	}

	v.originalImg = img
	v.applyTransforms()
}

// applyTransforms applies rotation and zoom to the image.
func (v *Viewer) applyTransforms() {
	if v.originalImg == nil {
		return
	}

	displayImg := v.originalImg

	// Apply rotation
	if v.rotation != 0 {
		displayImg = rotateImage(displayImg, v.rotation)
	}

	v.imageCanvas.Image = displayImg

	if v.fitMode {
		v.imageCanvas.FillMode = canvas.ImageFillContain
		v.imageCanvas.SetMinSize(fyne.NewSize(0, 0))
	} else {
		bounds := displayImg.Bounds()
		w := float32(float64(bounds.Dx()) * v.zoomLevel)
		h := float32(float64(bounds.Dy()) * v.zoomLevel)

		v.imageCanvas.FillMode = canvas.ImageFillOriginal
		v.imageCanvas.SetMinSize(fyne.NewSize(w, h))
	}

	v.imageCanvas.Refresh()
}

// Rotate rotates the image by 90 degrees.
func (v *Viewer) Rotate(clockwise bool) {
	if clockwise {
		v.rotation = (v.rotation + 90) % 360
	} else {
		v.rotation = (v.rotation + 270) % 360
	}
	v.applyTransforms()
}

// ZoomIn increases zoom by 10%.
func (v *Viewer) ZoomIn() {
	if v.fitMode {
		v.zoomLevel = 1.0
		v.fitMode = false
	}
	v.zoomLevel *= 1.1
	if v.zoomLevel > 10.0 {
		v.zoomLevel = 10.0
	}
	v.applyTransforms()
}

// ZoomOut decreases zoom by 10%.
func (v *Viewer) ZoomOut() {
	if v.fitMode {
		v.zoomLevel = 1.0
		v.fitMode = false
	}
	v.zoomLevel /= 1.1
	if v.zoomLevel < 0.1 {
		v.zoomLevel = 0.1
	}
	v.applyTransforms()
}

// ZoomFit fits the image to the window.
func (v *Viewer) ZoomFit() {
	v.zoomLevel = 1.0
	v.fitMode = true
	v.applyTransforms()
}

// ZoomOriginal displays the image at its original size (100%).
func (v *Viewer) ZoomOriginal() {
	v.zoomLevel = 1.0
	v.fitMode = false
	v.applyTransforms()
}

// Clear clears the current image.
func (v *Viewer) Clear() {
	v.imageCanvas.Image = nil
	v.imageCanvas.SetMinSize(fyne.NewSize(0, 0))
	v.imageCanvas.Refresh()
	v.originalImg = nil
	v.zoomLevel = 1.0
	v.rotation = 0
	v.fitMode = true
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
func toRGBA(src goimage.Image) *goimage.RGBA {
	if rgba, ok := src.(*goimage.RGBA); ok {
		return rgba
	}
	bounds := src.Bounds()
	dst := goimage.NewRGBA(bounds)
	for y := bounds.Min.Y; y < bounds.Max.Y; y++ {
		for x := bounds.Min.X; x < bounds.Max.X; x++ {
			dst.Set(x, y, src.At(x, y))
		}
	}
	return dst
}
