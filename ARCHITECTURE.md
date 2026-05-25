# Frame Architecture

This document describes the internal architecture of Frame, the minimal image viewer for Linux. It is intended for developers who want to understand, modify, or extend the codebase.

---

## Overview

Frame is a single-window, event-driven image viewer written in C11 using SDL3. The application follows a modular design with distinct responsibilities separated into header/source pairs under `src/`.

### Data Flow

```
SDL Input Events → input.c (keybindings) → app.c (state management)
                                         → viewer.c (display, zoom, pan, rotation, animation)
                                              → loader.c (file I/O) → cache.c (LRU)
                                              → anim.c (GIF/APNG)
```

- **Main thread** handles all rendering, input, and prefetching
- **~120 FPS cap** via `SDL_Delay(8)` to avoid 100% CPU usage

---

## Module Directory

| Module | Files | Responsibility |
|---|---|---|
| **main** | `main.c` | Entry point, SDL init, main event loop |
| **app** | `app.c`, `app.h` | Image list management, directory scanning, state |
| **viewer** | `viewer.c`, `viewer.h` | Image display, zoom/pan, rotation, animation playback |
| **input** | `input.c`, `input.h` | Keyboard event handling, all keybindings |
| **overlay** | `overlay.c`, `overlay.h` | HUD overlay system (info, help, modal dialogs), font rendering |
| **loader** | `loader.c`, `loader.h` | Image format detection, file loading via SDL3_image |
| **cache** | `cache.c`, `cache.h` | Thread-safe LRU image cache with prefetching |
| **anim** | `anim.c`, `anim.h` | Animated GIF/APNG wrapper around SDL3_image |
| **fileops** | `fileops.c`, `fileops.h` | File operations: trash (XDG Trash spec), rename |
| **exif** | `exif.c`, `exif.h` | EXIF metadata extraction via libexif |
| **utils** | `utils.c`, `utils.h` | Utility functions (file size formatting) |

---

## Detailed Module Breakdown

### 1. Main Loop (`main.c`)

The entry point initializes SDL3 video, creates a resizable window and renderer, then enters the main event loop. Events are polled and dispatched to the appropriate module. The viewer's renderer is borrowed (not owned), so it must outlive the viewer.

### 2. Application State (`app.c`)

`AppState` manages the list of image files (a NULL-terminated array of paths) and the current 0-based index. Key behaviors:

- `app_load_directory()`: Scans a directory for supported image files, sorts alphabetically via `qsort()`, sets current index
- If a file path is given, extracts the directory, scans it, and positions the index at the specified file
- Navigation functions clamp to valid range
- `app_remove_current()`: Removes the current image after deletion (shifts entries left)
- `app_rename_current()`: Updates the path, re-sorts, repositions the index

**Supported extensions** (case-insensitive):
`.jpg`, `.jpeg`, `.png`, `.gif`, `.webp`, `.bmp`, `.tiff`, `.tif`, `.ico`, `.apng`

### 3. Image Viewer (`viewer.c`)

Manages the displayed image, transformations, and rendering:

- **Zoom**: Scale 0.1×–10.0×, ±5% steps, cursor-aware scroll zoom, fit-to-window, original 1:1
- **Rotation**: 90°, 180°, 270° via manual pixel manipulation (RGBA8888), cached rotated surface, disabled for animations
- **Animation**: Tries `anim_load()` first, falls back to static. Advanced once per frame via `viewer_animation_tick()`. Minimum 10ms frame delay.
- **Prefetch cache**: LRU cache (capacity 10), loads 3 images ahead and 3 behind for instant navigation

### 4. Input Handling (`input.c`)

Translates SDL keyboard events into application actions:

- `gg` double-tap tracked with 500ms window via `SDL_GetTicks()`
- Overlays dismissed by any key press
- Delete flow: `overlay_modal_confirm()` → `fileops_trash()` → `viewer_clear()` → `app_remove_current()` → load next
- Rename flow: `overlay_modal_entry()` → validate → `fileops_rename()` → `app_rename_current()` → reload
- Info flow: gather file stats + EXIF data → `overlay_show_info()`

### 5. Overlay System (`overlay.c`)

Renders HUD on top of the image using SDL3_ttf:

- Font loading searches multiple paths for `DejaVuSans.ttf` or `LiberationSans-Regular.ttf`
- Text rendered via `TTF_RenderText_Blended_Wrapped`, center-aligned on semi-transparent background
- Textures cached and rebuilt only on viewport resize
- **Modal dialogs**: `overlay_modal_confirm()` for delete confirmation, `overlay_modal_entry()` for rename input — both rendered in-SDL3, no external dependencies

### 6. Image Loader (`loader.c`)

Wraps `IMG_Load()` from SDL3_image. Provides `loader_load_static()`, `loader_load_texture()`, `loader_is_supported()`, and `loader_is_animated()` (returns true for `.gif` and `.apng`).

### 7. LRU Cache (`cache.c`)

Thread-safe LRU cache for decoded SDL surfaces:

- Protected by `pthread_mutex_t`
- Eviction: least-recently-used entry dropped when full (max capacity 100)
- `cache_put()`: Replace surface if path exists, otherwise evict LRU on overflow
- `cache_get()`: Return surface and move to MRU position
- `cache_invalidate()`: Remove specific entry (used on delete/rename)

### 8. Animation (`anim.c`)

Wraps SDL3_image's `IMG_Animation`:

- `anim_load()`: Returns NULL if 1 frame or less
- `anim_get_frame()`: Returns borrowed surface pointer (do not free)
- `anim_get_delay()`: Returns frame delay in ms (minimum 10ms)
- Viewer duplicates frames from the animation for rotation support

### 9. File Operations (`fileops.c`)

**Trash**: Follows freedesktop.org Trash specification. Creates `~/.local/share/Trash/files/` and `info/` directories, handles name collisions with `_2`, `_3` suffixes, writes `.trashinfo` with ISO 8601 deletion date, uses temp file + rename for atomicity.

**Rename**: Builds new full path from directory + new name, checks for existing destination, calls `rename()`.

### 10. EXIF Extraction (`exif.c`)

Uses libexif to extract metadata from `IFD_0` and `IFD_EXIF`. Returns a dynamically allocated string (caller must free), or NULL if no EXIF data. Fields: camera make/model, datetime, exposure time, aperture (f-number), ISO, orientation, focal length, flash status.

---

## Build System

Frame uses **Meson** as the primary build system with a **Makefile** as an alternative.

Dependencies: `sdl3`, `sdl3-image`, `sdl3-ttf`, `libexif`, `threads`, `math`

### Nix Flake (`flake.nix`)

Provides:
- `packages.default`: Production build
- `devShells.default`: Development shell with clang-tools, all SDL3 libs
- `overlays.default`: NixOS overlay for system integration

### Desktop Integration (`frame.desktop`)

Registered MIME types: `image/jpeg`, `image/png`, `image/gif`, `image/webp`, `image/bmp`, `image/tiff`, `image/vnd.microsoft.icon`, `image/apng`

---

## Conventions

### Memory Management
- **Caller owns returned allocations** — functions returning `char*` or `SDL_Surface*` transfer ownership
- **Cache owns stored surfaces** — cache frees surfaces on eviction
- **Borrowed pointers** — viewer borrows the renderer pointer

### Error Handling
- All errors printed to `stderr` with descriptive messages
- Functions return NULL, false, or -1 on failure
- Allocations are checked; failures handled gracefully

### Thread Safety
- Only the cache module uses mutexes
- Main loop is single-threaded
- Prefetching happens synchronously on the main thread
