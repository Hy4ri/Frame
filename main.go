package main

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/Hy4ri/frame/internal/app"
)

func main() {
	// Resolve initial path from CLI arguments
	var initialPath string
	if len(os.Args) > 1 {
		absPath, err := filepath.Abs(os.Args[1])
		if err != nil {
			fmt.Fprintf(os.Stderr, "Error resolving path: %v\n", err)
			os.Exit(1)
		}
		initialPath = absPath
	}

	frameApp := app.New(initialPath)
	frameApp.Run()
}
