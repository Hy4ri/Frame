// Package app provides the core application logic for Frame image viewer.
package app

import (
	"os"
	"path/filepath"
	"slices"
	"strings"

	"github.com/Hy4ri/frame/internal/gui"
	"github.com/Hy4ri/frame/internal/image"
)

// supportedExtensions lists all image formats Frame can display.
var supportedExtensions = map[string]bool{
	".jpg":  true,
	".jpeg": true,
	".png":  true,
	".gif":  true,
	".webp": true,
	".bmp":  true,
	".tiff": true,
	".tif":  true,
	".ico":  true,
	".apng": true,
}

// App holds the application state and manages image viewing.
type App struct {
	window       *gui.Window
	images       []string // List of image paths in current directory
	currentIndex int      // Index of currently displayed image
	initialPath  string   // Path to initial image or directory
}

// New creates a new Frame application instance.
func New(initialPath string) *App {
	return &App{
		initialPath:  initialPath,
		currentIndex: 0,
	}
}

// Run initializes the UI and starts the application.
func (a *App) Run() {
	if a.initialPath != "" {
		a.loadImagesFromPath(a.initialPath)
	}

	a.window = gui.NewWindow(a)

	if len(a.images) > 0 {
		a.DisplayImage(a.currentIndex)
	} else if a.initialPath == "" {
		a.window.ShowFileChooser()
	}

	a.window.ShowAndRun()
}

// loadImagesFromPath populates the image list from a file or directory path.
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

	entries, err := os.ReadDir(dir)
	if err != nil {
		return
	}

	a.images = make([]string, 0, len(entries))
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		ext := strings.ToLower(filepath.Ext(entry.Name()))
		if supportedExtensions[ext] {
			a.images = append(a.images, filepath.Join(dir, entry.Name()))
		}
	}

	slices.Sort(a.images)

	// Binary search since the list is sorted (O(log n) vs O(n))
	if targetFile != "" {
		if idx, found := slices.BinarySearch(a.images, targetFile); found {
			a.currentIndex = idx
		}
	}
}

// DisplayImage loads and displays the image at the given index.
func (a *App) DisplayImage(index int) {
	if index < 0 || index >= len(a.images) {
		return
	}
	a.currentIndex = index
	a.window.LoadImage(a.images[index])
}

// NextImage displays the next image in the list.
func (a *App) NextImage() {
	if a.currentIndex < len(a.images)-1 {
		a.DisplayImage(a.currentIndex + 1)
	}
}

// PrevImage displays the previous image in the list.
func (a *App) PrevImage() {
	if a.currentIndex > 0 {
		a.DisplayImage(a.currentIndex - 1)
	}
}

// FirstImage jumps to the first image.
func (a *App) FirstImage() {
	a.DisplayImage(0)
}

// LastImage jumps to the last image.
func (a *App) LastImage() {
	a.DisplayImage(len(a.images) - 1)
}

// DeleteCurrent deletes the current image and moves to the next.
func (a *App) DeleteCurrent() {
	if len(a.images) == 0 {
		return
	}

	currentPath := a.images[a.currentIndex]

	a.window.ShowDeleteConfirmation(currentPath, func(confirmed bool) {
		if !confirmed {
			return
		}

		if err := image.MoveToTrash(currentPath); err != nil {
			a.window.ShowError("Failed to delete: " + err.Error())
			return
		}

		a.images = append(a.images[:a.currentIndex], a.images[a.currentIndex+1:]...)

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

// RotateCurrent rotates the current image.
func (a *App) RotateCurrent(clockwise bool) {
	if len(a.images) == 0 {
		return
	}
	a.window.RotateImage(clockwise)
}

// RenameCurrent opens a dialog to rename the current image.
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

		a.images[a.currentIndex] = newPath
		a.window.UpdateTitle(newPath)
	})
}

// ShowInfo displays information about the current image.
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

// ShowHelp displays the keybindings help dialog.
func (a *App) ShowHelp() {
	a.window.ShowHelpDialog()
}

// ToggleFullscreen toggles fullscreen mode.
func (a *App) ToggleFullscreen() {
	a.window.ToggleFullscreen()
}

// ZoomIn increases the zoom level.
func (a *App) ZoomIn() {
	a.window.ZoomIn()
}

// ZoomOut decreases the zoom level.
func (a *App) ZoomOut() {
	a.window.ZoomOut()
}

// ZoomFit fits the image to the window.
func (a *App) ZoomFit() {
	a.window.ZoomFit()
}

// ZoomOriginal displays the image at its original size (1:1).
func (a *App) ZoomOriginal() {
	a.window.ZoomOriginal()
}

// Quit exits the application.
func (a *App) Quit() {
	a.window.Close()
}

// GetCurrentPath returns the current image path.
func (a *App) GetCurrentPath() string {
	if a.currentIndex < 0 || a.currentIndex >= len(a.images) {
		return ""
	}
	return a.images[a.currentIndex]
}

// GetImageCount returns the total number of images.
func (a *App) GetImageCount() int {
	return len(a.images)
}

// GetCurrentIndex returns the current image index (1-based for display).
func (a *App) GetCurrentIndex() int {
	return a.currentIndex + 1
}

// OpenPath opens a new file or directory.
func (a *App) OpenPath(path string) {
	a.loadImagesFromPath(path)
	if len(a.images) > 0 {
		a.DisplayImage(0)
	}
}
