// Package gui provides GTK4 user interface components for Frame.
package gui

import (
	"context"
	"fmt"
	"path/filepath"

	"github.com/Hy4ri/frame/internal/image"
	"github.com/Hy4ri/frame/internal/keybindings"
	"github.com/diamondburned/gotk4/pkg/gdk/v4"
	"github.com/diamondburned/gotk4/pkg/gio/v2"
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
	stack        *gtk.Stack
	viewer       *Viewer
	editor       *EditorView
	app          AppController
	isFullscreen bool
	isEditMode   bool
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
	w.headerBar.SetShowTitleButtons(false)

	// Help button
	helpBtn := gtk.NewButtonFromIconName("help-about-symbolic")
	helpBtn.SetTooltipText("Help (press ?)")
	helpBtn.ConnectClicked(func() {
		w.ShowHelpDialog()
	})
	w.headerBar.PackEnd(helpBtn)

	// Edit button
	editBtn := gtk.NewButtonFromIconName("document-edit-symbolic")
	editBtn.SetTooltipText("Edit image (e)")
	editBtn.ConnectClicked(func() {
		w.EnterEditMode()
	})
	w.headerBar.PackEnd(editBtn)

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

	// Create the editor
	w.editor = NewEditorView(w.handleEditorSave, w.ExitEditMode)

	// Create stack for switching between viewer and editor
	w.stack = gtk.NewStack()
	w.stack.SetTransitionType(gtk.StackTransitionTypeCrossfade)
	w.stack.SetTransitionDuration(200)
	w.stack.AddNamed(w.viewer.widget, "viewer")
	w.stack.AddNamed(w.editor.GetWidget(), "editor")
	w.stack.SetVisibleChildName("viewer")

	// Set up the main layout
	w.window.SetChild(w.stack)

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
		// Check modifiers
		shift := state&gdk.ShiftMask != 0
		ctrl := state&gdk.ControlMask != 0

		// Handle edit mode keybindings
		if w.isEditMode {
			return w.handleEditModeKeys(keyval, ctrl)
		}

		// View mode keybindings
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

		// Edit mode
		case gdk.KEY_e:
			w.EnterEditMode()
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

// handleEditModeKeys handles keybindings when in edit mode
func (w *Window) handleEditModeKeys(keyval uint, ctrl bool) bool {
	switch keyval {
	// Exit edit mode
	case gdk.KEY_Escape:
		w.ExitEditMode()
		return true

	// Tool selection
	case gdk.KEY_c:
		w.editor.setTool(ToolCrop)
		return true
	case gdk.KEY_p:
		w.editor.setTool(ToolPen)
		return true

	// Undo/Redo
	case gdk.KEY_z:
		if ctrl {
			w.editor.Undo()
			return true
		}
	case gdk.KEY_y:
		if ctrl {
			w.editor.Redo()
			return true
		}
	case gdk.KEY_Z: // Ctrl+Shift+Z for redo
		if ctrl {
			w.editor.Redo()
			return true
		}

	// Save
	case gdk.KEY_s:
		if ctrl {
			w.ShowSaveDialog(true)
			return true
		}
	}

	return false
}

// applyStyles applies minimal custom styling while respecting user's system theme
func (w *Window) applyStyles() {
	// Load minimal CSS (only styles the image viewport with a dark background)
	provider := gtk.NewCSSProvider()
	provider.LoadFromString(StylesCSS)

	display := gdk.DisplayGetDefault()
	if display != nil {
		gtk.StyleContextAddProviderForDisplay(display, provider, gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
	}
}

// Show displays the window
func (w *Window) Show() {
	w.window.SetVisible(true)
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

// EnterEditMode switches to the editor view
func (w *Window) EnterEditMode() {
	path := w.app.GetCurrentPath()
	if path == "" {
		return
	}

	// Load the image into editor
	pixbuf := w.viewer.GetPixbuf()
	if pixbuf == nil {
		return
	}

	w.editor.LoadImage(path, pixbuf)
	w.stack.SetVisibleChildName("editor")
	w.isEditMode = true
	w.window.SetTitle("Editing - " + filepath.Base(path))
}

// ExitEditMode returns to the viewer
func (w *Window) ExitEditMode() {
	if w.editor.HasUnsavedChanges() {
		w.showDiscardConfirmation(func(discard bool) {
			if discard {
				w.doExitEditMode()
			}
		})
	} else {
		w.doExitEditMode()
	}
}

// doExitEditMode performs the actual exit from edit mode
func (w *Window) doExitEditMode() {
	w.stack.SetVisibleChildName("viewer")
	w.isEditMode = false
	w.UpdateTitle(w.app.GetCurrentPath())
}

// handleEditorSave handles saving from the editor
func (w *Window) handleEditorSave(asNew bool) {
	w.ShowSaveDialog(asNew)
}

// ShowSaveDialog shows the save options dialog
func (w *Window) ShowSaveDialog(defaultAsNew bool) {
	dialog := gtk.NewWindow()
	dialog.SetTitle("Save Edits")
	dialog.SetTransientFor(&w.window.Window)
	dialog.SetModal(true)
	dialog.SetDefaultSize(400, -1)
	dialog.SetDestroyWithParent(true)

	mainBox := gtk.NewBox(gtk.OrientationVertical, 16)
	mainBox.SetMarginTop(24)
	mainBox.SetMarginBottom(24)
	mainBox.SetMarginStart(24)
	mainBox.SetMarginEnd(24)

	titleLabel := gtk.NewLabel("Save Changes")
	titleLabel.AddCSSClass("title-2")
	mainBox.Append(titleLabel)

	descLabel := gtk.NewLabel("How would you like to save your edits?")
	mainBox.Append(descLabel)

	buttonBox := gtk.NewBox(gtk.OrientationVertical, 8)
	buttonBox.SetMarginTop(12)

	// Save as new file button
	newBtn := gtk.NewButtonWithLabel("Save as New Image")
	newBtn.AddCSSClass("suggested-action")
	newBtn.ConnectClicked(func() {
		dialog.Close()
		w.saveEdits(true)
	})
	buttonBox.Append(newBtn)

	// Apply to original button
	origBtn := gtk.NewButtonWithLabel("Apply to Original")
	origBtn.AddCSSClass("destructive-action")
	origBtn.ConnectClicked(func() {
		dialog.Close()
		w.saveEdits(false)
	})
	buttonBox.Append(origBtn)

	// Cancel button
	cancelBtn := gtk.NewButtonWithLabel("Cancel")
	cancelBtn.SetMarginTop(8)
	cancelBtn.ConnectClicked(func() {
		dialog.Close()
	})
	buttonBox.Append(cancelBtn)

	mainBox.Append(buttonBox)
	dialog.SetChild(mainBox)
	dialog.SetVisible(true)
}

// saveEdits saves the current edits
func (w *Window) saveEdits(asNew bool) {
	session := w.editor.GetSession()
	if session == nil {
		return
	}

	// Save the session file (non-destructive)
	if err := image.SaveEditSession(session); err != nil {
		w.ShowError("Failed to save edits: " + err.Error())
		return
	}

	// TODO: Implement actual image compositing and saving
	// For now, just save the session and exit edit mode
	w.doExitEditMode()
}

// showDiscardConfirmation asks user to confirm discarding changes
func (w *Window) showDiscardConfirmation(callback func(bool)) {
	dialog := gtk.NewWindow()
	dialog.SetTitle("Discard Changes?")
	dialog.SetTransientFor(&w.window.Window)
	dialog.SetModal(true)
	dialog.SetDefaultSize(350, -1)
	dialog.SetDestroyWithParent(true)

	mainBox := gtk.NewBox(gtk.OrientationVertical, 16)
	mainBox.SetMarginTop(24)
	mainBox.SetMarginBottom(24)
	mainBox.SetMarginStart(24)
	mainBox.SetMarginEnd(24)

	titleLabel := gtk.NewLabel("Discard unsaved changes?")
	titleLabel.AddCSSClass("title-3")
	mainBox.Append(titleLabel)

	buttonBox := gtk.NewBox(gtk.OrientationHorizontal, 8)
	buttonBox.SetHAlign(gtk.AlignCenter)
	buttonBox.SetMarginTop(12)

	cancelBtn := gtk.NewButtonWithLabel("Keep Editing")
	cancelBtn.ConnectClicked(func() {
		callback(false)
		dialog.Close()
	})
	buttonBox.Append(cancelBtn)

	discardBtn := gtk.NewButtonWithLabel("Discard")
	discardBtn.AddCSSClass("destructive-action")
	discardBtn.ConnectClicked(func() {
		callback(true)
		dialog.Close()
	})
	buttonBox.Append(discardBtn)

	mainBox.Append(buttonBox)
	dialog.SetChild(mainBox)
	dialog.SetVisible(true)
}

// IsEditMode returns whether the window is in edit mode
func (w *Window) IsEditMode() bool {
	return w.isEditMode
}

// ShowFileChooser opens a file chooser dialog using the modern FileDialog API
func (w *Window) ShowFileChooser() {
	dialog := gtk.NewFileDialog()
	dialog.SetTitle("Open Image")

	// Add image filter
	filters := gio.NewListStore(gtk.GTypeFileFilter)
	imageFilter := gtk.NewFileFilter()
	imageFilter.SetName("Images")
	imageFilter.AddMIMEType("image/*")
	filters.Append(imageFilter.Object)
	dialog.SetFilters(filters)
	dialog.SetDefaultFilter(imageFilter)

	dialog.Open(context.Background(), &w.window.Window, func(result gio.AsyncResulter) {
		file, err := dialog.OpenFinish(result)
		if err != nil {
			return // User cancelled or error occurred
		}
		if file != nil {
			path := file.Path()
			if path != "" {
				w.app.OpenPath(path)
			}
		}
	})
}

// ShowDeleteConfirmation shows a confirmation dialog before deletion
func (w *Window) ShowDeleteConfirmation(path string, callback func(bool)) {
	// Create a custom dialog window for delete confirmation
	dialog := gtk.NewWindow()
	dialog.SetTitle("Confirm Delete")
	dialog.SetTransientFor(&w.window.Window)
	dialog.SetModal(true)
	dialog.SetDefaultSize(400, -1)
	dialog.SetDestroyWithParent(true)

	// Main container
	mainBox := gtk.NewBox(gtk.OrientationVertical, 16)
	mainBox.SetMarginTop(24)
	mainBox.SetMarginBottom(24)
	mainBox.SetMarginStart(24)
	mainBox.SetMarginEnd(24)

	// Warning icon and message
	headerBox := gtk.NewBox(gtk.OrientationHorizontal, 12)
	headerBox.SetHAlign(gtk.AlignCenter)

	icon := gtk.NewImageFromIconName("dialog-warning-symbolic")
	icon.SetIconSize(gtk.IconSizeLarge)
	headerBox.Append(icon)

	titleLabel := gtk.NewLabel("Delete image?")
	titleLabel.AddCSSClass("title-2")
	headerBox.Append(titleLabel)

	mainBox.Append(headerBox)

	// File name
	fileLabel := gtk.NewLabel(filepath.Base(path))
	fileLabel.AddCSSClass("dim-label")
	mainBox.Append(fileLabel)

	// Detail text
	detailLabel := gtk.NewLabel("This will move the image to the trash.")
	mainBox.Append(detailLabel)

	// Button box
	buttonBox := gtk.NewBox(gtk.OrientationHorizontal, 8)
	buttonBox.SetHAlign(gtk.AlignCenter)
	buttonBox.SetMarginTop(12)

	cancelBtn := gtk.NewButtonWithLabel("Cancel")
	cancelBtn.ConnectClicked(func() {
		callback(false)
		dialog.Close()
	})
	buttonBox.Append(cancelBtn)

	deleteBtn := gtk.NewButtonWithLabel("Move to Trash")
	deleteBtn.AddCSSClass("destructive-action")
	deleteBtn.ConnectClicked(func() {
		callback(true)
		dialog.Close()
	})
	buttonBox.Append(deleteBtn)

	mainBox.Append(buttonBox)
	dialog.SetChild(mainBox)

	// Handle Escape key
	controller := gtk.NewEventControllerKey()
	controller.ConnectKeyPressed(func(keyval, keycode uint, state gdk.ModifierType) bool {
		if keyval == gdk.KEY_Escape {
			callback(false)
			dialog.Close()
			return true
		}
		return false
	})
	dialog.AddController(controller)

	dialog.SetVisible(true)
}

// ShowRenameDialog shows a dialog to rename the current image
func (w *Window) ShowRenameDialog(path string, callback func(string)) {
	// Create a custom dialog window
	dialog := gtk.NewWindow()
	dialog.SetTitle("Rename Image")
	dialog.SetTransientFor(&w.window.Window)
	dialog.SetModal(true)
	dialog.SetDefaultSize(400, -1)
	dialog.SetDestroyWithParent(true)

	// Main container
	mainBox := gtk.NewBox(gtk.OrientationVertical, 12)
	mainBox.SetMarginTop(20)
	mainBox.SetMarginBottom(20)
	mainBox.SetMarginStart(20)
	mainBox.SetMarginEnd(20)

	// Label
	label := gtk.NewLabel("New name:")
	label.SetHAlign(gtk.AlignStart)
	mainBox.Append(label)

	// Entry
	entry := gtk.NewEntry()
	currentName := filepath.Base(path)
	ext := filepath.Ext(currentName)
	nameWithoutExt := currentName[:len(currentName)-len(ext)]
	entry.SetText(nameWithoutExt)
	mainBox.Append(entry)

	// Button box
	buttonBox := gtk.NewBox(gtk.OrientationHorizontal, 8)
	buttonBox.SetHAlign(gtk.AlignEnd)
	buttonBox.SetMarginTop(12)

	cancelBtn := gtk.NewButtonWithLabel("Cancel")
	cancelBtn.ConnectClicked(func() {
		callback("")
		dialog.Close()
	})
	buttonBox.Append(cancelBtn)

	renameBtn := gtk.NewButtonWithLabel("Rename")
	renameBtn.AddCSSClass("suggested-action")
	renameBtn.ConnectClicked(func() {
		newName := entry.Text() + ext
		callback(newName)
		dialog.Close()
	})
	buttonBox.Append(renameBtn)

	mainBox.Append(buttonBox)
	dialog.SetChild(mainBox)

	// Handle Enter key to submit
	entryController := gtk.NewEventControllerKey()
	entryController.ConnectKeyPressed(func(keyval, keycode uint, state gdk.ModifierType) bool {
		if keyval == gdk.KEY_Return || keyval == gdk.KEY_KP_Enter {
			newName := entry.Text() + ext
			callback(newName)
			dialog.Close()
			return true
		}
		if keyval == gdk.KEY_Escape {
			callback("")
			dialog.Close()
			return true
		}
		return false
	})
	entry.AddController(entryController)

	dialog.SetVisible(true)
}

// ShowInfoDialog displays image information
func (w *Window) ShowInfoDialog(info *image.Info) {
	// Create a custom dialog window for info display
	dialog := gtk.NewWindow()
	dialog.SetTitle("Image Information")
	dialog.SetTransientFor(&w.window.Window)
	dialog.SetModal(true)
	dialog.SetDefaultSize(450, -1)
	dialog.SetDestroyWithParent(true)

	// Main container
	mainBox := gtk.NewBox(gtk.OrientationVertical, 12)
	mainBox.SetMarginTop(20)
	mainBox.SetMarginBottom(20)
	mainBox.SetMarginStart(20)
	mainBox.SetMarginEnd(20)

	// Title
	titleLabel := gtk.NewLabel("Image Information")
	titleLabel.AddCSSClass("title-2")
	mainBox.Append(titleLabel)

	// Info grid
	grid := gtk.NewGrid()
	grid.SetRowSpacing(8)
	grid.SetColumnSpacing(16)
	grid.SetMarginTop(12)

	addInfoRow := func(row int, label, value string) {
		labelWidget := gtk.NewLabel(label)
		labelWidget.SetHAlign(gtk.AlignEnd)
		labelWidget.AddCSSClass("dim-label")
		grid.Attach(labelWidget, 0, row, 1, 1)

		valueWidget := gtk.NewLabel(value)
		valueWidget.SetHAlign(gtk.AlignStart)
		valueWidget.SetSelectable(true)
		grid.Attach(valueWidget, 1, row, 1, 1)
	}

	addInfoRow(0, "File:", info.Name)
	addInfoRow(1, "Size:", info.FileSize)
	addInfoRow(2, "Dimensions:", fmt.Sprintf("%dx%d", info.Width, info.Height))
	addInfoRow(3, "Format:", info.Format)
	addInfoRow(4, "Modified:", info.Modified)

	mainBox.Append(grid)

	// EXIF data if available
	if info.ExifData != "" {
		exifLabel := gtk.NewLabel("EXIF Data")
		exifLabel.AddCSSClass("title-4")
		exifLabel.SetMarginTop(16)
		exifLabel.SetHAlign(gtk.AlignStart)
		mainBox.Append(exifLabel)

		exifContent := gtk.NewLabel(info.ExifData)
		exifContent.SetHAlign(gtk.AlignStart)
		exifContent.SetSelectable(true)
		exifContent.SetWrap(true)
		mainBox.Append(exifContent)
	}

	// OK button
	okBtn := gtk.NewButtonWithLabel("OK")
	okBtn.SetHAlign(gtk.AlignCenter)
	okBtn.SetMarginTop(16)
	okBtn.ConnectClicked(func() {
		dialog.Close()
	})
	mainBox.Append(okBtn)

	dialog.SetChild(mainBox)

	// Handle Escape key
	controller := gtk.NewEventControllerKey()
	controller.ConnectKeyPressed(func(keyval, keycode uint, state gdk.ModifierType) bool {
		if keyval == gdk.KEY_Escape || keyval == gdk.KEY_Return {
			dialog.Close()
			return true
		}
		return false
	})
	dialog.AddController(controller)

	dialog.SetVisible(true)
}

// ShowHelpDialog displays keybindings help
func (w *Window) ShowHelpDialog() {
	// Create a custom dialog window for help display
	dialog := gtk.NewWindow()
	dialog.SetTitle("Keyboard Shortcuts")
	dialog.SetTransientFor(&w.window.Window)
	dialog.SetModal(true)
	dialog.SetDefaultSize(400, -1)
	dialog.SetDestroyWithParent(true)

	// Main container
	mainBox := gtk.NewBox(gtk.OrientationVertical, 12)
	mainBox.SetMarginTop(20)
	mainBox.SetMarginBottom(20)
	mainBox.SetMarginStart(20)
	mainBox.SetMarginEnd(20)

	// Title
	titleLabel := gtk.NewLabel("Keyboard Shortcuts")
	titleLabel.AddCSSClass("title-2")
	mainBox.Append(titleLabel)

	// Help content
	helpLabel := gtk.NewLabel(keybindings.GetHelpTextPlain())
	helpLabel.SetHAlign(gtk.AlignStart)
	helpLabel.SetMarginTop(12)
	mainBox.Append(helpLabel)

	// OK button
	okBtn := gtk.NewButtonWithLabel("OK")
	okBtn.SetHAlign(gtk.AlignCenter)
	okBtn.SetMarginTop(16)
	okBtn.ConnectClicked(func() {
		dialog.Close()
	})
	mainBox.Append(okBtn)

	dialog.SetChild(mainBox)

	// Handle Escape key
	controller := gtk.NewEventControllerKey()
	controller.ConnectKeyPressed(func(keyval, keycode uint, state gdk.ModifierType) bool {
		if keyval == gdk.KEY_Escape || keyval == gdk.KEY_Return {
			dialog.Close()
			return true
		}
		return false
	})
	dialog.AddController(controller)

	dialog.SetVisible(true)
}

// ShowError displays an error message
func (w *Window) ShowError(message string) {
	// Create a custom dialog window for error display
	dialog := gtk.NewWindow()
	dialog.SetTitle("Error")
	dialog.SetTransientFor(&w.window.Window)
	dialog.SetModal(true)
	dialog.SetDefaultSize(400, -1)
	dialog.SetDestroyWithParent(true)

	// Main container
	mainBox := gtk.NewBox(gtk.OrientationVertical, 16)
	mainBox.SetMarginTop(24)
	mainBox.SetMarginBottom(24)
	mainBox.SetMarginStart(24)
	mainBox.SetMarginEnd(24)

	// Error icon and title
	headerBox := gtk.NewBox(gtk.OrientationHorizontal, 12)
	headerBox.SetHAlign(gtk.AlignCenter)

	icon := gtk.NewImageFromIconName("dialog-error-symbolic")
	icon.SetIconSize(gtk.IconSizeLarge)
	headerBox.Append(icon)

	titleLabel := gtk.NewLabel("Error")
	titleLabel.AddCSSClass("title-2")
	headerBox.Append(titleLabel)

	mainBox.Append(headerBox)

	// Error message
	msgLabel := gtk.NewLabel(message)
	msgLabel.SetWrap(true)
	mainBox.Append(msgLabel)

	// OK button
	okBtn := gtk.NewButtonWithLabel("OK")
	okBtn.SetHAlign(gtk.AlignCenter)
	okBtn.SetMarginTop(12)
	okBtn.ConnectClicked(func() {
		dialog.Close()
	})
	mainBox.Append(okBtn)

	dialog.SetChild(mainBox)

	// Handle Escape key
	controller := gtk.NewEventControllerKey()
	controller.ConnectKeyPressed(func(keyval, keycode uint, state gdk.ModifierType) bool {
		if keyval == gdk.KEY_Escape || keyval == gdk.KEY_Return {
			dialog.Close()
			return true
		}
		return false
	})
	dialog.AddController(controller)

	dialog.SetVisible(true)
}
