package gui

import (
	"github.com/diamondburned/gotk4/pkg/gdk/v4"
	"github.com/diamondburned/gotk4/pkg/gdkpixbuf/v2"
	"github.com/diamondburned/gotk4/pkg/gtk/v4"
)

// Viewer handles image display with zoom and rotation support
type Viewer struct {
	widget      *gtk.ScrolledWindow
	picture     *gtk.Picture
	currentPath string
	zoomLevel   float64
	rotation    int // 0, 90, 180, 270 degrees
	originalBuf *gdkpixbuf.Pixbuf
	fitMode     bool // true = fit to window, false = use zoomLevel
}

// NewViewer creates a new image viewer widget
func NewViewer() *Viewer {
	v := &Viewer{
		zoomLevel: 1.0,
		rotation:  0,
		fitMode:   true,
	}

	// Create scrolled window for panning
	v.widget = gtk.NewScrolledWindow()
	v.widget.SetHExpand(true)
	v.widget.SetVExpand(true)
	v.widget.SetPolicy(gtk.PolicyAutomatic, gtk.PolicyAutomatic)

	// Create picture widget for displaying images
	v.picture = gtk.NewPicture()
	v.picture.SetCanShrink(true)
	v.picture.SetKeepAspectRatio(true) // Prevent stretching
	v.picture.SetContentFit(gtk.ContentFitContain)
	v.picture.SetHAlign(gtk.AlignCenter)
	v.picture.SetVAlign(gtk.AlignCenter)

	v.widget.SetChild(v.picture)

	// Apply dark background class for optimal image viewing
	v.widget.AddCSSClass("image-viewport")

	// Add scroll wheel zoom
	scrollController := gtk.NewEventControllerScroll(gtk.EventControllerScrollVertical)
	scrollController.ConnectScroll(func(dx, dy float64) bool {
		// Check for Ctrl key (zoom with Ctrl+Scroll)
		// dy < 0 = scroll up = zoom in, dy > 0 = scroll down = zoom out
		if dy < 0 {
			v.ZoomIn()
		} else if dy > 0 {
			v.ZoomOut()
		}
		return true // Event handled
	})
	v.widget.AddController(scrollController)

	return v
}

// LoadImage loads and displays an image from the given path
func (v *Viewer) LoadImage(path string) {
	v.currentPath = path
	v.rotation = 0
	v.zoomLevel = 1.0
	v.fitMode = true

	// Load the image using GdkPixbuf
	pixbuf, err := gdkpixbuf.NewPixbufFromFile(path)
	if err != nil {
		// Try to show error in the picture widget
		v.picture.SetPaintable(nil)
		return
	}

	v.originalBuf = pixbuf
	v.applyTransforms()
}

// applyTransforms applies rotation and zoom to the image
func (v *Viewer) applyTransforms() {
	if v.originalBuf == nil {
		return
	}

	// Apply rotation if needed
	var displayBuf *gdkpixbuf.Pixbuf
	switch v.rotation {
	case 90:
		displayBuf = v.originalBuf.RotateSimple(gdkpixbuf.PixbufRotateClockwise)
	case 180:
		displayBuf = v.originalBuf.RotateSimple(gdkpixbuf.PixbufRotateUpsidedown)
	case 270:
		displayBuf = v.originalBuf.RotateSimple(gdkpixbuf.PixbufRotateCounterclockwise)
	default:
		displayBuf = v.originalBuf
	}

	if displayBuf == nil {
		displayBuf = v.originalBuf
	}

	if v.fitMode {
		// Fit to window - let GTK handle scaling
		v.picture.SetContentFit(gtk.ContentFitContain)
		texture := gdk.NewTextureForPixbuf(displayBuf)
		v.picture.SetPaintable(texture)
	} else {
		// Manual zoom - scale the pixbuf ourselves
		origW := displayBuf.Width()
		origH := displayBuf.Height()
		newW := int(float64(origW) * v.zoomLevel)
		newH := int(float64(origH) * v.zoomLevel)

		if newW > 0 && newH > 0 {
			scaled := displayBuf.ScaleSimple(newW, newH, gdkpixbuf.InterpBilinear)
			if scaled != nil {
				displayBuf = scaled
			}
		}

		// Use ContentFitFill but set explicit size to prevent stretching
		v.picture.SetContentFit(gtk.ContentFitFill)
		v.picture.SetSizeRequest(newW, newH)
		texture := gdk.NewTextureForPixbuf(displayBuf)
		v.picture.SetPaintable(texture)
	}
}

// Rotate rotates the image by 90 degrees
func (v *Viewer) Rotate(clockwise bool) {
	if clockwise {
		v.rotation = (v.rotation + 90) % 360
	} else {
		v.rotation = (v.rotation + 270) % 360
	}
	v.applyTransforms()
}

// ZoomIn increases zoom by 10%
func (v *Viewer) ZoomIn() {
	if v.fitMode {
		// First zoom in from fit mode: calculate current effective zoom
		v.zoomLevel = 1.0
		v.fitMode = false
	}
	v.zoomLevel *= 1.1
	if v.zoomLevel > 10.0 {
		v.zoomLevel = 10.0
	}
	v.applyTransforms()
}

// ZoomOut decreases zoom by 10%
func (v *Viewer) ZoomOut() {
	if v.fitMode {
		// First zoom out from fit mode
		v.zoomLevel = 1.0
		v.fitMode = false
	}
	v.zoomLevel /= 1.1
	if v.zoomLevel < 0.1 {
		v.zoomLevel = 0.1
	}
	v.applyTransforms()
}

// ZoomFit fits the image to the window
func (v *Viewer) ZoomFit() {
	v.zoomLevel = 1.0
	v.fitMode = true
	v.picture.SetSizeRequest(-1, -1) // Reset size request
	v.applyTransforms()
}

// ZoomOriginal displays the image at its original size (100%)
func (v *Viewer) ZoomOriginal() {
	v.zoomLevel = 1.0
	v.fitMode = false
	v.applyTransforms()
}

// Clear clears the current image
func (v *Viewer) Clear() {
	v.picture.SetPaintable(nil)
	v.picture.SetSizeRequest(-1, -1)
	v.originalBuf = nil
	v.currentPath = ""
	v.zoomLevel = 1.0
	v.rotation = 0
	v.fitMode = true
}

// GetPixbuf returns the original pixbuf for editing
func (v *Viewer) GetPixbuf() *gdkpixbuf.Pixbuf {
	return v.originalBuf
}

// GetCurrentPath returns the current image path
func (v *Viewer) GetCurrentPath() string {
	return v.currentPath
}
