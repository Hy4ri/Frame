# Frame

> A fast, minimal image viewer for Linux with vim keybindings, rewritten in Rust using [GPUI](https://github.com/zed-industries/zed/tree/main/crates/gpui) and [gpui-kit](https://crates.io/crates/gpui-kit).

<p align="center">
  <img src="assets/frame.svg" alt="Frame Icon" width="128" height="128">
</p>

<div align="center">

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-2.0.0-green.svg)](Cargo.toml)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)](https://github.com/Hy4ri/frame)
[![Built with GPUI](https://img.shields.io/badge/UI-GPUI-blueviolet)](https://github.com/zed-industries/zed/tree/main/crates/gpui)

</div>

---

## Features

- **Minimal Interface** — Clean, GPU-accelerated distraction-free viewing with a dark background
- **Vim Keybindings** — Navigate with `h`/`j`/`k`/`l`, `gg`, `G`
- **Image Navigation** — Previous/next, first/last, scroll wheel, arrow keys with debounce
- **Zoom & Pan** — Mouse wheel zoom (cursor-aware), click-and-drag panning
- **Rotation** — 90° clockwise and counter-clockwise
- **Image Ops** — Delete (move to trash via XDG trash with confirmation dialog), rename dialog with live input
- **Fuzzy Search Grid** — Full-screen 5x5 scrollable thumbnail search menu with live fuzzy matching, activated by `/`
- **Image Info** — Dimensions, file size, format, and EXIF metadata overlay
- **Format Support** — JPEG, PNG, GIF, APNG, WebP, BMP, TIFF, ICO via `image` crate
- **Animated Images** — Multi-frame GIF and APNG playback
- **Multi-Core Prefetching** — Parallel background decoding and thumbnail generation powered by Rayon
- **Live Directory Watching** — Inotify filesystem watching via `notify` crate; automatically reflects added, removed, or renamed files in real-time
- **Smart Caching** — Dual-tier LRU cache (128 MB full image cache, 32 MB thumbnail cache)
- **Fullscreen** — Toggle with `f`
- **Wayland + X11** — Native Linux rendering via Vulkan / GPU acceleration

---

## Installation

### Nix (Recommended)

```bash
# Run directly with Nix
nix run github:Hy4ri/frame -- /path/to/image.jpg

# Or in a local clone
nix run . -- /path/to/image.jpg
```

### Build from Source

**With Nix (development):**
```bash
nix develop
cargo build --release
./target/release/frame /path/to/image.jpg
```

**Without Nix:**
Ensure Linux graphics and windowing development libraries are installed (`libxcb`, `libxkbcommon`, `wayland`, `vulkan-loader`, `fontconfig`, `freetype`).

```bash
cargo build --release
./target/release/frame /path/to/image.jpg
```

---

## Usage

```bash
frame /path/to/image.jpg   # Open a specific image
frame /path/to/images/     # Open a directory (all supported images, sorted)
frame -v | --version       # Print version information
frame -h | --help          # Show usage & options
```

Frame scans the directory for all supported image files, sorts them, and displays the first (or specified) image. Window title shows `filename (N/M) - Frame`.

---

## Keybindings

| Key | Action |
|-----|--------|
| `h` / `←`, `k` / `↑` | Previous image |
| `l` / `→`, `j` / `↓` | Next image |
| `gg` (double-tap) | First image |
| `G` (Shift+`g`) | Last image |
| `f` | Toggle fullscreen |
| `+` / `=` / `z` | Zoom in |
| `-` / `x` | Zoom out |
| `0` | Fit to window |
| `1` | Original size (1:1) |
| Scroll wheel | Zoom toward cursor |
| Click + drag | Pan image |
| `r`, `R` | Rotate CW / CCW |
| `d` | Delete (move to trash) |
| `F2` | Rename |
| `/` | Open image search grid |
| `i` | Show image info overlay |
| `?` | Show keyboard shortcuts |
| `q` / `Esc` | Quit |

---

## Testing

Run the test suite:
```bash
nix develop --command cargo test
```

---

## License

MIT License — see [LICENSE](LICENSE).

Copyright (c) 2026 M57
