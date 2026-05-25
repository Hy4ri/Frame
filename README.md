# Frame

A fast, minimal image viewer for Linux with vim keybindings, built with C and [SDL3](https://wiki.libsdl.org/SDL3).

![Frame Icon](assets/frame.svg)

## Features

- **Minimal Interface** — Clean, distraction-free viewing experience
- **Vim Keybindings** — Navigate images using familiar vim keys
- **Image Operations** — Delete, rotate, rename images
- **Image Info** — View dimensions, file size, format, and EXIF data
- **Dark Background** — Easy on the eyes
- **Zoom & Pan** — Mouse wheel zoom, click-and-drag panning
- **Format Support** — JPEG, PNG, GIF (animated), APNG (animated), WebP, BMP, TIFF, ICO
- **Wayland + X11** — Runs natively on both via SDL3

## Installation

### Nix (Recommended)

**Run directly:**
```bash
nix run github:Hy4ri/frame -- /path/to/image.jpg
```

**Add to your system (NixOS):**
```nix
# flake.nix
{
  inputs.frame.url = "github:Hy4ri/frame";
}

# configuration.nix
environment.systemPackages = [ inputs.frame.packages.${pkgs.system}.default ];

# Or use the overlay:
nixpkgs.overlays = [ inputs.frame.overlays.default ];
environment.systemPackages = [ pkgs.frame ];
```

### Build from Source

```bash
# With Nix
nix develop
meson setup build
ninja -C build

# Without Nix (requires SDL3 dev libraries)
meson setup build
ninja -C build
```

Dependencies: SDL3, SDL3_image, SDL3_ttf, libexif, zenity (for dialogs).

## Usage

```bash
# Open an image
frame /path/to/image.jpg

# Open a directory (view all images)
frame /path/to/images/
```

## Keybindings

### Navigation & View

| Key | Action |
|-----|--------|
| `h` / `←` | Previous image |
| `l` / `→` | Next image |
| `j` / `↓` | Next image |
| `k` / `↑` | Previous image |
| `gg` | First image |
| `G` | Last image |
| `f` | Toggle fullscreen |
| `+` / `=` / `z` | Zoom in |
| `-` / `x` | Zoom out |
| `0` | Fit to window |
| `1` | Original size (1:1) |
| `Scroll` | Zoom in/out |
| `Drag` | Pan image |

### Image Operations

| Key | Action |
|-----|--------|
| `r` | Rotate clockwise 90° |
| `R` | Rotate counter-clockwise 90° |
| `d` / `Del` | Delete image (move to trash) |
| `F2` | Rename image |
| `i` | Show image info |

### General

| Key | Action |
|-----|--------|
| `?` | Show help |
| `q` / `Esc` | Quit |

## License

MIT License
