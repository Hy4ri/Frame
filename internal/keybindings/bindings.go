// Package keybindings defines vim-style keyboard shortcuts for Frame.
package keybindings

// GetHelpText returns the formatted help text for keybindings
func GetHelpText() string {
	return `<b>Frame - Keyboard Shortcuts</b>

<b>Navigation</b>
  <tt>h</tt> / <tt>←</tt>      Previous image
  <tt>l</tt> / <tt>→</tt>      Next image
  <tt>j</tt> / <tt>↓</tt>      Next image
  <tt>k</tt> / <tt>↑</tt>      Previous image
  <tt>gg</tt>          First image
  <tt>G</tt>           Last image

<b>View</b>
  <tt>f</tt>           Toggle fullscreen
  <tt>+</tt> / <tt>=</tt>      Zoom in
  <tt>-</tt>           Zoom out
  <tt>0</tt>           Fit to window
  <tt>1</tt>           Original size (1:1)

<b>Image Operations</b>
  <tt>r</tt>           Rotate clockwise 90°
  <tt>R</tt>           Rotate counter-clockwise 90°
  <tt>d</tt> / <tt>Del</tt>    Delete image (to trash)
  <tt>F2</tt>          Rename image
  <tt>i</tt>           Show image info

<b>General</b>
  <tt>?</tt>           Show this help
  <tt>q</tt> / <tt>Esc</tt>    Quit`
}

// Binding represents a single keybinding
type Binding struct {
	Key         string
	Description string
	Action      string
}

// AllBindings returns all keybindings as a structured list
func AllBindings() []Binding {
	return []Binding{
		{"h / ←", "Previous image", "prev"},
		{"l / →", "Next image", "next"},
		{"j / ↓", "Next image", "next"},
		{"k / ↑", "Previous image", "prev"},
		{"gg", "First image", "first"},
		{"G", "Last image", "last"},
		{"f", "Toggle fullscreen", "fullscreen"},
		{"+/=", "Zoom in", "zoom_in"},
		{"-", "Zoom out", "zoom_out"},
		{"0", "Fit to window", "zoom_fit"},
		{"1", "Original size", "zoom_original"},
		{"r", "Rotate clockwise", "rotate_cw"},
		{"R", "Rotate counter-clockwise", "rotate_ccw"},
		{"d / Del", "Delete image", "delete"},
		{"F2", "Rename image", "rename"},
		{"i", "Show image info", "info"},
		{"?", "Show help", "help"},
		{"q / Esc", "Quit", "quit"},
	}
}
