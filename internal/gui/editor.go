// Package gui provides GTK4 user interface components for Frame.
package gui

import (
	"fmt"

	"github.com/Hy4ri/frame/internal/image"
	"github.com/diamondburned/gotk4/pkg/cairo"
	"github.com/diamondburned/gotk4/pkg/gdk/v4"
	"github.com/diamondburned/gotk4/pkg/gdkpixbuf/v2"
	"github.com/diamondburned/gotk4/pkg/gtk/v4"
)

// Tool represents the currently active editing tool
type Tool int

const (
	ToolNone Tool = iota
	ToolCrop
	ToolPen
	ToolEraser
)

// EditorView provides image editing functionality
type EditorView struct {
	widget      *gtk.Box
	overlay     *gtk.Overlay
	picture     *gtk.Picture
	drawArea    *gtk.DrawingArea
	toolbar     *gtk.Box
	propsPanel  *gtk.Box
	cropPanel   *gtk.Box
	statusLabel *gtk.Label

	// Tool toggle buttons (for radio-like behavior)
	cropBtn *gtk.ToggleButton
	penBtn  *gtk.ToggleButton

	// Current state
	currentTool Tool
	brushSize   float64
	brushColor  string
	isDrawing   bool
	currentPath string
	originalBuf *gdkpixbuf.Pixbuf
	preCropBuf  *gdkpixbuf.Pixbuf // Stores image before crop for undo
	postCropBuf *gdkpixbuf.Pixbuf // Stores image after crop for redo
	session     *image.EditSession

	// Current stroke being drawn
	currentStroke *image.Stroke

	// History for undo/redo
	undoStack []image.EditAction
	redoStack []image.EditAction

	// Crop state
	cropStartX, cropStartY float64
	cropEndX, cropEndY     float64
	isCropping             bool
	cropActive             bool

	// Callbacks
	onSave   func(asNew bool)
	onCancel func()
}

// Color palette for the pen tool
var colorPalette = []string{
	"#000000", // Black
	"#FFFFFF", // White
	"#FF0000", // Red
	"#00FF00", // Green
	"#0000FF", // Blue
	"#FFFF00", // Yellow
	"#FF00FF", // Magenta
	"#00FFFF", // Cyan
	"#FF8000", // Orange
	"#8000FF", // Purple
}

// NewEditorView creates a new image editor view
func NewEditorView(onSave func(asNew bool), onCancel func()) *EditorView {
	e := &EditorView{
		currentTool: ToolNone,
		brushSize:   5.0,
		brushColor:  "#000000",
		undoStack:   make([]image.EditAction, 0),
		redoStack:   make([]image.EditAction, 0),
		onSave:      onSave,
		onCancel:    onCancel,
	}

	// Main container
	e.widget = gtk.NewBox(gtk.OrientationVertical, 0)
	e.widget.SetHExpand(true)
	e.widget.SetVExpand(true)

	// Create toolbar
	e.createToolbar()

	// Create editing area with overlay
	e.overlay = gtk.NewOverlay()
	e.overlay.SetHExpand(true)
	e.overlay.SetVExpand(true)

	// Picture for displaying the image
	e.picture = gtk.NewPicture()
	e.picture.SetCanShrink(true)
	e.picture.SetContentFit(gtk.ContentFitContain)
	e.picture.SetHAlign(gtk.AlignCenter)
	e.picture.SetVAlign(gtk.AlignCenter)
	e.overlay.SetChild(e.picture)

	// Drawing area overlay for strokes and crop selection
	e.drawArea = gtk.NewDrawingArea()
	e.drawArea.SetHExpand(true)
	e.drawArea.SetVExpand(true)
	e.drawArea.SetDrawFunc(e.onDraw)
	e.overlay.AddOverlay(e.drawArea)

	// Set up mouse event handling
	e.setupMouseEvents()

	// Scrolled window for the overlay
	scroll := gtk.NewScrolledWindow()
	scroll.SetChild(e.overlay)
	scroll.SetHExpand(true)
	scroll.SetVExpand(true)
	scroll.SetPolicy(gtk.PolicyAutomatic, gtk.PolicyAutomatic)
	scroll.AddCSSClass("image-viewport")

	// Add toolbar and content
	e.widget.Append(e.toolbar)
	e.widget.Append(scroll)

	// Create properties panel (for brush tools)
	e.createPropsPanel()

	// Create crop panel (for crop tool)
	e.createCropPanel()

	return e
}

// createToolbar builds the editor toolbar
func (e *EditorView) createToolbar() {
	e.toolbar = gtk.NewBox(gtk.OrientationHorizontal, 8)
	e.toolbar.SetMarginTop(8)
	e.toolbar.SetMarginBottom(8)
	e.toolbar.SetMarginStart(12)
	e.toolbar.SetMarginEnd(12)
	e.toolbar.AddCSSClass("toolbar")

	// Tool buttons group (radio-like behavior - only one active at a time)
	toolBox := gtk.NewBox(gtk.OrientationHorizontal, 4)

	e.cropBtn = gtk.NewToggleButton()
	e.cropBtn.SetIconName("edit-cut-symbolic")
	e.cropBtn.SetTooltipText("Crop (c)")
	e.cropBtn.ConnectToggled(func() {
		if e.cropBtn.Active() {
			e.selectTool(ToolCrop)
		} else if e.currentTool == ToolCrop {
			e.setTool(ToolNone)
		}
	})
	toolBox.Append(e.cropBtn)

	e.penBtn = gtk.NewToggleButton()
	e.penBtn.SetIconName("document-edit-symbolic")
	e.penBtn.SetTooltipText("Pen (p)")
	e.penBtn.ConnectToggled(func() {
		if e.penBtn.Active() {
			e.selectTool(ToolPen)
		} else if e.currentTool == ToolPen {
			e.setTool(ToolNone)
		}
	})
	toolBox.Append(e.penBtn)

	e.toolbar.Append(toolBox)

	// Separator
	sep1 := gtk.NewSeparator(gtk.OrientationVertical)
	sep1.SetMarginStart(8)
	sep1.SetMarginEnd(8)
	e.toolbar.Append(sep1)

	// Undo/Redo buttons
	historyBox := gtk.NewBox(gtk.OrientationHorizontal, 4)

	undoBtn := gtk.NewButtonFromIconName("edit-undo-symbolic")
	undoBtn.SetTooltipText("Undo (Ctrl+Z)")
	undoBtn.ConnectClicked(func() { e.Undo() })
	historyBox.Append(undoBtn)

	redoBtn := gtk.NewButtonFromIconName("edit-redo-symbolic")
	redoBtn.SetTooltipText("Redo (Ctrl+Y)")
	redoBtn.ConnectClicked(func() { e.Redo() })
	historyBox.Append(redoBtn)

	e.toolbar.Append(historyBox)

	// Spacer
	spacer := gtk.NewBox(gtk.OrientationHorizontal, 0)
	spacer.SetHExpand(true)
	e.toolbar.Append(spacer)

	// Cancel and Save buttons
	actionBox := gtk.NewBox(gtk.OrientationHorizontal, 8)

	cancelBtn := gtk.NewButtonWithLabel("Cancel")
	cancelBtn.ConnectClicked(func() {
		if e.onCancel != nil {
			e.onCancel()
		}
	})
	actionBox.Append(cancelBtn)

	saveBtn := gtk.NewButtonWithLabel("Save")
	saveBtn.AddCSSClass("suggested-action")
	saveBtn.ConnectClicked(func() {
		e.showSaveDialog()
	})
	actionBox.Append(saveBtn)

	e.toolbar.Append(actionBox)
}

// createPropsPanel creates the properties panel for brush settings
func (e *EditorView) createPropsPanel() {
	e.propsPanel = gtk.NewBox(gtk.OrientationHorizontal, 12)
	e.propsPanel.SetMarginStart(12)
	e.propsPanel.SetMarginEnd(12)
	e.propsPanel.SetMarginBottom(8)

	// Brush size control
	sizeLabel := gtk.NewLabel("Size:")
	e.propsPanel.Append(sizeLabel)

	sizeScale := gtk.NewScaleWithRange(gtk.OrientationHorizontal, 1, 50, 1)
	sizeScale.SetValue(e.brushSize)
	sizeScale.SetSizeRequest(150, -1)
	sizeScale.ConnectValueChanged(func() {
		e.brushSize = sizeScale.Value()
	})
	e.propsPanel.Append(sizeScale)

	// Color picker
	colorLabel := gtk.NewLabel("Color:")
	colorLabel.SetMarginStart(16)
	e.propsPanel.Append(colorLabel)

	colorBox := gtk.NewBox(gtk.OrientationHorizontal, 4)
	for _, color := range colorPalette {
		colorBtn := e.createColorButton(color)
		colorBox.Append(colorBtn)
	}
	e.propsPanel.Append(colorBox)

	// Insert props panel after toolbar
	// We'll add it dynamically when pen/eraser is selected
}

// createCropPanel creates the crop action panel
func (e *EditorView) createCropPanel() {
	e.cropPanel = gtk.NewBox(gtk.OrientationHorizontal, 12)
	e.cropPanel.SetMarginStart(12)
	e.cropPanel.SetMarginEnd(12)
	e.cropPanel.SetMarginBottom(8)

	// Status indicator
	e.statusLabel = gtk.NewLabel("Draw a selection on the image")
	e.statusLabel.AddCSSClass("dim-label")
	e.cropPanel.Append(e.statusLabel)

	// Spacer
	spacer := gtk.NewBox(gtk.OrientationHorizontal, 0)
	spacer.SetHExpand(true)
	e.cropPanel.Append(spacer)

	// Cancel crop button
	cancelCropBtn := gtk.NewButtonWithLabel("Cancel")
	cancelCropBtn.ConnectClicked(func() {
		e.cancelCrop()
	})
	e.cropPanel.Append(cancelCropBtn)

	// Apply crop button
	applyCropBtn := gtk.NewButtonWithLabel("Apply Crop")
	applyCropBtn.AddCSSClass("suggested-action")
	applyCropBtn.ConnectClicked(func() {
		e.applyCropToImage()
	})
	e.cropPanel.Append(applyCropBtn)
}

// createColorButton creates a color selection button with visible color swatch
func (e *EditorView) createColorButton(color string) *gtk.Button {
	btn := gtk.NewButton()
	btn.SetSizeRequest(28, 28)

	// Create a drawing area to show the color
	colorBox := gtk.NewDrawingArea()
	colorBox.SetSizeRequest(20, 20)

	// Parse the color once
	r, g, b := parseHexColor(color)

	colorBox.SetDrawFunc(func(area *gtk.DrawingArea, cr *cairo.Context, w, h int) {
		// Draw filled rectangle with the color
		cr.SetSourceRGB(r, g, b)
		cr.Rectangle(2, 2, float64(w-4), float64(h-4))
		cr.Fill()

		// Draw border
		cr.SetSourceRGB(0.5, 0.5, 0.5)
		cr.SetLineWidth(1)
		cr.Rectangle(2, 2, float64(w-4), float64(h-4))
		cr.Stroke()
	})

	btn.SetChild(colorBox)
	btn.ConnectClicked(func() {
		e.brushColor = color
	})

	return btn
}

// setupMouseEvents configures mouse handling for drawing and cropping
func (e *EditorView) setupMouseEvents() {
	// Motion controller for drawing
	motion := gtk.NewEventControllerMotion()
	motion.ConnectMotion(func(x, y float64) {
		if e.isDrawing && e.currentStroke != nil && e.currentTool == ToolPen {
			e.currentStroke.Points = append(e.currentStroke.Points, image.Point{X: x, Y: y})
			e.drawArea.QueueDraw()
		}
		if e.isDrawing && e.currentTool == ToolEraser {
			e.eraseStrokesAt(x, y)
		}
		if e.isCropping {
			e.cropEndX = x
			e.cropEndY = y
			e.drawArea.QueueDraw()
		}
	})
	e.drawArea.AddController(motion)

	// Gesture for click handling
	gesture := gtk.NewGestureClick()
	gesture.SetButton(1) // Primary button

	gesture.ConnectPressed(func(nPress int, x, y float64) {
		switch e.currentTool {
		case ToolPen:
			e.startStroke(x, y)
		case ToolEraser:
			e.isDrawing = true
			e.eraseStrokesAt(x, y)
		case ToolCrop:
			e.startCrop(x, y)
		}
	})

	gesture.ConnectReleased(func(nPress int, x, y float64) {
		switch e.currentTool {
		case ToolPen:
			e.endStroke()
		case ToolEraser:
			e.isDrawing = false
		case ToolCrop:
			e.endCrop()
		}
	})

	e.drawArea.AddController(gesture)
}

// eraseStrokesAt removes any strokes that intersect with the given point
func (e *EditorView) eraseStrokesAt(x, y float64) {
	if e.session == nil || len(e.session.Strokes) == 0 {
		return
	}

	eraserRadius := e.brushSize / 2
	strokesToRemove := []int{}

	// Find strokes that intersect with eraser position
	for i, stroke := range e.session.Strokes {
		for _, pt := range stroke.Points {
			dx := pt.X - x
			dy := pt.Y - y
			dist := dx*dx + dy*dy
			threshold := (eraserRadius + stroke.BrushSize/2) * (eraserRadius + stroke.BrushSize/2)
			if dist < threshold {
				strokesToRemove = append(strokesToRemove, i)
				break // Found intersection, no need to check more points
			}
		}
	}

	// Remove strokes in reverse order to preserve indices
	if len(strokesToRemove) > 0 {
		for i := len(strokesToRemove) - 1; i >= 0; i-- {
			idx := strokesToRemove[i]
			// Save for undo
			e.pushUndo(image.EditAction{
				Type:        "erase",
				Data:        e.session.Strokes[idx],
				Description: "Erase stroke",
			})
			// Remove stroke
			e.session.Strokes = append(e.session.Strokes[:idx], e.session.Strokes[idx+1:]...)
		}
		e.drawArea.QueueDraw()
	}
}

// startStroke begins a new drawing stroke (pen only)
func (e *EditorView) startStroke(x, y float64) {
	e.isDrawing = true
	e.currentStroke = &image.Stroke{
		Tool:      "pen",
		Color:     e.brushColor,
		BrushSize: e.brushSize,
		Points:    []image.Point{{X: x, Y: y}},
	}
}

// endStroke finishes the current stroke and adds it to the session
func (e *EditorView) endStroke() {
	if e.currentStroke != nil && len(e.currentStroke.Points) > 0 {
		e.session.AddStroke(*e.currentStroke)
		e.pushUndo(image.EditAction{
			Type:        "stroke",
			Data:        *e.currentStroke,
			Description: "Draw stroke",
		})
	}
	e.currentStroke = nil
	e.isDrawing = false
	e.drawArea.QueueDraw()
}

// startCrop begins crop selection
func (e *EditorView) startCrop(x, y float64) {
	e.isCropping = true
	e.cropStartX = x
	e.cropStartY = y
	e.cropEndX = x
	e.cropEndY = y
}

// endCrop finishes crop selection
func (e *EditorView) endCrop() {
	e.isCropping = false
	e.cropActive = true
	e.updateCropStatus()
	e.drawArea.QueueDraw()
}

// onDraw handles drawing the overlay (strokes and crop selection)
func (e *EditorView) onDraw(area *gtk.DrawingArea, cr *cairo.Context, width, height int) {
	// Draw existing strokes from session
	if e.session != nil {
		for _, stroke := range e.session.Strokes {
			e.drawStroke(cr, &stroke)
		}
	}

	// Draw current stroke in progress
	if e.currentStroke != nil {
		e.drawStroke(cr, e.currentStroke)
	}

	// Draw crop selection
	if e.currentTool == ToolCrop && (e.isCropping || e.cropActive) {
		e.drawCropOverlay(cr, width, height)
	}
}

// drawStroke renders a stroke to the cairo context
func (e *EditorView) drawStroke(cr *cairo.Context, stroke *image.Stroke) {
	if len(stroke.Points) < 2 {
		return
	}

	// Parse color
	r, g, b := parseHexColor(stroke.Color)

	if stroke.Tool == "eraser" {
		// For eraser, we use white (or transparent would be better with compositing)
		cr.SetSourceRGBA(1, 1, 1, 1)
	} else {
		cr.SetSourceRGB(r, g, b)
	}

	cr.SetLineWidth(stroke.BrushSize)
	cr.SetLineCap(cairo.LineCapRound)
	cr.SetLineJoin(cairo.LineJoinRound)

	cr.MoveTo(stroke.Points[0].X, stroke.Points[0].Y)
	for i := 1; i < len(stroke.Points); i++ {
		cr.LineTo(stroke.Points[i].X, stroke.Points[i].Y)
	}
	cr.Stroke()
}

// drawCropOverlay renders the crop selection UI
func (e *EditorView) drawCropOverlay(cr *cairo.Context, width, height int) {
	// Semi-transparent overlay outside crop area
	cr.SetSourceRGBA(0, 0, 0, 0.5)

	x1 := min(e.cropStartX, e.cropEndX)
	y1 := min(e.cropStartY, e.cropEndY)
	x2 := max(e.cropStartX, e.cropEndX)
	y2 := max(e.cropStartY, e.cropEndY)

	// Draw darkened areas outside selection
	cr.Rectangle(0, 0, float64(width), y1)
	cr.Fill()
	cr.Rectangle(0, y2, float64(width), float64(height)-y2)
	cr.Fill()
	cr.Rectangle(0, y1, x1, y2-y1)
	cr.Fill()
	cr.Rectangle(x2, y1, float64(width)-x2, y2-y1)
	cr.Fill()

	// Draw selection border
	cr.SetSourceRGB(1, 1, 1)
	cr.SetLineWidth(2)
	cr.Rectangle(x1, y1, x2-x1, y2-y1)
	cr.Stroke()

	// Draw corner handles
	handleSize := 8.0
	cr.SetSourceRGB(1, 1, 1)
	corners := [][2]float64{{x1, y1}, {x2, y1}, {x1, y2}, {x2, y2}}
	for _, c := range corners {
		cr.Rectangle(c[0]-handleSize/2, c[1]-handleSize/2, handleSize, handleSize)
		cr.Fill()
	}
}

// selectTool selects a tool and untoggles other tool buttons (radio behavior)
func (e *EditorView) selectTool(tool Tool) {
	// Untoggle other buttons without triggering their callbacks
	switch tool {
	case ToolCrop:
		if e.penBtn.Active() {
			e.penBtn.SetActive(false)
		}
	case ToolPen:
		if e.cropBtn.Active() {
			e.cropBtn.SetActive(false)
		}
	}
	e.setTool(tool)
}

// setTool changes the active tool
func (e *EditorView) setTool(tool Tool) {
	e.currentTool = tool

	// Show/hide properties panel for brush tools
	if tool == ToolPen || tool == ToolEraser {
		if e.propsPanel.Parent() == nil {
			e.widget.InsertChildAfter(e.propsPanel, e.toolbar)
		}
	} else {
		if e.propsPanel.Parent() != nil {
			e.widget.Remove(e.propsPanel)
		}
	}

	// Show/hide crop panel for crop tool
	if tool == ToolCrop {
		if e.cropPanel.Parent() == nil {
			e.widget.InsertChildAfter(e.cropPanel, e.toolbar)
		}
		e.updateCropStatus()
	} else {
		if e.cropPanel.Parent() != nil {
			e.widget.Remove(e.cropPanel)
		}
	}
}

// updateCropStatus updates the crop status label
func (e *EditorView) updateCropStatus() {
	if !e.cropActive {
		e.statusLabel.SetText("Draw a selection on the image")
	} else {
		w := int(max(e.cropEndX, e.cropStartX) - min(e.cropEndX, e.cropStartX))
		h := int(max(e.cropEndY, e.cropStartY) - min(e.cropEndY, e.cropStartY))
		e.statusLabel.SetText(fmt.Sprintf("Selection: %d × %d pixels", w, h))
	}
}

// cancelCrop clears the current crop selection
func (e *EditorView) cancelCrop() {
	e.cropActive = false
	e.isCropping = false
	e.cropStartX = 0
	e.cropStartY = 0
	e.cropEndX = 0
	e.cropEndY = 0
	e.updateCropStatus()
	e.drawArea.QueueDraw()
}

// applyCropToImage applies the crop selection to the actual image
func (e *EditorView) applyCropToImage() {
	if !e.cropActive || e.originalBuf == nil {
		return
	}

	// Store original for undo
	e.preCropBuf = e.originalBuf.Copy()

	// Get screen selection bounds
	screenX1 := min(e.cropStartX, e.cropEndX)
	screenY1 := min(e.cropStartY, e.cropEndY)
	screenX2 := max(e.cropStartX, e.cropEndX)
	screenY2 := max(e.cropStartY, e.cropEndY)

	// Get drawing area size (this is the overlay size)
	areaW := float64(e.drawArea.AllocatedWidth())
	areaH := float64(e.drawArea.AllocatedHeight())

	// Get image dimensions
	imgW := float64(e.originalBuf.Width())
	imgH := float64(e.originalBuf.Height())

	// Calculate scale to fit image in area (maintaining aspect ratio)
	scaleToFit := min(areaW/imgW, areaH/imgH)

	// Calculate displayed image size
	displayW := imgW * scaleToFit
	displayH := imgH * scaleToFit

	// Calculate offset (image is centered in the area)
	offsetX := (areaW - displayW) / 2
	offsetY := (areaH - displayH) / 2

	// Convert screen coordinates to image coordinates
	imgX1 := (screenX1 - offsetX) / scaleToFit
	imgY1 := (screenY1 - offsetY) / scaleToFit
	imgX2 := (screenX2 - offsetX) / scaleToFit
	imgY2 := (screenY2 - offsetY) / scaleToFit

	// Clamp to image bounds
	cropX := int(max(0, min(imgX1, imgW-1)))
	cropY := int(max(0, min(imgY1, imgH-1)))
	cropW := int(max(1, min(imgX2-imgX1, imgW-float64(cropX))))
	cropH := int(max(1, min(imgY2-imgY1, imgH-float64(cropY))))

	if cropW <= 0 || cropH <= 0 {
		return
	}

	// Create cropped pixbuf
	croppedBuf := e.originalBuf.NewSubpixbuf(cropX, cropY, cropW, cropH)
	if croppedBuf == nil {
		return
	}

	// Update the buffer and display
	e.originalBuf = croppedBuf.Copy()
	e.postCropBuf = e.originalBuf.Copy() // Store for redo
	texture := gdk.NewTextureForPixbuf(e.originalBuf)
	e.picture.SetPaintable(texture)

	// Store crop in session
	e.session.SetCrop(&image.CropRegion{
		X:      cropX,
		Y:      cropY,
		Width:  cropW,
		Height: cropH,
	})

	// Push to undo stack
	e.pushUndo(image.EditAction{
		Type:        "crop",
		Description: "Crop image",
	})

	// Reset crop selection
	e.cancelCrop()
}

// Undo reverses the last edit action
func (e *EditorView) Undo() {
	if len(e.undoStack) == 0 {
		return
	}
	action := e.undoStack[len(e.undoStack)-1]
	e.undoStack = e.undoStack[:len(e.undoStack)-1]
	e.redoStack = append(e.redoStack, action)

	// Apply undo based on action type
	switch action.Type {
	case "stroke":
		// Remove last stroke from session
		if len(e.session.Strokes) > 0 {
			e.session.Strokes = e.session.Strokes[:len(e.session.Strokes)-1]
		}
	case "erase":
		// Restore erased stroke
		if stroke, ok := action.Data.(image.Stroke); ok {
			e.session.AddStroke(stroke)
		}
	case "crop":
		// Restore original image before crop
		if e.preCropBuf != nil {
			e.originalBuf = e.preCropBuf.Copy()
			texture := gdk.NewTextureForPixbuf(e.originalBuf)
			e.picture.SetPaintable(texture)
			e.preCropBuf = nil
		}
		e.session.ClearCrop()
		e.cropActive = false
	}
	e.drawArea.QueueDraw()
}

// Redo reapplies the last undone action
func (e *EditorView) Redo() {
	if len(e.redoStack) == 0 {
		return
	}
	action := e.redoStack[len(e.redoStack)-1]
	e.redoStack = e.redoStack[:len(e.redoStack)-1]
	e.undoStack = append(e.undoStack, action)

	// Apply redo based on action type
	switch action.Type {
	case "stroke":
		if stroke, ok := action.Data.(image.Stroke); ok {
			e.session.AddStroke(stroke)
		}
	case "erase":
		// Remove the stroke again (redo erase)
		if stroke, ok := action.Data.(image.Stroke); ok {
			// Find and remove this stroke
			for i, s := range e.session.Strokes {
				if len(s.Points) == len(stroke.Points) && s.Color == stroke.Color {
					e.session.Strokes = append(e.session.Strokes[:i], e.session.Strokes[i+1:]...)
					break
				}
			}
		}
	case "crop":
		// Re-apply crop using stored post-crop buffer
		if e.postCropBuf != nil {
			e.preCropBuf = e.originalBuf.Copy() // Store current for undo again
			e.originalBuf = e.postCropBuf.Copy()
			texture := gdk.NewTextureForPixbuf(e.originalBuf)
			e.picture.SetPaintable(texture)
		}
		if crop, ok := action.Data.(*image.CropRegion); ok {
			e.session.SetCrop(crop)
		}
		e.cropActive = true
	}
	e.drawArea.QueueDraw()
}

// pushUndo adds an action to the undo stack
func (e *EditorView) pushUndo(action image.EditAction) {
	e.undoStack = append(e.undoStack, action)
	// Clear redo stack when new action is performed
	e.redoStack = make([]image.EditAction, 0)
}

// LoadImage loads an image for editing
func (e *EditorView) LoadImage(path string, pixbuf *gdkpixbuf.Pixbuf) {
	e.currentPath = path
	e.originalBuf = pixbuf

	// Always start fresh - no session persistence
	e.session = image.NewEditSession(path)

	// Display the image
	texture := gdk.NewTextureForPixbuf(pixbuf)
	e.picture.SetPaintable(texture)

	// Reset drawing state
	e.currentStroke = nil
	e.isDrawing = false
	e.cropActive = e.session.Crop != nil
	e.undoStack = make([]image.EditAction, 0)
	e.redoStack = make([]image.EditAction, 0)

	e.drawArea.QueueDraw()
}

// showSaveDialog shows the save options dialog
func (e *EditorView) showSaveDialog() {
	if e.onSave != nil {
		// For now, call save - we'll implement the dialog in gui.go
		e.onSave(true) // Default to saving as new
	}
}

// GetResultPixbuf returns the final image with all edits applied
// TODO: Composite strokes onto the image (currently only applies crop)
func (e *EditorView) GetResultPixbuf() *gdkpixbuf.Pixbuf {
	if e.originalBuf == nil {
		return nil
	}
	// Return a copy to avoid external modification
	return e.originalBuf.Copy()
}

// GetSession returns the current edit session
func (e *EditorView) GetSession() *image.EditSession {
	return e.session
}

// GetWidget returns the root widget
func (e *EditorView) GetWidget() *gtk.Box {
	return e.widget
}

// ApplyCrop applies the current crop selection to the session
func (e *EditorView) ApplyCrop() {
	if !e.cropActive {
		return
	}

	x1 := min(e.cropStartX, e.cropEndX)
	y1 := min(e.cropStartY, e.cropEndY)
	x2 := max(e.cropStartX, e.cropEndX)
	y2 := max(e.cropStartY, e.cropEndY)

	crop := &image.CropRegion{
		X:      int(x1),
		Y:      int(y1),
		Width:  int(x2 - x1),
		Height: int(y2 - y1),
	}

	e.session.SetCrop(crop)
	e.pushUndo(image.EditAction{
		Type:        "crop",
		Data:        crop,
		Description: "Crop image",
	})
}

// HasUnsavedChanges returns true if there are unsaved edits
func (e *EditorView) HasUnsavedChanges() bool {
	return e.session != nil && e.session.HasEdits()
}

// parseHexColor converts a hex color string to RGB floats (0-1)
func parseHexColor(hex string) (r, g, b float64) {
	if len(hex) < 7 {
		return 0, 0, 0
	}
	var ri, gi, bi int
	_, _ = parseHex(hex[1:3], &ri)
	_, _ = parseHex(hex[3:5], &gi)
	_, _ = parseHex(hex[5:7], &bi)
	return float64(ri) / 255, float64(gi) / 255, float64(bi) / 255
}

// parseHex parses a hex string to an int (simple implementation)
func parseHex(s string, result *int) (int, error) {
	*result = 0
	for _, c := range s {
		*result *= 16
		if c >= '0' && c <= '9' {
			*result += int(c - '0')
		} else if c >= 'a' && c <= 'f' {
			*result += int(c-'a') + 10
		} else if c >= 'A' && c <= 'F' {
			*result += int(c-'A') + 10
		}
	}
	return *result, nil
}
