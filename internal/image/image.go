// Package image provides image operations and metadata extraction.
package image

import (
	"fmt"
	"image"
	_ "image/gif"
	_ "image/jpeg"
	_ "image/png"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/rwcarlsen/goexif/exif"
	_ "golang.org/x/image/bmp"
	_ "golang.org/x/image/tiff"
	_ "golang.org/x/image/webp"
)

// MoveToTrash moves a file to the XDG trash directory
// (~/.local/share/Trash/files/) following the freedesktop.org Trash
// specification. Creates the trash directories if they don't exist and
// writes a .trashinfo file with the original path and deletion date.
func MoveToTrash(path string) error {
	home, err := os.UserHomeDir()
	if err != nil {
		return fmt.Errorf("move to trash: %w", err)
	}

	filesDir := filepath.Join(home, ".local/share/Trash/files")
	infoDir := filepath.Join(home, ".local/share/Trash/info")

	for _, d := range []string{filesDir, infoDir} {
		if err := os.MkdirAll(d, 0700); err != nil {
			return fmt.Errorf("move to trash: %w", err)
		}
	}

	name := filepath.Base(path)
	dest := filepath.Join(filesDir, name)
	ext := filepath.Ext(name)
	base := strings.TrimSuffix(name, ext)
	for i := 1; ; i++ {
		if _, err := os.Stat(dest); os.IsNotExist(err) {
			break
		}
		dest = filepath.Join(filesDir, fmt.Sprintf("%s_%d%s", base, i, ext))
	}

	// Write .trashinfo atomically before moving the file, so a crash
	// between writing the info and moving the file leaves an orphan
	// .trashinfo rather than a missing one.
	info := fmt.Sprintf(
		"[Trash Info]\nPath=%s\nDeletionDate=%s\n",
		path, time.Now().Format("2006-01-02T15:04:05"),
	)
	infoName := strings.TrimSuffix(filepath.Base(dest), filepath.Ext(dest)) + ".trashinfo"
	infoPath := filepath.Join(infoDir, infoName)
	tmpInfoPath := infoPath + ".tmp"
	if err := os.WriteFile(tmpInfoPath, []byte(info), 0644); err != nil {
		return fmt.Errorf("move to trash: %w", err)
	}
	if err := os.Rename(tmpInfoPath, infoPath); err != nil {
		os.Remove(tmpInfoPath)
		return fmt.Errorf("move to trash: %w", err)
	}

	if err := os.Rename(path, dest); err != nil {
		os.Remove(infoPath)
		return fmt.Errorf("move to trash: %w", err)
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

// getExifData extracts basic EXIF data using Go's native EXIF decoder.
// Returns empty string if no EXIF data is present or extraction fails.
func getExifData(path string) string {
	f, err := os.Open(path)
	if err != nil {
		return ""
	}
	defer f.Close()

	x, err := exif.Decode(f)
	if err != nil {
		return ""
	}

	var parts []string

	if tag, err := x.Get(exif.Make); err == nil {
		parts = append(parts, tag.String())
	}
	if tag, err := x.Get(exif.Model); err == nil {
		parts = append(parts, tag.String())
	}
	if tag, err := x.Get(exif.DateTimeOriginal); err == nil {
		parts = append(parts, tag.String())
	}
	if tag, err := x.Get(exif.ExposureTime); err == nil {
		parts = append(parts, tag.String())
	}
	if tag, err := x.Get(exif.FNumber); err == nil {
		parts = append(parts, tag.String())
	}
	if tag, err := x.Get(exif.ISOSpeedRatings); err == nil {
		parts = append(parts, fmt.Sprintf("ISO %s", tag.String()))
	}

	return strings.Join(parts, "\n")
}
