// Package image provides image operations and metadata extraction.
package image

import (
	"fmt"
	"image"
	_ "image/gif"
	_ "image/jpeg"
	_ "image/png"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	_ "golang.org/x/image/bmp"
	_ "golang.org/x/image/tiff"
	_ "golang.org/x/image/webp"
)

// MoveToTrash moves a file to the system trash using gio.
func MoveToTrash(path string) error {
	cmd := exec.Command("gio", "trash", path)
	if err := cmd.Run(); err != nil {
		// Fallback: delete directly if gio is not available
		return os.Remove(path)
	}
	return nil
}

// Rename renames a file to a new name in the same directory.
func Rename(oldPath, newName string) (string, error) {
	dir := filepath.Dir(oldPath)
	newPath := filepath.Join(dir, newName)

	if _, err := os.Stat(newPath); err == nil {
		return "", fmt.Errorf("file already exists: %s", newName)
	}

	if err := os.Rename(oldPath, newPath); err != nil {
		return "", err
	}

	return newPath, nil
}

// Info contains metadata about an image file.
type Info struct {
	Name     string
	Path     string
	FileSize string
	Width    int
	Height   int
	Format   string
	Modified string
	ExifData string
}

// GetInfo retrieves metadata for an image file.
func GetInfo(path string) (*Info, error) {
	stat, err := os.Stat(path)
	if err != nil {
		return nil, err
	}

	info := &Info{
		Name:     filepath.Base(path),
		Path:     path,
		FileSize: formatFileSize(stat.Size()),
		Modified: stat.ModTime().Format(time.RFC1123),
		Format:   getFormatFromExt(filepath.Ext(path)),
	}

	// Get image dimensions natively via Go's image decoders
	if w, h, err := getImageDimensions(path); err == nil {
		info.Width = w
		info.Height = h
	}

	info.ExifData = getExifData(path)

	return info, nil
}

// getImageDimensions reads image dimensions using Go's native image decoders.
// This replaces the previous ImageMagick `identify` dependency.
func getImageDimensions(path string) (int, int, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, 0, err
	}
	defer f.Close()

	cfg, _, err := image.DecodeConfig(f)
	if err != nil {
		return 0, 0, err
	}
	return cfg.Width, cfg.Height, nil
}

// formatFileSize formats a file size in bytes to a human-readable string.
func formatFileSize(bytes int64) string {
	const (
		KB = 1024
		MB = KB * 1024
		GB = MB * 1024
	)

	switch {
	case bytes >= GB:
		return fmt.Sprintf("%.2f GB", float64(bytes)/float64(GB))
	case bytes >= MB:
		return fmt.Sprintf("%.2f MB", float64(bytes)/float64(MB))
	case bytes >= KB:
		return fmt.Sprintf("%.2f KB", float64(bytes)/float64(KB))
	default:
		return fmt.Sprintf("%d bytes", bytes)
	}
}

// formatNames maps file extensions to human-readable format names.
var formatNames = map[string]string{
	".jpg":  "JPEG",
	".jpeg": "JPEG",
	".png":  "PNG",
	".gif":  "GIF",
	".webp": "WebP",
	".bmp":  "BMP",
	".tiff": "TIFF",
	".tif":  "TIFF",
	".ico":  "ICO",
	".apng": "APNG",
}

// getFormatFromExt returns a format name based on file extension.
func getFormatFromExt(ext string) string {
	if format, ok := formatNames[strings.ToLower(ext)]; ok {
		return format
	}
	return "Unknown"
}

// getExifData attempts to extract basic EXIF data using exiftool.
func getExifData(path string) string {
	cmd := exec.Command("exiftool", "-s", "-Make", "-Model", "-DateTimeOriginal", "-ExposureTime", "-FNumber", "-ISO", path)
	output, err := cmd.Output()
	if err != nil {
		return ""
	}
	return string(output)
}
