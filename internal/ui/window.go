// Package ui provides GTK4 user interface components for Frame.
package ui

import (
	"fmt"
	"path/filepath"

	"github.com/Hy4ri/frame/internal/image"
	"github.com/Hy4ri/frame/internal/keybindings"
	"github.com/diamondburned/gotk4/pkg/gdk/v4"
	"github.com/diamondburned/gotk4/pkg/gtk/v4"
)

// AppController defines the interface for controlling the application from UI
type AppController interface {
	NextImage()
	PrevImage()
	FirstImage()
	LastImage()
	DeleteCurrent()
	RotateCurrent(clockwise bool)
	RenameCurrent()
	ShowInfo()
	ShowHelp()
	ToggleFullscreen()
	ZoomIn()
	ZoomOut()
	ZoomFit()
	ZoomOriginal()
	Quit()
	GetCurrentPath() string
	GetImageCount() int
	GetCurrentIndex() int
	OpenPath(path string)
}

// Window represents the main application window
type Window struct {
	window       *gtk.ApplicationWindow
	headerBar    *gtk.HeaderBar
	viewer       *Viewer
	app          AppController
	isFullscreen bool
	gSequence    bool // Track if 'g' was pressed for 'gg' sequence
}

// NewWindow creates and configures the main application window
func NewWindow(gtkApp *gtk.Application, app AppController) *Window {
	w := &Window{
		app: app,
	}

	// Create the main window
	w.window = gtk.NewApplicationWindow(gtkApp)
	w.window.SetTitle("Frame")
	w.window.SetDefaultSize(1200, 800)

	// Create header bar with minimal controls
	w.headerBar = gtk.NewHeaderBar()
	w.headerBar.SetShowTitleButtons(true)

	// Help button
	helpBtn := gtk.NewButtonFromIconName("help-about-symbolic")
	helpBtn.SetTooltipText("Help (press ?)")
	helpBtn.ConnectClicked(func() {
		w.ShowHelpDialog()
	})
	w.headerBar.PackEnd(helpBtn)

	// Open file button
	openBtn := gtk.NewButtonFromIconName("document-open-symbolic")
	openBtn.SetTooltipText("Open image or folder")
	openBtn.ConnectClicked(func() {
		w.ShowFileChooser()
	})
	w.headerBar.PackStart(openBtn)

	w.window.SetTitlebar(w.headerBar)

	// Create the image viewer
	w.viewer = NewViewer()

	// Set up the main layout
	w.window.SetChild(w.viewer.widget)

	// Set up keybindings
	w.setupKeybindings()

	// Apply dark theme
	w.applyStyles()

	return w
}

// setupKeybindings configures vim-style keyboard shortcuts
func (w *Window) setupKeybindings() {
	controller := gtk.NewEventControllerKey()

	controller.ConnectKeyPressed(func(keyval, keycode uint, state gdk.ModifierType) bool {
		// Check for shift modifier
		shift := state&gdk.ShiftMask != 0

		switch keyval {
		// Navigation
		case gdk.KEY_h, gdk.KEY_Left:
			w.app.PrevImage()
			return true
		case gdk.KEY_l, gdk.KEY_Right:
			w.app.NextImage()
			return true
		case gdk.KEY_j, gdk.KEY_Down:
			w.app.NextImage()
			return true
		case gdk.KEY_k, gdk.KEY_Up:
			w.app.PrevImage()
			return true

		// First/Last image
		case gdk.KEY_g:
			if w.gSequence {
				// 'gg' - go to first
				w.app.FirstImage()
				w.gSequence = false
			} else {
				w.gSequence = true
				// Reset after a delay (handled via state)
			}
			return true
		case gdk.KEY_G:
			w.app.LastImage()
			w.gSequence = false
			return true

		// Fullscreen
		case gdk.KEY_f:
			w.app.ToggleFullscreen()
			return true

		// Zoom
		case gdk.KEY_plus, gdk.KEY_equal:
			w.app.ZoomIn()
			return true
		case gdk.KEY_minus:
			w.app.ZoomOut()
			return true
		case gdk.KEY_0:
			w.app.ZoomFit()
			return true
		case gdk.KEY_1:
			w.app.ZoomOriginal()
			return true

		// Rotation
		case gdk.KEY_r:
			if shift {
				w.app.RotateCurrent(false) // Counter-clockwise
			} else {
				w.app.RotateCurrent(true) // Clockwise
			}
			return true
		case gdk.KEY_R:
			w.app.RotateCurrent(false)
			return true

		// Delete
		case gdk.KEY_d, gdk.KEY_Delete:
			w.app.DeleteCurrent()
			return true

		// Info
		case gdk.KEY_i:
			w.app.ShowInfo()
			return true

		// Help
		case gdk.KEY_question:
			w.app.ShowHelp()
			return true

		// Rename
		case gdk.KEY_F2:
			w.app.RenameCurrent()
			return true

		// Quit
		case gdk.KEY_q, gdk.KEY_Escape:
			w.app.Quit()
			return true

		default:
			// Reset g sequence on any other key
			w.gSequence = false
		}

		return false
	})

	w.window.AddController(controller)
}

// applyStyles applies dark theme styling
func (w *Window) applyStyles() {
	// Request dark theme
	settings := gtk.SettingsGetDefault()
	if settings != nil {
		settings.SetObjectProperty("gtk-application-prefer-dark-theme", true)
	}

	// Load custom CSS
	provider := gtk.NewCSSProvider()
	provider.LoadFromData(StylesCSS)

	display := gdk.DisplayGetDefault()
	if display != nil {
		gtk.StyleContextAddProviderForDisplay(display, provider, gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
	}
}

// Show displays the window
func (w *Window) Show() {
	w.window.Show()
}

// LoadImage loads and displays an image file
func (w *Window) LoadImage(path string) {
	w.viewer.LoadImage(path)
	w.UpdateTitle(path)
}

// UpdateTitle updates the window title with the current image info
func (w *Window) UpdateTitle(path string) {
	name := filepath.Base(path)
	count := w.app.GetImageCount()
	index := w.app.GetCurrentIndex()
	title := fmt.Sprintf("%s (%d/%d) - Frame", name, index, count)
	w.window.SetTitle(title)
}

// ClearImage clears the current image display
func (w *Window) ClearImage() {
	w.viewer.Clear()
	w.window.SetTitle("Frame")
}

// ToggleFullscreen toggles fullscreen mode
func (w *Window) ToggleFullscreen() {
	if w.isFullscreen {
		w.window.Unfullscreen()
		w.headerBar.SetVisible(true)
	} else {
		w.window.Fullscreen()
		w.headerBar.SetVisible(false)
	}
	w.isFullscreen = !w.isFullscreen
}

// Zoom operations delegate to viewer
func (w *Window) ZoomIn()       { w.viewer.ZoomIn() }
func (w *Window) ZoomOut()      { w.viewer.ZoomOut() }
func (w *Window) ZoomFit()      { w.viewer.ZoomFit() }
func (w *Window) ZoomOriginal() { w.viewer.ZoomOriginal() }

// RotateImage rotates the current image
func (w *Window) RotateImage(clockwise bool) {
	w.viewer.Rotate(clockwise)
}

// ShowFileChooser opens a file chooser dialog
func (w *Window) ShowFileChooser() {
	dialog := gtk.NewFileChooserNative(
		"Open Image",
		&w.window.Window,
		gtk.FileChooserActionOpen,
		"Open",
		"Cancel",
	)

	// Add image filter
	filter := gtk.NewFileFilter()
	filter.SetName("Images")
	filter.AddMIMEType("image/*")
	dialog.AddFilter(filter)

	dialog.ConnectResponse(func(response int) {
		if response == int(gtk.ResponseAccept) {
			file := dialog.File()
			if file != nil {
				path := file.Path()
				if path != "" {
					w.app.OpenPath(path)
				}
			}
		}
	})

	dialog.Show()
}

// ShowDeleteConfirmation shows a confirmation dialog before deletion
func (w *Window) ShowDeleteConfirmation(path string, callback func(bool)) {
	dialog := gtk.NewMessageDialog(
		&w.window.Window,
		gtk.DialogModal|gtk.DialogDestroyWithParent,
		gtk.MessageWarning,
		gtk.ButtonsNone,
	)
	dialog.SetMarkup(fmt.Sprintf("<b>Delete image?</b>\n\n%s", filepath.Base(path)))
	dialog.AddButton("Cancel", int(gtk.ResponseCancel))
	dialog.AddButton("Move to Trash", int(gtk.ResponseAccept))

	dialog.ConnectResponse(func(response int) {
		callback(response == int(gtk.ResponseAccept))
		dialog.Destroy()
	})

	dialog.Show()
}

// ShowRenameDialog shows a dialog to rename the current image
func (w *Window) ShowRenameDialog(path string, callback func(string)) {
	dialog := gtk.NewDialog()
	dialog.SetTitle("Rename Image")
	dialog.SetTransientFor(&w.window.Window)
	dialog.SetModal(true)
	dialog.SetDefaultSize(400, -1)

	// Content area
	content := dialog.ContentArea()
	content.SetMarginTop(12)
	content.SetMarginBottom(12)
	content.SetMarginStart(12)
	content.SetMarginEnd(12)
	content.SetSpacing(12)

	label := gtk.NewLabel("New name:")
	label.SetHAlign(gtk.AlignStart)
	content.Append(label)

	entry := gtk.NewEntry()
	currentName := filepath.Base(path)
	ext := filepath.Ext(currentName)
	nameWithoutExt := currentName[:len(currentName)-len(ext)]
	entry.SetText(nameWithoutExt)
	entry.SetActivatesDefault(true)
	content.Append(entry)

	// Buttons
	dialog.AddButton("Cancel", int(gtk.ResponseCancel))
	dialog.AddButton("Rename", int(gtk.ResponseAccept))
	dialog.SetDefaultResponse(int(gtk.ResponseAccept))

	dialog.ConnectResponse(func(response int) {
		if response == int(gtk.ResponseAccept) {
			newName := entry.Text() + ext
			callback(newName)
		} else {
			callback("")
		}
		dialog.Destroy()
	})

	dialog.Show()
}

// ShowInfoDialog displays image information
func (w *Window) ShowInfoDialog(info *image.Info) {
	dialog := gtk.NewMessageDialog(
		&w.window.Window,
		gtk.DialogModal|gtk.DialogDestroyWithParent,
		gtk.MessageInfo,
		gtk.ButtonsOK,
	)

	markup := fmt.Sprintf(`<b>Image Information</b>

<b>File:</b> %s
<b>Size:</b> %s
<b>Dimensions:</b> %dx%d
<b>Format:</b> %s
<b>Modified:</b> %s`,
		info.Name,
		info.FileSize,
		info.Width, info.Height,
		info.Format,
		info.Modified,
	)

	if info.ExifData != "" {
		markup += fmt.Sprintf("\n\n<b>EXIF:</b>\n%s", info.ExifData)
	}

	dialog.SetMarkup(markup)

	dialog.ConnectResponse(func(response int) {
		dialog.Destroy()
	})

	dialog.Show()
}

// ShowHelpDialog displays keybindings help
func (w *Window) ShowHelpDialog() {
	dialog := gtk.NewMessageDialog(
		&w.window.Window,
		gtk.DialogModal|gtk.DialogDestroyWithParent,
		gtk.MessageInfo,
		gtk.ButtonsOK,
	)

	dialog.SetMarkup(keybindings.GetHelpText())

	dialog.ConnectResponse(func(response int) {
		dialog.Destroy()
	})

	dialog.Show()
}

// ShowError displays an error message
func (w *Window) ShowError(message string) {
	dialog := gtk.NewMessageDialog(
		&w.window.Window,
		gtk.DialogModal|gtk.DialogDestroyWithParent,
		gtk.MessageError,
		gtk.ButtonsOK,
	)
	dialog.SetMarkup(fmt.Sprintf("<b>Error</b>\n\n%s", message))

	dialog.ConnectResponse(func(response int) {
		dialog.Destroy()
	})

	dialog.Show()
}
