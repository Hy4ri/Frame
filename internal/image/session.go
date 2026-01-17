// Package image provides image loading, operations, and metadata extraction.
package image

// EditSession represents a non-destructive editing session stored as JSON.
// This allows edits to be saved and reloaded without modifying the original image.
type EditSession struct {
	Version    string      `json:"version"`
	ImagePath  string      `json:"image_path"`
	Crop       *CropRegion `json:"crop,omitempty"`
	Strokes    []Stroke    `json:"strokes,omitempty"`
	HistoryPos int         `json:"history_pos"` // Current position in undo stack
}

// CropRegion defines the cropping area
type CropRegion struct {
	X      int    `json:"x"`
	Y      int    `json:"y"`
	Width  int    `json:"width"`
	Height int    `json:"height"`
	Ratio  string `json:"ratio,omitempty"` // "free", "1:1", "4:3", "16:9"
}

// Stroke represents a single pen/eraser stroke
type Stroke struct {
	Tool      string  `json:"tool"`       // "pen" or "eraser"
	Color     string  `json:"color"`      // Hex color, e.g., "#FF0000"
	BrushSize float64 `json:"brush_size"` // Brush diameter in pixels
	Points    []Point `json:"points"`     // List of points in the stroke
}

// Point represents a coordinate in a stroke
type Point struct {
	X float64 `json:"x"`
	Y float64 `json:"y"`
}

// EditAction represents an undoable action for the history stack
type EditAction struct {
	Type        string      `json:"type"` // "crop", "stroke", "clear"
	Data        interface{} `json:"data"` // The action-specific data
	Description string      `json:"description"`
}

// NewEditSession creates a new empty edit session for an image
func NewEditSession(imagePath string) *EditSession {
	return &EditSession{
		Version:    "1.0",
		ImagePath:  imagePath,
		Strokes:    make([]Stroke, 0),
		HistoryPos: 0,
	}
}

// AddStroke adds a stroke to the session
func (s *EditSession) AddStroke(stroke Stroke) {
	s.Strokes = append(s.Strokes, stroke)
	s.HistoryPos = len(s.Strokes)
}

// SetCrop sets the crop region
func (s *EditSession) SetCrop(crop *CropRegion) {
	s.Crop = crop
}

// ClearCrop removes the crop region
func (s *EditSession) ClearCrop() {
	s.Crop = nil
}

// HasEdits returns true if the session has any edits
func (s *EditSession) HasEdits() bool {
	return s.Crop != nil || len(s.Strokes) > 0
}

// Clear removes all edits from the session
func (s *EditSession) Clear() {
	s.Crop = nil
	s.Strokes = make([]Stroke, 0)
	s.HistoryPos = 0
}
