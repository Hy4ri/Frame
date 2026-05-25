# Frame

> A fast, minimal image viewer for Linux with vim keybindings, built with C and [SDL3](https://wiki.libsdl.org/SDL3).

<p align="center">
  <img src="assets/frame.svg" alt="Frame Icon" width="128" height="128">
</p>

<div align="center">

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-1.0.0-green.svg)](meson.build)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)](https://github.com/Hy4ri/frame)
[![SDL3](https://img.shields.io/badge/SDL3-powered-orange)](https://wiki.libsdl.org/SDL3)

</div>

---

## Features

- **Minimal Interface** — Clean, distraction-free viewing experience with a dark background
- **Vim Keybindings** — Navigate images using familiar `h`/`j`/`k`/`l`, `gg`, `G` keys
- **Image Navigation** — Previous/next, first/last, scroll wheel, keyboard arrows
- **Zoom & Pan** — Mouse wheel zoom (cursor-aware), click-and-drag panning
- **Rotation** — Clockwise and counter-clockwise 90° rotation
- **Image Operations** — Delete (move to trash with confirmation), rename via dialog
- **Image Info** — View dimensions, file size, format, EXIF data (camera make/model, exposure, aperture, ISO, etc.)
- **Format Support** — JPEG, PNG, GIF (animated), APNG (animated), WebP, BMP, TIFF, ICO
- **Animated Images** — Full GIF and APNG animation playback
- **Smart Caching** — LRU cache with background prefetching for instant navigation
- **Fullscreen** — Toggle fullscreen mode with `f`
- **Help** — Built-in keyboard shortcut reference (`?`)
- **Desktop Integration** — Installable via `.desktop` file with MIME type support
- **Wayland + X11** — Runs natively on both via SDL3

---

## Installation

### Nix (Recommended)

**Run directly (no install):**
```bash
nix run github:Hy4ri/frame -- /path/to/image.jpg
```

**Add to your system permanently:**

```nix
# flake.nix
{
  inputs.frame.url = "github:Hy4ri/frame";

  outputs = { self, nixpkgs, frame, ... }: {
    # Use in NixOS configuration
  };
}
```

```nix
# configuration.nix
{ inputs, ... }:
{
  environment.systemPackages = [ inputs.frame.packages.${pkgs.system}.default ];

  # Or use the overlay:
  nixpkgs.overlays = [ inputs.frame.overlays.default ];
  environment.systemPackages = [ pkgs.frame ];
}
```

### Build from Source

#### With Nix (recommended for development)

```bash
# Enter development shell (provides all dependencies)
nix develop

# Configure and build
meson setup build
ninja -C build

# Run
./build/frame /path/to/image.jpg
```

#### Without Nix

```bash
# Install dependencies (Debian/Ubuntu example)
sudo apt install libsdl3-dev libsdl3-image-dev libsdl3-ttf-dev libexif-dev zenity

# Configure and build
meson setup build
ninja -C build

# Run
./build/frame /path/to/image.jpg
```

#### Using Makefile (alternative)

```bash
make
./frame /path/to/image.jpg
```

### Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| [SDL3](https://wiki.libsdl.org/SDL3) | Windowing, rendering, input | Yes |
| [SDL3_image](https://github.com/libsdl-org/SDL_image) | Image format loading | Yes |
| [SDL3_ttf](https://github.com/libsdl-org/SDL_ttf) | Font rendering for overlays | Yes |
| [libexif](https://github.com/libexif/libexif) | EXIF metadata extraction | Yes |
| [zenity](https://wiki.gnome.org/Projects/Zenity) | Dialog boxes (delete confirm, rename) | Runtime only |
| Meson / Ninja | Build system | Build only |
| pkg-config | Dependency discovery | Build only |

---

## Usage

```bash
# Open a specific image
frame /path/to/image.jpg

# Open a directory (shows all supported images, sorted alphabetically)
frame /path/to/images/

# Open without arguments (shows usage message)
frame
```

When you open a directory or an image file, Frame scans the directory for all supported image files, sorts them alphabetically, and displays the first (or the specifically opened) image.

### Window Title

The window title shows the current filename and position:

```
image_001.jpg (3/42) - Frame
```

---

## Keybindings

### Navigation

| Key | Action |
|-----|--------|
| `h` / `←` | Previous image |
| `l` / `→` | Next image |
| `j` / `↓` | Next image |
| `k` / `↑` | Previous image |
| `gg` (double-tap `g` within 500ms) | First image |
| `G` (Shift+`g`) | Last image |

### View Controls

| Key | Action |
|-----|--------|
| `f` | Toggle fullscreen |
| `+` / `=` / `z` | Zoom in (5% from center) |
| `-` / `x` | Zoom out (5% from center) |
| `0` | Fit to window (preserves aspect ratio) |
| `1` | Original size (1:1 pixel mapping) |
| Scroll wheel | Zoom toward cursor position |
| Click + drag | Pan image |

### Image Operations

| Key | Action |
|-----|--------|
| `r` | Rotate clockwise 90° |
| `R` (Shift+`r`) | Rotate counter-clockwise 90° |
| `d` / `Del` | Delete image (moves to XDG Trash) |
| `F2` | Rename image (dialog box) |
| `i` | Show image information overlay |

### General

| Key | Action |
|-----|--------|
| `?` | Show keyboard shortcuts help |
| `q` / `Esc` | Quit |

**Any key dismisses an active overlay** (info or help screen) without performing its normal action.

---

## Image Information Overlay

Press `i` to display an overlay with detailed information about the current image:

- **Filename**
- **File size** (human-readable: KB, MB, etc.)
- **Image dimensions** (width × height)
- **Format** (JPEG, PNG, WebP, GIF, etc.)
- **Last modified date**
- **Position** in the image list (index / total)
- **EXIF metadata** (when available):
  - Camera make and model
  - Date and time taken
  - Exposure time
  - Aperture (f-number)
  - ISO speed rating
  - Orientation
  - Focal length
  - Flash status

---

## Project Architecture

Frame is organized into a modular C codebase under `src/`, built with Meson.

```
frame/
├── assets/          # Application icons (SVG, PNG)
├── src/             # Source code
│   ├── main.c       # Entry point, SDL init, main loop
│   ├── app.c/h      # Image list management, directory scanning
│   ├── viewer.c/h   # Image display, zoom, pan, rotation, animation
│   ├── input.c/h    # Keyboard input handling, keybindings
│   ├── overlay.c/h  # HUD overlay system (info, help)
│   ├── loader.c/h   # Image file loading and format detection
│   ├── cache.c/h    # LRU image cache with prefetching
│   ├── anim.c/h     # Animated GIF/APNG playback
│   ├── fileops.c/h  # File operations (trash, rename)
│   ├── exif.c/h     # EXIF metadata extraction
│   └── utils.c/h    # Utility functions (file size formatting)
├── build/           # Build artifacts (generated)
├── flake.nix        # Nix flake for build and development
├── meson.build      # Meson build definition
├── Makefile         # Alternative GNU Make build
└── frame.desktop    # Desktop entry for file manager integration
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for a detailed module breakdown.

---

## Troubleshooting

| Problem | Solution |
|---|---|
| **"SDL_Init failed"** | Ensure your system has SDL3 installed and a working display server (Wayland or X11). |
| **No images found** | Frame scans the directory for images. If you opened a file, its directory is scanned. Only supported extensions are listed: `.jpg`, `.jpeg`, `.png`, `.gif`, `.webp`, `.bmp`, `.tiff`, `.tif`, `.ico`, `.apng`. |
| **No overlays shown** | The overlay system requires a TTF font. Frame looks for DejaVuSans.ttf and LiberationSans-Regular.ttf in common locations. Install `fonts-dejavu-core` or `liberation-fonts`. |
| **Zenity dialog doesn't appear** | Ensure `zenity` is installed. On Debian/Ubuntu: `sudo apt install zenity`. On Nix, it's provided automatically. |
| **Trash operation fails** | The XDG Trash specification requires `~/.local/share/Trash/`. Frame creates this directory automatically if it doesn't exist. |
| **Animation not playing** | Only GIF and APNG formats support animation. If an animated file displays only the first frame, it may be a static variant of the format. |
| **Performance issues** | Frame caps at ~120 FPS and sleeps between frames. Large images (over 16384 pixels in any dimension) are rejected to prevent OOM. |
| **"No font found"** | Install a TrueType font. On Nix, `nix develop` provides fonts. On Debian/Ubuntu: `sudo apt install fonts-dejavu-core`. |

---

## Contributing

Contributions are welcome! Here's how you can help:

1. **Report bugs** — Open an issue with a clear description and reproduction steps
2. **Suggest features** — Open an issue with the "enhancement" label
3. **Submit pull requests** — Follow the guidelines below

### Development Setup

```bash
# Clone the repository
git clone https://github.com/Hy4ri/frame
cd frame

# Enter Nix development environment (all dependencies included)
nix develop

# Build
meson setup build
ninja -C build

# Run tests/usage
./build/frame ~/Pictures/
```

### Coding Guidelines

- **Language**: C11 (`-std=c11`)
- **Style**: Follow the existing code style (K&R-ish, 4-space indents)
- **Documentation**: All public API functions have header comments
- **Error handling**: Check return values, print errors to stderr
- **Memory**: No memory leaks — use valgrind to verify
- **Thread safety**: The cache module uses mutexes; avoid blocking the main thread

### Commit Messages

Use conventional commits:

```
feat: add support for new image format
fix: correct trash path on non-English locales
docs: update keybinding table
refactor: simplify zoom calculation
```

---

## License

MIT License — see [LICENSE](LICENSE).

Copyright (c) 2026 M57
