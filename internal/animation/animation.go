// Package animation provides decoding and frame compositing for animated
// image formats (GIF, APNG).
package animation

import (
	"image"
	"image/color"
	"image/draw"
	"image/gif"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/kettek/apng"
)

// minFrameDelay is the minimum delay between frames to prevent CPU spinning.
// Browsers typically clamp GIF delays below 20ms to 100ms; we use 10ms as a
// reasonable floor.
const minFrameDelay = 10 * time.Millisecond

// Animation holds pre-composited frames and timing data for playback.
type Animation struct {
	Frames    []image.Image   // Full-size composited frames ready for display
	Delays    []time.Duration // Per-frame display duration
	LoopCount int             // 0 = loop forever, >0 = loop N times
}

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

// decodeGIF decodes an animated GIF file, compositing frames according to
// their disposal methods.
func decodeGIF(path string) (*Animation, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	g, err := gif.DecodeAll(f)
	if err != nil {
		return nil, err
	}

	// Single-frame GIF: not animated
	if len(g.Image) <= 1 {
		return nil, nil
	}

	width := g.Config.Width
	height := g.Config.Height

	// If config dimensions are zero (rare), infer from first frame
	if width == 0 || height == 0 {
		bounds := g.Image[0].Bounds()
		width = bounds.Max.X
		height = bounds.Max.Y
	}

	canvasRect := image.Rect(0, 0, width, height)
	canvas := image.NewRGBA(canvasRect)
	var prevCanvas *image.RGBA

	anim := &Animation{
		Frames:    make([]image.Image, 0, len(g.Image)),
		Delays:    make([]time.Duration, 0, len(g.Image)),
		LoopCount: g.LoopCount,
	}

	for i, frame := range g.Image {
		// Handle disposal from the PREVIOUS frame before drawing the current one.
		if i > 0 {
			switch g.Disposal[i-1] {
			case gif.DisposalBackground:
				// Clear the previous frame's area to transparent.
				prevBounds := g.Image[i-1].Bounds()
				clearRect(canvas, prevBounds)
			case gif.DisposalPrevious:
				// Restore to the saved state.
				if prevCanvas != nil {
					copy(canvas.Pix, prevCanvas.Pix)
				}
				// DisposalNone (0) or unspecified: leave canvas as-is.
			}
		}

		// Save canvas state before drawing if the current frame's disposal
		// is DisposalPrevious (we need to restore after this frame).
		if i < len(g.Disposal) && g.Disposal[i] == gif.DisposalPrevious {
			prevCanvas = cloneRGBA(canvas)
		}

		// Draw the current frame onto the canvas.
		draw.Draw(canvas, frame.Bounds(), frame, frame.Bounds().Min, draw.Over)

		// Save composited frame.
		composited := cloneRGBA(canvas)
		anim.Frames = append(anim.Frames, composited)

		// Convert delay: GIF delays are in 100ths of a second.
		delay := time.Duration(g.Delay[i]) * 10 * time.Millisecond
		if delay < minFrameDelay {
			delay = minFrameDelay
		}
		anim.Delays = append(anim.Delays, delay)
	}

	return anim, nil
}

// decodeAPNG decodes an animated PNG file, compositing frames according to
// their dispose and blend operations.
func decodeAPNG(path string) (*Animation, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	a, err := apng.DecodeAll(f)
	if err != nil {
		return nil, err
	}

	// Single-frame PNG: not animated.
	if len(a.Frames) <= 1 {
		return nil, nil
	}

	// Determine canvas size from the first frame (default image).
	firstBounds := a.Frames[0].Image.Bounds()
	width := firstBounds.Dx()
	height := firstBounds.Dy()

	canvasRect := image.Rect(0, 0, width, height)
	canvas := image.NewRGBA(canvasRect)
	var prevCanvas *image.RGBA

	anim := &Animation{
		Frames:    make([]image.Image, 0, len(a.Frames)),
		Delays:    make([]time.Duration, 0, len(a.Frames)),
		LoopCount: int(a.LoopCount),
	}

	for _, frame := range a.Frames {
		// Save canvas state before drawing if dispose op is "previous".
		if frame.DisposeOp == apng.DISPOSE_OP_PREVIOUS {
			prevCanvas = cloneRGBA(canvas)
		}

		// Calculate the frame's target rectangle on the canvas.
		frameRect := image.Rect(
			frame.XOffset, frame.YOffset,
			frame.XOffset+frame.Image.Bounds().Dx(),
			frame.YOffset+frame.Image.Bounds().Dy(),
		)

		// Apply blend operation.
		switch frame.BlendOp {
		case apng.BLEND_OP_SOURCE:
			// Replace the region entirely.
			draw.Draw(canvas, frameRect, frame.Image, frame.Image.Bounds().Min, draw.Src)
		default:
			// BLEND_OP_OVER: alpha-composite over existing content.
			draw.Draw(canvas, frameRect, frame.Image, frame.Image.Bounds().Min, draw.Over)
		}

		// Save composited frame.
		composited := cloneRGBA(canvas)
		anim.Frames = append(anim.Frames, composited)

		// Get frame delay.
		delay := time.Duration(float64(time.Second) * frame.GetDelay())
		if delay < minFrameDelay {
			delay = minFrameDelay
		}
		anim.Delays = append(anim.Delays, delay)

		// Handle disposal for the CURRENT frame (affects what the next frame sees).
		switch frame.DisposeOp {
		case apng.DISPOSE_OP_BACKGROUND:
			// Clear the frame's area to transparent.
			clearRect(canvas, frameRect)
		case apng.DISPOSE_OP_PREVIOUS:
			// Restore to saved state.
			if prevCanvas != nil {
				copy(canvas.Pix, prevCanvas.Pix)
			}
			// DISPOSE_OP_NONE: leave canvas as-is.
		}
	}

	return anim, nil
}

// cloneRGBA creates a deep copy of an RGBA image.
func cloneRGBA(src *image.RGBA) *image.RGBA {
	dst := image.NewRGBA(src.Bounds())
	copy(dst.Pix, src.Pix)
	return dst
}

// clearRect fills a rectangle in the canvas with transparent pixels.
func clearRect(canvas *image.RGBA, rect image.Rectangle) {
	transparent := color.RGBA{0, 0, 0, 0}
	for y := rect.Min.Y; y < rect.Max.Y; y++ {
		for x := rect.Min.X; x < rect.Max.X; x++ {
			canvas.SetRGBA(x, y, transparent)
		}
	}
}
