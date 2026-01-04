// Package app provides the core application logic for Frame image viewer.
package app

import (
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/Hy4ri/frame/internal/gui"
	"github.com/Hy4ri/frame/internal/image"
	"github.com/diamondburned/gotk4/pkg/gtk/v4"
)

// supportedExtensions lists all image formats Frame can display
var supportedExtensions = map[string]bool{
	".jpg":  true,
	".jpeg": true,
	".png":  true,
	".gif":  true,
	".webp": true,
	".bmp":  true,
	".svg":  true,
	".tiff": true,
	".tif":  true,
	".ico":  true,
}

// App holds the application state and manages image viewing
type App struct {
	gtkApp       *gtk.Application
	window       *gui.Window
	images       []string // List of image paths in current directory
	currentIndex int      // Index of currently displayed image
	currentPath  string   // Path to current image or directory
}

// New creates a new Frame application instance
func New(gtkApp *gtk.Application, initialPath string) *App {
	app := &App{
		gtkApp:       gtkApp,
		currentPath:  initialPath,
		currentIndex: 0,
	}
	return app
}

// Run initializes the UI and starts the application
func (a *App) Run() {
	// Load images from path
	if a.currentPath != "" {
		a.loadImagesFromPath(a.currentPath)
	}

	// Create the main window
	a.window = gui.NewWindow(a.gtkApp, a)

	// Display initial image if available
	if len(a.images) > 0 {
		a.DisplayImage(a.currentIndex)
	} else if a.currentPath == "" {
		a.window.ShowFileChooser()
	}

	a.window.Show()
}

// loadImagesFromPath populates the image list from a file or directory path
func (a *App) loadImagesFromPath(path string) {
	info, err := os.Stat(path)
	if err != nil {
		return
	}

	var dir string
	var targetFile string

	if info.IsDir() {
		dir = path
	} else {
		dir = filepath.Dir(path)
		targetFile = path
	}

	// Scan directory for images
	entries, err := os.ReadDir(dir)
	if err != nil {
		return
	}

	a.images = nil
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		ext := strings.ToLower(filepath.Ext(entry.Name()))
		if supportedExtensions[ext] {
			a.images = append(a.images, filepath.Join(dir, entry.Name()))
		}
	}

	// Sort images by name
	sort.Strings(a.images)

	// If a specific file was provided, find its index
	if targetFile != "" {
		for i, img := range a.images {
			if img == targetFile {
				a.currentIndex = i
				break
			}
		}
	}
}

// DisplayImage loads and displays the image at the given index
func (a *App) DisplayImage(index int) {
	if index < 0 || index >= len(a.images) {
		return
	}
	a.currentIndex = index
	a.window.LoadImage(a.images[index])
}

// Navigation methods for vim keybindings

// NextImage displays the next image in the list
func (a *App) NextImage() {
	if a.currentIndex < len(a.images)-1 {
		a.DisplayImage(a.currentIndex + 1)
	}
}

// PrevImage displays the previous image in the list
func (a *App) PrevImage() {
	if a.currentIndex > 0 {
		a.DisplayImage(a.currentIndex - 1)
	}
}

// FirstImage jumps to the first image
func (a *App) FirstImage() {
	a.DisplayImage(0)
}

// LastImage jumps to the last image
func (a *App) LastImage() {
	a.DisplayImage(len(a.images) - 1)
}

// Image operations

// DeleteCurrent deletes the current image and moves to the next
func (a *App) DeleteCurrent() {
	if len(a.images) == 0 {
		return
	}

	currentPath := a.images[a.currentIndex]

	// Confirm deletion via dialog
	a.window.ShowDeleteConfirmation(currentPath, func(confirmed bool) {
		if !confirmed {
			return
		}

		// Move to trash (safer than permanent delete)
		if err := image.MoveToTrash(currentPath); err != nil {
			a.window.ShowError("Failed to delete: " + err.Error())
			return
		}

		// Remove from list
		a.images = append(a.images[:a.currentIndex], a.images[a.currentIndex+1:]...)

		// Adjust index and display next image
		if len(a.images) == 0 {
			a.window.ClearImage()
			return
		}
		if a.currentIndex >= len(a.images) {
			a.currentIndex = len(a.images) - 1
		}
		a.DisplayImage(a.currentIndex)
	})
}

// RotateCurrent rotates the current image clockwise by 90 degrees
func (a *App) RotateCurrent(clockwise bool) {
	if len(a.images) == 0 {
		return
	}
	a.window.RotateImage(clockwise)
}

// RenameCurrent opens a dialog to rename the current image
func (a *App) RenameCurrent() {
	if len(a.images) == 0 {
		return
	}

	currentPath := a.images[a.currentIndex]
	a.window.ShowRenameDialog(currentPath, func(newName string) {
		if newName == "" {
			return
		}

		newPath, err := image.Rename(currentPath, newName)
		if err != nil {
			a.window.ShowError("Failed to rename: " + err.Error())
			return
		}

		// Update the path in our list
		a.images[a.currentIndex] = newPath
		a.window.UpdateTitle(newPath)
	})
}

// ShowInfo displays information about the current image
func (a *App) ShowInfo() {
	if len(a.images) == 0 {
		return
	}
	info, err := image.GetInfo(a.images[a.currentIndex])
	if err != nil {
		a.window.ShowError("Failed to get info: " + err.Error())
		return
	}
	a.window.ShowInfoDialog(info)
}

// ShowHelp displays the keybindings help dialog
func (a *App) ShowHelp() {
	a.window.ShowHelpDialog()
}

// ToggleFullscreen toggles fullscreen mode
func (a *App) ToggleFullscreen() {
	a.window.ToggleFullscreen()
}

// Zoom operations

// ZoomIn increases the zoom level
func (a *App) ZoomIn() {
	a.window.ZoomIn()
}

// ZoomOut decreases the zoom level
func (a *App) ZoomOut() {
	a.window.ZoomOut()
}

// ZoomFit fits the image to the window
func (a *App) ZoomFit() {
	a.window.ZoomFit()
}

// ZoomOriginal displays the image at its original size (1:1)
func (a *App) ZoomOriginal() {
	a.window.ZoomOriginal()
}

// Quit exits the application
func (a *App) Quit() {
	a.gtkApp.Quit()
}

// GetCurrentPath returns the current image path
func (a *App) GetCurrentPath() string {
	if a.currentIndex < 0 || a.currentIndex >= len(a.images) {
		return ""
	}
	return a.images[a.currentIndex]
}

// GetImageCount returns the total number of images
func (a *App) GetImageCount() int {
	return len(a.images)
}

// GetCurrentIndex returns the current image index (1-based for display)
func (a *App) GetCurrentIndex() int {
	return a.currentIndex + 1
}

// OpenPath opens a new file or directory
func (a *App) OpenPath(path string) {
	a.loadImagesFromPath(path)
	if len(a.images) > 0 {
		a.DisplayImage(0)
	}
}
