# Frame

A fast, minimal image viewer for Linux with vim keybindings, built with Go and GTK4.

![Frame Icon](assets/frame.svg)

## Features

- **Minimal Interface** - Clean, distraction-free viewing experience
- **Vim Keybindings** - Navigate images using familiar vim keys
- **Image Operations** - Delete, rotate, rename images
- **Image Info** - View dimensions, file size, format, and EXIF data
- **Dark Theme** - Easy on the eyes
- **Format Support** - JPEG, PNG, GIF (animated), WebP, BMP, TIFF, SVG, ICO

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
go build -o frame .

# Without Nix (requires GTK4 dev libraries)
go build -o frame .
```

## Usage

```bash
# Open an image
frame /path/to/image.jpg

# Open a directory (view all images)
frame /path/to/images/

# Open file chooser
frame
```

## Keybindings

| Key | Action |
|-----|--------|
| `h` / `←` | Previous image |
| `l` / `→` | Next image |
| `j` / `↓` | Next image |
| `k` / `↑` | Previous image |
| `gg` | First image |
| `G` | Last image |
| `f` | Toggle fullscreen |
| `+` / `=` | Zoom in |
| `-` | Zoom out |
| `0` | Fit to window |
| `1` | Original size (1:1) |
| `r` | Rotate clockwise 90° |
| `R` | Rotate counter-clockwise 90° |
| `d` / `Del` | Delete image (move to trash) |
| `F2` | Rename image |
| `i` | Show image info |
| `?` | Show help |
| `q` / `Esc` | Quit |

## License

MIT License
