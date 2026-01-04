// Package image provides image loading, operations, and metadata extraction.
package image

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"time"
)

// MoveToTrash moves a file to the system trash using gio
func MoveToTrash(path string) error {
	// Use gio trash command which works on most Linux desktops
	cmd := exec.Command("gio", "trash", path)
	if err := cmd.Run(); err != nil {
		// Fallback: try to delete directly if gio is not available
		return os.Remove(path)
	}
	return nil
}

// Rename renames a file to a new name in the same directory
func Rename(oldPath, newName string) (string, error) {
	dir := filepath.Dir(oldPath)
	newPath := filepath.Join(dir, newName)

	// Check if target already exists
	if _, err := os.Stat(newPath); err == nil {
		return "", fmt.Errorf("file already exists: %s", newName)
	}

	if err := os.Rename(oldPath, newPath); err != nil {
		return "", err
	}

	return newPath, nil
}

// Info contains metadata about an image file
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

// GetInfo retrieves metadata for an image file
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

	// Try to get image dimensions using identify command (ImageMagick)
	// This is optional - if it fails, we just won't have dimensions
	if dims, err := getImageDimensions(path); err == nil {
		info.Width = dims[0]
		info.Height = dims[1]
	}

	// Try to extract basic EXIF data
	info.ExifData = getExifData(path)

	return info, nil
}

// formatFileSize formats a file size in bytes to a human-readable string
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

// getFormatFromExt returns a format name based on file extension
func getFormatFromExt(ext string) string {
	formats := map[string]string{
		".jpg":  "JPEG",
		".jpeg": "JPEG",
		".png":  "PNG",
		".gif":  "GIF",
		".webp": "WebP",
		".bmp":  "BMP",
		".svg":  "SVG",
		".tiff": "TIFF",
		".tif":  "TIFF",
		".ico":  "ICO",
	}

	if format, ok := formats[ext]; ok {
		return format
	}
	return "Unknown"
}

// getImageDimensions uses the 'file' command to get image dimensions
func getImageDimensions(path string) ([2]int, error) {
	// Try using 'identify' from ImageMagick if available
	cmd := exec.Command("identify", "-format", "%w %h", path)
	output, err := cmd.Output()
	if err != nil {
		return [2]int{0, 0}, err
	}

	var w, h int
	_, err = fmt.Sscanf(string(output), "%d %d", &w, &h)
	if err != nil {
		return [2]int{0, 0}, err
	}

	return [2]int{w, h}, nil
}

// getExifData attempts to extract basic EXIF data using exiftool
func getExifData(path string) string {
	// Try using exiftool if available
	cmd := exec.Command("exiftool", "-s", "-Make", "-Model", "-DateTimeOriginal", "-ExposureTime", "-FNumber", "-ISO", path)
	output, err := cmd.Output()
	if err != nil {
		return ""
	}
	return string(output)
}
