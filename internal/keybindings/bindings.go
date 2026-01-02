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
