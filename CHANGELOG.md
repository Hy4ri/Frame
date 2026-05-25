# Changelog

All notable changes to Frame will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-05-25

### Added

- Initial public release
- Image viewer with SDL3 rendering (Wayland + X11)
- Vim-style keyboard navigation: `h`/`j`/`k`/`l`, `gg`, `G`
- Mouse wheel zoom (cursor-aware) and click-drag panning
- Image rotation (90° CW and CCW)
- Image deletion via XDG Trash specification with SDL confirmation dialog
- Image rename with SDL entry dialog
- Image information overlay (filename, size, dimensions, format, date, EXIF data)
- Keyboard shortcuts help overlay (`?`)
- Animated GIF and APNG playback via SDL3_image
- Background prefetch LRU cache for instant image navigation
- Fullscreen toggle (`f`), zoom to fit (`0`), original 1:1 size (`1`)
- Format support: JPEG, PNG, GIF, APNG, WebP, BMP, TIFF, ICO
- EXIF metadata extraction via libexif
- Desktop file integration with MIME type registration
- Meson build system with Nix flake support
- Makefile as alternative build system
- Dark background for distraction-free viewing
- Window title showing current filename and position
- Automatic directory scanning and alphabetical sorting
- Maximum image dimension enforcement (16384px) to prevent OOM
- ~120 FPS frame rate cap
