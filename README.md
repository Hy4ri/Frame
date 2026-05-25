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

- **Minimal Interface** — Clean, distraction-free viewing with a dark background
- **Vim Keybindings** — Navigate with `h`/`j`/`k`/`l`, `gg`, `G`
- **Image Navigation** — Previous/next, first/last, scroll wheel, arrow keys
- **Zoom & Pan** — Mouse wheel zoom (cursor-aware), click-and-drag panning
- **Rotation** — 90° clockwise and counter-clockwise
- **Image Ops** — Delete (move to trash with SDL confirmation dialog), rename via SDL entry dialog
- **Image Info** — Dimensions, file size, format, EXIF data overlay
- **Format Support** — JPEG, PNG, GIF, APNG, WebP, BMP, TIFF, ICO
- **Animated Images** — Full GIF and APNG animation playback
- **Smart Caching** — LRU cache with background prefetching for instant navigation
- **Fullscreen** — Toggle with `f`
- **Desktop Integration** — Installable via `.desktop` file with MIME type support
- **Wayland + X11** — Runs natively on both via SDL3

---

## Installation

### Nix (Recommended)

```bash
# Run directly (no install)
nix run github:Hy4ri/frame -- /path/to/image.jpg

# Or add to flake.nix, then use inputs.frame.packages.${system}.default
```

### Build from Source

**With Nix (development):**
```bash
nix develop
meson setup build && ninja -C build
./build/frame /path/to/image.jpg
```

**Without Nix:**
```bash
sudo apt install libsdl3-dev libsdl3-image-dev libsdl3-ttf-dev libexif-dev
meson setup build && ninja -C build
./build/frame /path/to/image.jpg
```

**Using Makefile:**
```bash
make && ./frame /path/to/image.jpg
```

### Dependencies

| Dependency | Purpose | Required |
|---|---|---|
| [SDL3](https://wiki.libsdl.org/SDL3) | Windowing, rendering, input | Yes |
| [SDL3_image](https://github.com/libsdl-org/SDL_image) | Image format loading | Yes |
| [SDL3_ttf](https://github.com/libsdl-org/SDL_ttf) | Font rendering for overlays | Yes |
| [libexif](https://github.com/libexif/libexif) | EXIF metadata extraction | Yes |
| Meson / Ninja | Build system | Build only |
| pkg-config | Dependency discovery | Build only |

---

## Usage

```bash
frame /path/to/image.jpg   # Open a specific image
frame /path/to/images/     # Open a directory (all supported images, sorted)
frame                       # Show usage message
```

Frame scans the directory for all supported image files, sorts them alphabetically, and displays the first (or specified) image. Window title shows `filename (N/M) - Frame`.

---

## Keybindings

| Key | Action |
|-----|--------|
| `h`/`←`, `k`/`↑` | Previous image |
| `l`/`→`, `j`/`↓` | Next image |
| `gg` (double-tap) | First image |
| `G` (Shift+`g`) | Last image |
| `f` | Toggle fullscreen |
| `+`/`=`/`z`, `-`/`x` | Zoom in / out |
| `0` | Fit to window |
| `1` | Original size (1:1) |
| Scroll wheel | Zoom toward cursor |
| Click + drag | Pan image |
| `r`, `R` | Rotate CW / CCW |
| `d` / `Del` | Delete (move to trash) |
| `F2` | Rename |
| `i` | Show image info overlay |
| `?` | Show keyboard shortcuts |
| `q` / `Esc` | Quit |

**Any key dismisses an active overlay** without performing its normal action.

---

## Troubleshooting

| Problem | Solution |
|---|---|
| **"SDL_Init failed"** | Ensure SDL3 is installed and a display server (Wayland/X11) is running. |
| **No images found** | Only supported extensions are scanned: `.jpg`, `.jpeg`, `.png`, `.gif`, `.webp`, `.bmp`, `.tiff`, `.tif`, `.ico`, `.apng`. |
| **No overlays shown** | Frame needs DejaVuSans.ttf or LiberationSans-Regular.ttf. Install `fonts-dejavu-core` or `liberation-fonts`. |
| **Animation not playing** | Only GIF and APNG support animation. Some files may be static variants. |

---

## License

MIT License — see [LICENSE](LICENSE).

Copyright (c) 2026 M57

See [ARCHITECTURE.md](ARCHITECTURE.md) for a detailed module breakdown.
