// Package gui provides Fyne-based user interface components for Frame.
package gui

import (
	"errors"
	"fmt"
	"image"
	"os"
	"path/filepath"
	"strings"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/driver/desktop"
	"fyne.io/fyne/v2/storage"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"

	frameimage "github.com/Hy4ri/frame/internal/image"
	"github.com/Hy4ri/frame/internal/keybindings"
)

// AppController defines the interface for controlling the application from UI.
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

// Window represents the main application window.
type Window struct {
	fyneApp      fyne.App
	fyneWindow   fyne.Window
	viewer       *Viewer
	app          AppController
	isFullscreen bool
	gSequence    bool
	gTimer       *time.Timer
}

// NewWindow creates and configures the main application window.
func NewWindow(appCtrl AppController) *Window {
	w := &Window{
		app: appCtrl,
	}

	w.fyneApp = app.NewWithID("com.github.hy4ri.frame")
	w.fyneApp.Settings().SetTheme(theme.DarkTheme())

	w.fyneWindow = w.fyneApp.NewWindow("Frame")
	w.fyneWindow.Resize(fyne.NewSize(1200, 800))

	// Create the image viewer
	w.viewer = NewViewer()

	// Set up the main layout
	w.fyneWindow.SetContent(w.viewer.Widget())

	// Set up keybindings: TypedKey for special keys, TypedRune for characters
	w.fyneWindow.Canvas().SetOnTypedKey(w.handleSpecialKey)
	w.fyneWindow.Canvas().SetOnTypedRune(w.handleCharKey)
	if dc, ok := w.fyneWindow.Canvas().(desktop.Canvas); ok {
		dc.SetOnKeyDown(w.handleKeyDown)
		dc.SetOnKeyUp(w.handleKeyUp)
	}

	// Reset sticky state when the app loses focus (e.g. alt-tab).
	w.fyneApp.Lifecycle().SetOnExitedForeground(func() {
		w.gSequence = false
		if w.gTimer != nil {
			w.gTimer.Stop()
			if w.gTimer.Reset(time.Second) {
				<-w.gTimer.C
			}
			w.gTimer = nil
		}
		w.viewer.SetCtrlHeld(false)
	})

	return w
}

func (w *Window) handleKeyDown(event *fyne.KeyEvent) {
	switch event.Name {
	case desktop.KeyControlLeft, desktop.KeyControlRight:
		w.viewer.SetCtrlHeld(true)
	}
}

func (w *Window) handleKeyUp(event *fyne.KeyEvent) {
	switch event.Name {
	case desktop.KeyControlLeft, desktop.KeyControlRight:
		w.viewer.SetCtrlHeld(false)
	}
}

// handleSpecialKey handles non-character keys (arrows, Escape, F-keys, Delete).
func (w *Window) handleSpecialKey(event *fyne.KeyEvent) {
	switch event.Name {
	// Navigation (arrow keys)
	case fyne.KeyLeft:
		w.app.PrevImage()
	case fyne.KeyRight:
		w.app.NextImage()
	case fyne.KeyDown:
		w.app.NextImage()
	case fyne.KeyUp:
		w.app.PrevImage()

	// Rename
	case fyne.KeyF2:
		w.app.RenameCurrent()

	// Delete
	case fyne.KeyDelete:
		w.app.DeleteCurrent()

	// Quit
	case fyne.KeyEscape:
		w.app.Quit()

	default:
		w.gSequence = false
	}
}

// handleCharKey handles typed character keybindings (vim-style).
func (w *Window) handleCharKey(r rune) {
	switch r {
	// Navigation
	case 'h':
		w.app.PrevImage()
	case 'l':
		w.app.NextImage()
	case 'j':
		w.app.NextImage()
	case 'k':
		w.app.PrevImage()

	// First/Last image
	case 'g':
		if w.gSequence {
			w.app.FirstImage()
			w.gSequence = false
			if w.gTimer != nil {
				if !w.gTimer.Stop() {
					<-w.gTimer.C
				}
				w.gTimer = nil
			}
		} else {
			w.gSequence = true
			if w.gTimer != nil {
				if !w.gTimer.Stop() {
					<-w.gTimer.C
				}
			}
			w.gTimer = time.AfterFunc(500*time.Millisecond, func() {
				w.gSequence = false
			})
		}
		return // Don't reset gSequence below
	case 'G':
		w.app.LastImage()

	// Fullscreen
	case 'f':
		w.app.ToggleFullscreen()

	// Zoom
	case '+', '=':
		w.app.ZoomIn()
	case '-':
		w.app.ZoomOut()
	case '0':
		w.app.ZoomFit()
	case '1':
		w.app.ZoomOriginal()

	// Rotation
	case 'r':
		w.app.RotateCurrent(true)
	case 'R':
		w.app.RotateCurrent(false)

	// Delete
	case 'd':
		w.app.DeleteCurrent()

	// Info
	case 'i':
		w.app.ShowInfo()

	// Help
	case '?':
		w.app.ShowHelp()

	// Quit
	case 'q':
		w.app.Quit()

	default:
		w.gSequence = false
		if w.gTimer != nil {
			if !w.gTimer.Stop() {
				<-w.gTimer.C
			}
			w.gTimer = nil
		}
		return
	}

	// Reset g sequence on any non-g key
	if r != 'g' {
		w.gSequence = false
		if w.gTimer != nil {
			if !w.gTimer.Stop() {
				<-w.gTimer.C
			}
			w.gTimer = nil
		}
	}
}

// ShowAndRun displays the window and starts the event loop.
func (w *Window) ShowAndRun() {
	w.fyneWindow.ShowAndRun()
}

// Close closes the window and quits the app.
func (w *Window) Close() {
	w.fyneApp.Quit()
}

// LoadImage loads and displays an image file.
func (w *Window) LoadImage(path string) {
	w.viewer.LoadImage(path)
	w.UpdateTitle(path)
}

// PrefetchImage decodes an image into the viewer's cache without displaying it.
func (w *Window) PrefetchImage(path string) {
	w.viewer.PrefetchImage(path)
}

// InvalidateCache removes a path from the image cache (e.g. after delete/rename).
func (w *Window) InvalidateCache(path string) {
	w.viewer.cache.Invalidate(path)
}

// UpdateTitle updates the window title with the current image info.
func (w *Window) UpdateTitle(path string) {
	name := filepath.Base(path)
	count := w.app.GetImageCount()
	index := w.app.GetCurrentIndex()
	title := fmt.Sprintf("%s (%d/%d) - Frame", name, index, count)
	w.fyneWindow.SetTitle(title)
}

// ClearImage clears the current image display.
func (w *Window) ClearImage() {
	w.viewer.Clear()
	w.fyneWindow.SetTitle("Frame")
}

// ToggleFullscreen toggles fullscreen mode.
func (w *Window) ToggleFullscreen() {
	w.isFullscreen = !w.isFullscreen
	w.fyneWindow.SetFullScreen(w.isFullscreen)
}

// Zoom operations delegate to viewer.
func (w *Window) ZoomIn()       { w.viewer.ZoomIn() }
func (w *Window) ZoomOut()      { w.viewer.ZoomOut() }
func (w *Window) ZoomFit()      { w.viewer.ZoomFit() }
func (w *Window) ZoomOriginal() { w.viewer.ZoomOriginal() }

// RotateImage rotates the current image.
func (w *Window) RotateImage(clockwise bool) {
	w.viewer.Rotate(clockwise)
}

// ShowFileChooser opens a file chooser dialog.
func (w *Window) ShowFileChooser() {
	fd := dialog.NewFileOpen(func(reader fyne.URIReadCloser, err error) {
		if err != nil || reader == nil {
			return
		}
		defer reader.Close()

		uri := reader.URI()
		path := uri.Path()
		if path != "" {
			w.app.OpenPath(path)
		}
	}, w.fyneWindow)

	// Set image filter
	fd.SetFilter(storage.NewExtensionFileFilter([]string{
		".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".tiff", ".tif", ".ico", ".apng",
	}))

	fd.Show()
}

// ShowDeleteConfirmation shows a confirmation dialog before deletion.
func (w *Window) ShowDeleteConfirmation(path string, callback func(bool)) {
	name := filepath.Base(path)
	content := fmt.Sprintf("Move \"%s\" to trash?", name)

	d := dialog.NewConfirm("Delete Image", content, func(confirmed bool) {
		callback(confirmed)
	}, w.fyneWindow)
	d.SetConfirmText("Move to Trash")
	d.SetDismissText("Cancel")
	d.Show()
}

// ShowRenameDialog shows a dialog to rename the current image.
func (w *Window) ShowRenameDialog(path string, callback func(string)) {
	currentName := filepath.Base(path)
	ext := filepath.Ext(currentName)
	nameWithoutExt := currentName[:len(currentName)-len(ext)]

	entry := widget.NewEntry()
	entry.SetText(nameWithoutExt)

	items := []*widget.FormItem{
		widget.NewFormItem("New name", entry),
	}

	d := dialog.NewForm("Rename Image", "Rename", "Cancel", items, func(confirmed bool) {
		if confirmed {
			newName := strings.TrimSpace(entry.Text)
			// Strip any trailing extension the user may have typed so
			// "image.jpg" doesn't become "image.jpg.jpg".
			if e := filepath.Ext(newName); e != "" {
				newName = strings.TrimSuffix(newName, e)
			}
			callback(newName + ext)
		} else {
			callback("")
		}
	}, w.fyneWindow)
	d.Show()
}

// ShowInfoDialog displays image information.
func (w *Window) ShowInfoDialog(info *frameimage.Info) {
	dims := fmt.Sprintf("%dx%d", info.Width, info.Height)

	content := widget.NewForm(
		widget.NewFormItem("File", widget.NewLabel(info.Name)),
		widget.NewFormItem("Size", widget.NewLabel(info.FileSize)),
		widget.NewFormItem("Dimensions", widget.NewLabel(dims)),
		widget.NewFormItem("Format", widget.NewLabel(info.Format)),
		widget.NewFormItem("Modified", widget.NewLabel(info.Modified)),
	)

	if info.ExifData != "" {
		exifLabel := widget.NewLabel(info.ExifData)
		exifLabel.Wrapping = fyne.TextWrapWord
		content.Append("EXIF", exifLabel)
	}

	d := dialog.NewCustom("Image Information", "OK", content, w.fyneWindow)
	d.Resize(fyne.NewSize(450, 350))
	d.Show()
}

// ShowHelpDialog displays keybindings help.
func (w *Window) ShowHelpDialog() {
	helpText := keybindings.GetHelpText()

	label := widget.NewLabel(helpText)
	label.TextStyle = fyne.TextStyle{Monospace: true}

	scroll := container.NewVScroll(label)
	scroll.SetMinSize(fyne.NewSize(380, 400))

	d := dialog.NewCustom("Keyboard Shortcuts", "OK", scroll, w.fyneWindow)
	d.Resize(fyne.NewSize(420, 480))
	d.Show()
}

// ShowError displays an error message.
func (w *Window) ShowError(message string) {
	dialog.ShowError(errors.New(message), w.fyneWindow)
}

// LoadImageFromFile loads an image.Image from a file path, supporting all
// registered decoders (JPEG, PNG, GIF, WebP, BMP, TIFF via blank imports).
func LoadImageFromFile(path string) (image.Image, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	img, _, err := image.Decode(f)
	if err != nil {
		return nil, err
	}
	return img, nil
}
