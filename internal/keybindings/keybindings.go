// Package keybindings defines vim-style keyboard shortcuts for Frame.
package keybindings

// GetHelpText returns the formatted help text for keybindings.
func GetHelpText() string {
	return `NAVIGATION
  h / ←      Previous image
  l / →      Next image
  j / ↓      Next image
  k / ↑      Previous image
  gg         First image
  G          Last image

VIEW
  f          Toggle fullscreen
  + / =      Zoom in
  -          Zoom out
  Scroll ↑   Zoom in
  Scroll ↓   Zoom out
  0          Fit to window
  1          Original size (1:1)

IMAGE OPERATIONS
  r          Rotate clockwise 90°
  R          Rotate counter-clockwise 90°
  d / Del    Delete image (to trash)
  F2         Rename image
  i          Show image info

GENERAL
  ?          Show this help
  q / Esc    Quit`
}
