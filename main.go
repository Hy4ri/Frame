package main

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/Hy4ri/frame/internal/app"
	"github.com/diamondburned/gotk4/pkg/gio/v2"
	"github.com/diamondburned/gotk4/pkg/gtk/v4"
)

func main() {
	application := gtk.NewApplication("com.github.hy4ri.frame", gio.ApplicationHandlesOpen)

	// Handle opening with file arguments
	application.ConnectOpen(func(files []gio.Filer, hint string) {
		if len(files) > 0 {
			path := files[0].Path()
			if path != "" {
				startApp(application, path)
			}
		}
	})

	// Handle activation without arguments
	application.ConnectActivate(func() {
		// If no file provided, show a file chooser or usage
		startApp(application, "")
	})

	if code := application.Run(os.Args); code > 0 {
		os.Exit(code)
	}
}

// startApp initializes and displays the main window
func startApp(application *gtk.Application, initialPath string) {
	// Resolve to absolute path if provided
	var absPath string
	if initialPath != "" {
		var err error
		absPath, err = filepath.Abs(initialPath)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error resolving path: %v\n", err)
			absPath = initialPath
		}
	}

	// Create and run the Frame application
	frameApp := app.New(application, absPath)
	frameApp.Run()
}
