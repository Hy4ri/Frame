#define _GNU_SOURCE
#include "font.h"

#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

/* ------------------------------------------------------------------
 * Fallback font list - single source of truth.
 * Previously duplicated in overlay.c and search.c (DRY violation).
 * Includes original DejaVu/Liberation paths plus modern Noto fallbacks.
 * ------------------------------------------------------------------ */
static const char *fallback_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/run/current-system/sw/share/X11/fonts/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/TTF/NotoSans-Regular.ttf",
    "/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf",
    NULL
};

static bool file_is_readable(const char *path)
{
    if (!path) {
        return false;
    }
    return access(path, R_OK) == 0;
}

#ifdef HAVE_FONTCONFIG

/* Resolve "sans-serif" via fontconfig. Returns malloc'd path or NULL. */
static char *fontconfig_resolve_sans(void)
{
    FcConfig *config = FcInitLoadConfigAndFonts();
    if (!config) {
        config = FcConfigGetCurrent();
        if (!config) return NULL;
    }
    FcPattern *pat = FcNameParse((FcChar8 *)"sans-serif");
    if (!pat) {
        pat = FcPatternCreate();
        if (!pat) return NULL;
    }
    FcConfigSubstitute(config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult result = FcResultNoMatch;
    FcPattern *matched = FcFontMatch(config, pat, &result);
    FcPatternDestroy(pat);
    if (!matched || result != FcResultMatch) {
        if (matched) FcPatternDestroy(matched);
        return NULL;
    }
    FcChar8 *file = NULL;
    if (FcPatternGetString(matched, FC_FILE, 0, &file) != FcResultMatch || !file) {
        FcPatternDestroy(matched);
        return NULL;
    }
    if (!file_is_readable((const char *)file)) {
        FcPatternDestroy(matched);
        return NULL;
    }
    char *dup = strdup((const char *)file);
    FcPatternDestroy(matched);
    return dup;
}

#endif /* HAVE_FONTCONFIG */

char *font_get_system_path(void)
{
#ifdef HAVE_FONTCONFIG
    char *fc_path = fontconfig_resolve_sans();
    if (fc_path) {
        return fc_path;
    }
#endif

    for (int i = 0; fallback_paths[i]; i++) {
        const char *candidate = fallback_paths[i];
        if (!file_is_readable(candidate)) {
            continue;
        }
        char *dup = strdup(candidate);
        if (dup) {
            return dup;
        }
    }

    return NULL;
}

TTF_Font *font_open_system_font(float ptsize)
{
    if (ptsize <= 0.0f) {
        return NULL;
    }

    char *path = font_get_system_path();
    if (!path) {
        return NULL;
    }

    TTF_Font *font = TTF_OpenFont(path, ptsize);
    free(path);
    return font;
}
