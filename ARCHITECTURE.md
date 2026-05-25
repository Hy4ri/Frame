# Frame Architecture

This document describes the internal architecture of Frame, the minimal image viewer for Linux. It is intended for developers who want to understand, modify, or extend the codebase.

---

## Overview

Frame is a single-window, event-driven image viewer written in C11 using SDL3. The application follows a modular design with distinct responsibilities separated into header/source pairs under `src/`.

### High-Level Data Flow

```
┌──────────────┐     keyboard      ┌───────────┐
│   SDL Input  │ ────────────────> │  input.c  │
│  (events)    │                   │ (handler) │
└──────┬───────┘                   └─────┬─────┘
       │                                │
       │ mouse/scroll                   │ commands
       ▼                                ▼
┌──────────────┐     path        ┌───────────┐
│  viewer.c    │ ◄───────────── │  app.c    │
│  (display,   │                │ (state)   │
│   zoom, pan, │                │           │
│   rotation,  │                │ nav cmds  │
│   animation) │                └───────────┘
└──────┬───────┘
       │
       │ loads images via
       ▼
┌──────────────┐     cache hit/miss    ┌───────────┐
│  loader.c    │ ────────────────────> │ cache.c   │
│  (file I/O)  │                      │ (LRU)     │
└──────┬───────┘                      └───────────┘
       │
       │ animated?
       ▼
┌──────────────┐
│  anim.c      │
│  (GIF/APNG)  │
└──────────────┘
```

---

## Module Directory

| Module | Files | Responsibility |
|---|---|---|
| **main** | `main.c` | Entry point, SDL init, main event loop |
| **app** | `app.c`, `app.h` | Image list management, directory scanning, state |
| **viewer** | `viewer.c`, `viewer.h` | Image display, zoom/pan, rotation, animation playback |
| **input** | `input.c`, `input.h` | Keyboard event handling, all keybindings |
| **overlay** | `overlay.c`, `overlay.h` | HUD overlay system (info, help, font rendering) |
| **loader** | `loader.c`, `loader.h` | Image format detection, file loading via SDL3_image |
| **cache** | `cache.c`, `cache.h` | Thread-safe LRU image cache with prefetching |
| **anim** | `anim.c`, `anim.h` | Animated GIF/APNG wrapper around SDL3_image |
| **fileops** | `fileops.c`, `fileops.h` | File operations: trash, rename |
| **exif** | `exif.c`, `exif.h` | EXIF metadata extraction via libexif |
| **utils** | `utils.c`, `utils.h` | Utility functions (file size formatting) |

---

## Detailed Module Breakdown

### 1. Main Loop (`main.c`)

The entry point initializes SDL3 video, creates a resizable window and renderer, then enters the main event loop:

```
Initialize SDL → Create Window → Create Renderer → 
Create AppState → Create Viewer → Init Overlay → 
Load initial directory (if path given) →
Main Loop:
  ├── Poll SDL events
  │   ├── SDL_EVENT_QUIT         → exit
  │   ├── SDL_EVENT_KEY_DOWN     → input_handle_keyboard()
  │   ├── SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED → viewer_handle_resize()
  │   ├── SDL_EVENT_MOUSE_BUTTON_DOWN → viewer_begin_drag()
  │   ├── SDL_EVENT_MOUSE_BUTTON_UP   → viewer_end_drag()
  │   ├── SDL_EVENT_MOUSE_MOTION      → viewer_do_drag()
  │   └── SDL_EVENT_MOUSE_WHEEL       → viewer_scroll_zoom()
  │
  ├── viewer_animation_tick()   // advance animated frames
  ├── viewer_render()            // draw image
  ├── overlay_render()           // draw HUD on top
  └── SDL_RenderPresent() + SDL_Delay(8ms)  // ~120 FPS cap
```

Key design decisions:
- **Single-threaded rendering** — all rendering happens on the main thread
- **~120 FPS cap** via `SDL_Delay(8)` to avoid 100% CPU usage
- **The renderer is borrowed** by the viewer (not owned); it must outlive the viewer

### 2. Application State (`app.c`)

`AppState` manages the list of image files and the current position:

```c
struct AppState {
    char **images;       // NULL-terminated array of full paths
    int count;           // number of entries
    int current_index;   // 0-based, -1 if empty
    char *initial_path;  // from CLI, may be NULL
};
```

**Key behaviors:**
- `app_load_directory()`: Scans a directory for supported image files, sorts them alphabetically via `qsort()`, and sets the current index
- If a file path is given instead of a directory, it extracts the directory, scans it, and positions the index at the specified file
- Navigation functions (`next`, `prev`, `first`, `last`) use `app_display_image()` which clamps to valid range
- `app_remove_current()`: Removes the current image from the list after file deletion (shifts remaining entries left)
- `app_rename_current()`: Updates the path, re-sorts, and repositions the index

**Supported image extensions** (case-insensitive):
`.jpg`, `.jpeg`, `.png`, `.gif`, `.webp`, `.bmp`, `.tiff`, `.tif`, `.ico`, `.apng`

### 3. Image Viewer (`viewer.c`)

`Viewer` manages the displayed image, transformations, and rendering:

```c
struct Viewer {
    SDL_Renderer *renderer;     // borrowed
    SDL_Texture *texture;       // current display texture
    SDL_Surface *original;      // decoded image (rotation=0)
    SDL_Surface *rotated;       // cached rotated version
    int rotation_degrees;       // 0, 90, 180, 270
    float scale;                // 0.1 to 10.0
    float offset_x, offset_y;   // pan offset
    int viewport_w, viewport_h;
    bool needs_fit;
    bool is_animated;
    Animation *animation;        // GIF/APNG state
    int anim_frame;
    Uint64 anim_last_tick;
    ImageCache *cache;           // LRU prefetch cache (capacity 10)
};
```

**Zoom system:**
- Scale ranges from 0.1× to 10.0×
- `viewer_zoom_in/out()`: Zoom ±5% from viewport center
- `viewer_scroll_zoom()`: Zoom toward mouse cursor position
- `viewer_zoom_fit()`: Scale to fit viewport (preserving aspect ratio)
- `viewer_zoom_original()`: 1:1 pixel mapping, centered if smaller than viewport

**Rotation system:**
- Rotation is applied to a cached copy of the original surface
- Manual pixel manipulation for 90°, 180°, 270° rotation (RGBA8888 format)
- `viewer_apply_rotation()`: Rotates the surface and creates a new texture
- Rotation is disabled for animated images (too complex)

**Animation system:**
- `viewer_load_image()`: Tries animation first via `anim_load()`, falls back to static
- `viewer_animation_tick()`: Called once per frame, advances animation based on frame delay
- Animation wraps around infinitely
- Minimum frame delay of 10ms to prevent CPU spin

**Prefetch cache:**
- LRU cache with capacity of 10 entries
- `viewer_prefetch()`: Loads images in the background for instant navigation
- In `input.c`, `prefetch_around()` preloads 3 images ahead and 3 behind

### 4. Input Handling (`input.c`)

The input module translates SDL keyboard events into application actions:

**Keybinding architecture:**
- The `gg` double-tap sequence is tracked with a 500ms window using `SDL_GetTicks()`
- If an overlay is active, any key dismisses it without taking normal action
- Dialog operations (delete, rename) use `zenity` via `popen()`
- The delete flow: confirm → `fileops_trash()` → `viewer_clear()` → `app_remove_current()` → load next image
- The rename flow: validation → `fileops_rename()` → `app_rename_current()` → reload image
- The info flow: gather file stats + EXIF data → format string → `overlay_show_info()`

**Dialog integration:**
- Delete: `zenity --question` with custom OK/Cancel labels
- Rename: `zenity --entry` with pre-filled current filename
- Both use `popen()` with 2>/dev/null to suppress output

### 5. Overlay System (`overlay.c`)

The overlay module renders HUD information on top of the image:

- **Font loading**: Searches multiple common paths for DejaVuSans.ttf or LiberationSans-Regular.ttf
- **Text rendering**: Uses `TTF_RenderText_Blended_Wrapped` for word-wrapped text
- **Layout**: Centered semi-transparent black background with border, title above body
- **Textures are cached** and rebuilt only when viewport size changes
- The help overlay is a static string built at compile time

### 6. Image Loader (`loader.c`)

- `loader_load_static()`: Wraps `IMG_Load()` from SDL3_image
- `loader_load_texture()`: Convenience function (load + convert to texture)
- `loader_is_supported()`: Checks file extension against supported list
- `loader_is_animated()`: Returns true for .gif and .apng

### 7. LRU Cache (`cache.c`)

Thread-safe least-recently-used cache for decoded image surfaces:

- **Capacity**: Configurable (viewer uses 10), max 100 (sanity limit)
- **Thread safety**: Protected by `pthread_mutex_t`
- **Eviction policy**: When full, the least-recently-used entry is evicted (entry at `order[0]`)
- **Cache entry**: Stores path + SDL_Surface; cache takes ownership of surfaces
- `cache_put()`: If path exists, replace surface and touch. If full, evict LRU.
- `cache_get()`: Return surface and touch (move to MRU position)
- `cache_invalidate()`: Remove specific entry (used on file deletion/rename)

### 8. Animation (`anim.c`)

Wraps SDL3_image's `IMG_Animation`:

- `anim_load()`: Calls `IMG_LoadAnimation()`, returns NULL if 1 frame or less
- `anim_get_frame()`: Returns borrowed surface pointer (do not free)
- `anim_get_delay()`: Returns frame delay in ms (minimum 10ms)
- The viewer duplicates frames from the animation because it needs to apply rotation

### 9. File Operations (`fileops.c`)

**Trash implementation** (freedesktop.org Trash specification):

1. Creates `~/.local/share/Trash/files/` and `~/.local/share/Trash/info/` if needed
2. Handles name collisions by appending `_2`, `_3`, etc.
3. Writes a `.trashinfo` file with original path and deletion date (ISO 8601)
4. Uses a temporary file + rename for atomic `.trashinfo` creation
5. Moves the file via `rename()` (same filesystem)

**Rename implementation:**
1. Builds new full path from directory + new name
2. Checks if destination already exists (fails if so)
3. Calls `rename()`

### 10. EXIF Extraction (`exif.c`)

Uses libexif to extract metadata from `IFD_0`, `IFD_EXIF`:

| EXIF Tag | Display |
|---|---|
| `EXIF_TAG_MAKE` | Camera manufacturer (e.g. "Canon") |
| `EXIF_TAG_MODEL` | Camera model (e.g. "EOS R5") |
| `EXIF_TAG_DATE_TIME_ORIGINAL` | Date and time taken |
| `EXIF_TAG_EXPOSURE_TIME` | Shutter speed (e.g. "1/125s") |
| `EXIF_TAG_FNUMBER` | Aperture (e.g. "f/2.8") |
| `EXIF_TAG_ISO_SPEED_RATINGS` | ISO sensitivity |
| `EXIF_TAG_ORIENTATION` | Image orientation |
| `EXIF_TAG_FOCAL_LENGTH` | Lens focal length |
| `EXIF_TAG_FLASH` | Flash firing status |

Returns a dynamically allocated string (caller must free), or NULL if no EXIF data.

---

## Build System

Frame uses **Meson** as the primary build system with a **Makefile** as an alternative.

### Meson (`meson.build`)

```python
project('frame', 'c', version: '0.9.0', default_options: ['c_std=c11'])
```

Dependencies: `sdl3`, `sdl3-image`, `sdl3-ttf`, `libexif`, `threads`, `math`

### Nix Flake (`flake.nix`)

Provides:
- `packages.default`: Production build
- `devShells.default`: Development shell with clang-tools, zenity, all SDL3 libs
- `overlays.default`: NixOS overlay for system integration

### Desktop Integration (`frame.desktop`)

Registered MIME types:
`image/jpeg`, `image/png`, `image/gif`, `image/webp`, `image/bmp`, `image/tiff`, `image/vnd.microsoft.icon`, `image/apng`

---

## Conventions

### Memory Management
- **Caller owns returned allocations** — functions returning `char*` or `SDL_Surface*` transfer ownership to the caller
- **Cache owns stored surfaces** — the cache takes ownership of surfaces passed to `cache_put()` and frees them on eviction
- **Borrowed pointers** — the viewer borrows the renderer pointer; overlay text textures are owned by the overlay module

### Error Handling
- All errors are printed to `stderr` with descriptive messages
- Functions return NULL, false, or -1 on failure
- Allocations are checked; failures are handled gracefully (not panicked)

### Thread Safety
- Only the cache module uses threads (mutex protection)
- The main loop is single-threaded
- Prefetching happens synchronously on the main thread (simple and safe)

---

## Extending Frame

### Adding a New Image Format

1. Add the extension to `supported_extensions[]` in both `app.c` and `loader.c`
2. Add a format name mapping in `utils.c`'s `format_from_ext()`
3. If SDL3_image doesn't support it natively, implement loading in `loader.c`
4. Add the MIME type to `frame.desktop`

### Adding a New Keybinding

1. Add the key handling in `input.c`'s `input_handle_keyboard()`
2. If adding a new action, add a new function to the relevant module (viewer, app, fileops)
3. Update the help string in `overlay.c`'s `overlay_show_help()`
4. Update the keybinding table in `README.md`

### Adding a New Overlay

1. Add a `overlay_show_*()` function in `overlay.c`
2. Update `overlay.h` with the new declaration
3. The overlay will be rendered automatically in the main loop

### Adding New EXIF Fields

1. Add `exif_content_get_entry()` calls in `exif.c`
2. Append the formatted value to the result string
