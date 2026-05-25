# Changelog

All notable changes to Frame will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.0] - 2026-05-25

### Added

- Initial public release
- Image viewer with SDL3 rendering (Wayland + X11)
- Vim-style keyboard navigation: `h`/`j`/`k`/`l`, `gg`, `G`
- Mouse wheel zoom (cursor-aware) and click-drag panning
- Image rotation (90° clockwise and counter-clockwise)
- Image deletion via XDG Trash specification with zenity confirmation
- Image rename with zenity entry dialog
- Image information overlay (filename, size, dimensions, format, date, EXIF data)
- Keyboard shortcuts help overlay (`?`)
- Animated GIF and APNG playback via SDL3_image
- Background prefetch LRU cache for instant image navigation
- Fullscreen toggle (`f`)
- Zoom to fit (`0`) and original 1:1 size (`1`)
- Format support: JPEG, PNG, GIF, APNG, WebP, BMP, TIFF, ICO
- EXIF metadata extraction via libexif (make, model, date, exposure, aperture, ISO, orientation, focal length, flash)
- Desktop file integration with MIME type registration
- Meson build system with Nix flake support
- Makefile as alternative build system
- Dark background (30, 30, 30) for distraction-free viewing
- Window title showing current filename and position (e.g., `image.jpg (3/42) - Frame`)
- Automatic directory scanning and alphabetical sorting
- Maximum image dimension enforcement (16384px) to prevent OOM
- ~120 FPS frame rate cap with SDL_Delay
