#ifndef FRAME_FONT_H
#define FRAME_FONT_H

#include <SDL3_ttf/SDL_ttf.h>

/**
 * font_get_system_path - Resolve the preferred system font file.
 *
 * WHY: Desktop environments expose the user's chosen UI font via
 * fontconfig. Querying fontconfig (family "sans-serif") returns the
 * actual configured font instead of blindly trying hardcoded DejaVu
 * paths that may not exist on NixOS / minimal containers / other
 * distros. Hardcoded paths are retained as fallback for robustness
 * when fontconfig is unavailable or yields no result.
 *
 * Returns: heap-allocated string containing absolute path to a
 *   readable TTF file. Caller must free() it. Returns NULL if no
 *   suitable font was found (either via fontconfig or fallback list).
 */
char *font_get_system_path(void);

/**
 * font_open_system_font - Open the system font at given point size.
 *
 * Convenience wrapper around font_get_system_path() + TTF_OpenFont().
 * Resolves the system font path, attempts to open it at @ptsize,
 * frees the temporary path string and returns the font handle.
 *
 * @ptsize: Desired point size (must be > 0).
 *
 * Returns: TTF_Font* on success, NULL on failure (no font found or
 *   TTF_OpenFont failed). Caller must TTF_CloseFont() when done.
 */
TTF_Font *font_open_system_font(float ptsize);

#endif /* FRAME_FONT_H */
