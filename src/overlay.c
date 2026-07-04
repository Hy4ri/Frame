#define _GNU_SOURCE
#include "overlay.h"
#include "viewer.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *font_paths[] = {
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/run/current-system/sw/share/X11/fonts/DejaVuSans.ttf",
    NULL
};

static TTF_Font *body_font = NULL;
static TTF_Font *title_font = NULL;
static TTF_Font *help_font = NULL;
static bool active = false;
static SDL_Texture *text_texture = NULL;
static SDL_Texture *title_texture = NULL;
static int text_w = 0, text_h = 0;
static int title_w = 0, title_h = 0;
static char *current_title = NULL;
static char *current_body = NULL;
static int viewport_w = 0, viewport_h = 0;
static bool entry_mode_active = false;

typedef struct {
    const char *key;
    const char *desc;
} HelpShortcut;

static HelpShortcut help_nav[] = {
    {"h / \xe2\x86\x90", "Previous image"},
    {"l / \xe2\x86\x92", "Next image"},
    {"j / \xe2\x86\x93", "Next image (alt)"},
    {"k / \xe2\x86\x91", "Previous image (alt)"},
    {"gg", "First image"},
    {"G", "Last image"}
};

static HelpShortcut help_view[] = {
    {"f", "Toggle fullscreen"},
    {"+ / = / z", "Zoom in"},
    {"- / x", "Zoom out"},
    {"0", "Fit to window"},
    {"1", "Original size (1:1)"},
    {"Scroll", "Zoom with wheel"},
    {"Drag", "Pan with mouse"}
};

static HelpShortcut help_ops[] = {
    {"r", "Rotate CW 90\xc2\xb0"},
    {"R", "Rotate CCW 90\xc2\xb0"},
    {"d / Del", "Delete image"},
    {"F2", "Rename image"},
    {"i", "Show image info"}
};

static HelpShortcut help_gen[] = {
    {"/", "Search images grid"},
    {"?", "Show this help"},
    {"q / Esc", "Quit"}
};

/* For entry dialog */
static char entry_buffer[512] = {0};  /* text being edited */
static int entry_cursor = 0;          /* cursor position (not visually rendered, just logical) */

bool overlay_init(void)
{
    if (!TTF_Init()) {
        fprintf(stderr, "TTF_Init failed: %s\n", SDL_GetError());
        return false;
    }

    for (int i = 0; font_paths[i]; i++) {
        body_font = TTF_OpenFont(font_paths[i], 14.0f);
        if (body_font) {
            title_font = TTF_OpenFont(font_paths[i], 22.0f);
            help_font = TTF_OpenFont(font_paths[i], 16.0f);
            if (!title_font) {
                title_font = body_font;
            }
            if (!help_font) {
                help_font = body_font;
            }
            break;
        }
    }

    if (!body_font) {
        fprintf(stderr, "No font found, overlays disabled\n");
        TTF_Quit();
        return false;
    }

    if (!title_font) {
        title_font = body_font; /* fallback */
    }
    if (!help_font) {
        help_font = body_font;
    }

    return true;
}

/* ================================================================
   Confirm dialog
   ================================================================ */

bool overlay_modal_confirm(const char *title_text, const char *message,
                           SDL_Renderer *renderer, struct Viewer *viewer) {
    if (!body_font || !title_font) {
        /* No fonts loaded — can't render dialog. Default to cancel. */
        return false;
    }

    /* Build full text: message + hint */
    char full_text[2048];
    snprintf(full_text, sizeof(full_text), "%s\n\n[Enter] Confirm    [Esc] Cancel",
             message ? message : "");

    overlay_show_info(title_text ? title_text : "Confirm", full_text);
    active = true;

    /* Inner event loop — block until user dismisses */
    SDL_Event e;
    while (active) {
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                overlay_hide();
                return false;

            case SDL_EVENT_KEY_DOWN:
                switch (e.key.key) {
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    overlay_hide();
                    return true;
                case SDLK_ESCAPE:
                    overlay_hide();
                    return false;
                default:
                    break;
                }
                break;

            default:
                break;
            }
        }

        /* Render the viewer background first to prevent flickering */
        if (viewer) {
            viewer_render(viewer, renderer);
        }
        /* Render the overlay on top of the current frame */
        overlay_render(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }

    return false;
}

/* ================================================================
   Entry dialog
   ================================================================ */

char *overlay_modal_entry(const char *title_text, const char *initial_text,
                          SDL_Renderer *renderer, SDL_Window *window, struct Viewer *viewer) {
    if (!body_font || !title_font) {
        return NULL;
    }

    /* Initialize entry buffer */
    if (initial_text) {
        strncpy(entry_buffer, initial_text, sizeof(entry_buffer) - 1);
    } else {
        entry_buffer[0] = '\0';
    }
    entry_cursor = strlen(entry_buffer);
    entry_buffer[sizeof(entry_buffer) - 1] = '\0';

    free(current_title);
    current_title = strdup(title_text ? title_text : "Enter Text");
    entry_mode_active = true;
    active = true;

    /* Start text input for SDL_EVENT_TEXT_INPUT events */
    SDL_StartTextInput(window);

    /* Inner event loop */
    SDL_Event e;
    while (active) {
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                SDL_StopTextInput(window);
                overlay_hide();
                return NULL;

            case SDL_EVENT_TEXT_INPUT:
                /* Insert typed text at cursor position */
                {
                    const char *text = e.text.text;
                    int text_len = strlen(text);
                    int buf_len = strlen(entry_buffer);
                    int remaining = (int)sizeof(entry_buffer) - buf_len - 1;
                    if (remaining >= text_len) {
                        memmove(entry_buffer + entry_cursor + text_len, 
                                entry_buffer + entry_cursor, 
                                buf_len - entry_cursor + 1);
                        memcpy(entry_buffer + entry_cursor, text, text_len);
                        entry_cursor += text_len;
                    }
                }
                break;

            case SDL_EVENT_KEY_DOWN:
                switch (e.key.key) {
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    if (entry_buffer[0] != '\0') {
                        char *result = strdup(entry_buffer);
                        SDL_StopTextInput(window);
                        overlay_hide();
                        return result;
                    }
                    /* If empty, treat as cancel */
                    SDL_StopTextInput(window);
                    overlay_hide();
                    return NULL;

                case SDLK_ESCAPE:
                    SDL_StopTextInput(window);
                    overlay_hide();
                    return NULL;

                case SDLK_BACKSPACE:
                    {
                        if (entry_cursor > 0) {
                            int buf_len = strlen(entry_buffer);
                            memmove(entry_buffer + entry_cursor - 1, 
                                    entry_buffer + entry_cursor, 
                                    buf_len - entry_cursor + 1);
                            entry_cursor--;
                        }
                    }
                    break;

                case SDLK_DELETE:
                    {
                        int buf_len = strlen(entry_buffer);
                        if (entry_cursor < buf_len) {
                            memmove(entry_buffer + entry_cursor, 
                                    entry_buffer + entry_cursor + 1, 
                                    buf_len - entry_cursor);
                        }
                    }
                    break;

                case SDLK_LEFT:
                    if (entry_cursor > 0) {
                        entry_cursor--;
                    }
                    break;

                case SDLK_RIGHT:
                    {
                        int buf_len = strlen(entry_buffer);
                        if (entry_cursor < buf_len) {
                            entry_cursor++;
                        }
                    }
                    break;

                default:
                    break;
                }
                break;

            default:
                break;
            }
        }

        /* Render viewer background first to prevent flickering */
        if (viewer) {
            viewer_render(viewer, renderer);
        }
        /* Render */
        overlay_render(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(8);
    }

    SDL_StopTextInput(window);
    return NULL;
}

void overlay_shutdown(void)
{
    overlay_hide();
    if (body_font && body_font != title_font) TTF_CloseFont(body_font);
    if (title_font) TTF_CloseFont(title_font);
    if (help_font && help_font != body_font && help_font != title_font) TTF_CloseFont(help_font);
    body_font = NULL;
    title_font = NULL;
    help_font = NULL;
    TTF_Quit();
}

bool overlay_is_active(void) { return active; }

bool overlay_is_available(void) {
    return (body_font != NULL);
}

void overlay_hide(void)
{
    active = false;
    entry_mode_active = false;
    free(current_title); current_title = NULL;
    free(current_body); current_body = NULL;
    SDL_DestroyTexture(text_texture); text_texture = NULL;
    SDL_DestroyTexture(title_texture); title_texture = NULL;
}

/* Create a texture from text, returns dimensions via w/h pointers */
static SDL_Texture *render_text(const char *text, TTF_Font *font,
                                 SDL_Renderer *renderer,
                                 int *out_w, int *out_h)
{
    SDL_Color white = {255, 255, 255, 255};
    int max_w = viewport_w - 80; /* 40px padding each side */
    if (max_w < 200) max_w = 200;

    SDL_Surface *surf = TTF_RenderText_Blended_Wrapped(font, text, 0, white, max_w);
    if (!surf) return NULL;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    *out_w = surf->w;
    *out_h = surf->h;
    SDL_DestroySurface(surf);
    return tex;
}

void overlay_show_info(const char *title, const char *text)
{
    overlay_hide();
    current_title = strdup(title ? title : "Image Information");
    current_body = strdup(text ? text : "");
    active = true;
}

void overlay_show_help(void)
{
    overlay_hide();
    current_title = strdup("Keyboard Shortcuts");
    current_body = strdup("HELP_TABLE");
    active = true;
}

static void render_shortcut_table(SDL_Renderer *renderer, HelpShortcut *items, int count, const char *category_name, float x, float y, float w, float row_h) {
    /* 1. Draw Category Name */
    TTF_SetFontStyle(help_font, TTF_STYLE_BOLD);
    SDL_Surface *cat_surf = TTF_RenderText_Blended(help_font, category_name, 0, (SDL_Color){220, 50, 50, 255});
    TTF_SetFontStyle(help_font, TTF_STYLE_NORMAL);
    float cat_h = 0;
    if (cat_surf) {
        SDL_Texture *cat_tex = SDL_CreateTextureFromSurface(renderer, cat_surf);
        if (cat_tex) {
            SDL_FRect cat_rect = {x, y, (float)cat_surf->w, (float)cat_surf->h};
            SDL_RenderTexture(renderer, cat_tex, NULL, &cat_rect);
            SDL_DestroyTexture(cat_tex);
        }
        cat_h = cat_surf->h;
        SDL_DestroySurface(cat_surf);
    }

    float table_y = y + cat_h + 8;
    float key_col_w = w * 0.35f;
    float desc_col_w = w - key_col_w;
    float table_h = row_h * (count + 1);

    /* 2. Draw Table Background */
    SDL_FRect tbl_rect = {x, table_y, w, table_h};
    SDL_SetRenderDrawColor(renderer, 25, 25, 25, 255);
    SDL_RenderFillRect(renderer, &tbl_rect);

    /* 3. Draw Header Background */
    SDL_FRect hdr_rect = {x, table_y, w, row_h};
    SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
    SDL_RenderFillRect(renderer, &hdr_rect);

    /* 4. Draw Row Separators and Zebra Stripes */
    for (int i = 0; i < count; i++) {
        float ry = table_y + row_h * (i + 1);
        if (i % 2 == 1) {
            SDL_FRect row_rect = {x, ry, w, row_h};
            SDL_SetRenderDrawColor(renderer, 32, 32, 32, 255);
            SDL_RenderFillRect(renderer, &row_rect);
        }
    }

    /* 5. Draw Header Text */
    SDL_Color header_color = {150, 150, 150, 255};
    SDL_Color text_color = {230, 230, 230, 255};

    TTF_SetFontStyle(help_font, TTF_STYLE_BOLD);

    SDL_Surface *hdr_key_surf = TTF_RenderText_Blended(help_font, "Key", 0, header_color);
    if (hdr_key_surf) {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, hdr_key_surf);
        if (tex) {
            SDL_FRect r = {x + 10, table_y + (row_h - hdr_key_surf->h)/2.0f, (float)hdr_key_surf->w, (float)hdr_key_surf->h};
            SDL_RenderTexture(renderer, tex, NULL, &r);
            SDL_DestroyTexture(tex);
        }
        SDL_DestroySurface(hdr_key_surf);
    }

    SDL_Surface *hdr_desc_surf = TTF_RenderText_Blended(help_font, "Action", 0, header_color);
    if (hdr_desc_surf) {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, hdr_desc_surf);
        if (tex) {
            SDL_FRect r = {x + key_col_w + 10, table_y + (row_h - hdr_desc_surf->h)/2.0f, (float)hdr_desc_surf->w, (float)hdr_desc_surf->h};
            SDL_RenderTexture(renderer, tex, NULL, &r);
            SDL_DestroyTexture(tex);
        }
        SDL_DestroySurface(hdr_desc_surf);
    }

    TTF_SetFontStyle(help_font, TTF_STYLE_NORMAL);

    /* 6. Draw Table Items */
    for (int i = 0; i < count; i++) {
        float ry = table_y + row_h * (i + 1);

        /* Key */
        SDL_Surface *key_surf = TTF_RenderText_Blended(help_font, items[i].key, 0, text_color);
        if (key_surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, key_surf);
            if (tex) {
                float kw = key_surf->w > key_col_w - 20 ? key_col_w - 20 : key_surf->w;
                SDL_FRect r = {x + 10, ry + (row_h - key_surf->h)/2.0f, kw, (float)key_surf->h};
                SDL_RenderTexture(renderer, tex, NULL, &r);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(key_surf);
        }

        /* Desc */
        SDL_Surface *desc_surf = TTF_RenderText_Blended(help_font, items[i].desc, 0, text_color);
        if (desc_surf) {
            SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, desc_surf);
            if (tex) {
                float dw = desc_surf->w > desc_col_w - 20 ? desc_col_w - 20 : desc_surf->w;
                SDL_FRect r = {x + key_col_w + 10, ry + (row_h - desc_surf->h)/2.0f, dw, (float)desc_surf->h};
                SDL_RenderTexture(renderer, tex, NULL, &r);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(desc_surf);
        }
    }

    /* 7. Draw Borders (Clear separations) */
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    SDL_RenderRect(renderer, &tbl_rect);

    /* Draw vertical column separator line */
    SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
    SDL_RenderLine(renderer, x + key_col_w, table_y, x + key_col_w, table_y + table_h);

    /* Draw header horizontal separator line */
    SDL_RenderLine(renderer, x, table_y + row_h, x + w, table_y + row_h);
}

void overlay_render(SDL_Renderer *renderer)
{
    if (!active) return;

    /* Get viewport dimensions */
    int vp_w, vp_h;
    if (!SDL_GetRenderOutputSize(renderer, &vp_w, &vp_h)) {
        return;
    }

    /* Rebuild textures if viewport changed */
    if (vp_w != viewport_w || vp_h != viewport_h) {
        viewport_w = vp_w;
        viewport_h = vp_h;
        SDL_DestroyTexture(text_texture); text_texture = NULL;
        SDL_DestroyTexture(title_texture); title_texture = NULL;
    }

    /* Special rendering code if it's the IMAGE INFO overlay */
    if (current_title && strcmp(current_title, "Image Information") == 0) {
        if (!title_texture && current_title) {
            title_texture = render_text(current_title, title_font, renderer, &title_w, &title_h);
        }

        /* Parse current_body into a key-value list */
        typedef struct {
            char key[64];
            char val[256];
            bool is_header;
        } InfoRow;

        InfoRow rows[128];
        int row_count = 0;

        char *body_copy = strdup(current_body ? current_body : "");
        char *line = strtok(body_copy, "\n");
        while (line && row_count < 128) {
            /* Trim leading space */
            while (*line == ' ') line++;
            
            if (strcmp(line, "EXIF:") == 0) {
                strcpy(rows[row_count].key, "EXIF DATA");
                rows[row_count].val[0] = '\0';
                rows[row_count].is_header = true;
                row_count++;
            } else {
                char *colon = strchr(line, ':');
                if (colon) {
                    *colon = '\0';
                    char *k = line;
                    char *v = colon + 1;
                    /* Trim trailing spaces from key */
                    int k_len = strlen(k);
                    while (k_len > 0 && k[k_len - 1] == ' ') {
                        k[k_len - 1] = '\0';
                        k_len--;
                    }
                    /* Trim leading spaces from val */
                    while (*v == ' ') v++;

                    strncpy(rows[row_count].key, k, sizeof(rows[row_count].key) - 1);
                    rows[row_count].key[sizeof(rows[row_count].key) - 1] = '\0';
                    strncpy(rows[row_count].val, v, sizeof(rows[row_count].val) - 1);
                    rows[row_count].val[sizeof(rows[row_count].val) - 1] = '\0';
                    rows[row_count].is_header = false;
                    row_count++;
                } else if (line[0] != '\0') {
                    /* Regular text line without colon */
                    strncpy(rows[row_count].key, line, sizeof(rows[row_count].key) - 1);
                    rows[row_count].key[sizeof(rows[row_count].key) - 1] = '\0';
                    rows[row_count].val[0] = '\0';
                    rows[row_count].is_header = false;
                    row_count++;
                }
            }
            line = strtok(NULL, "\n");
        }
        free(body_copy);

        /* Now render the info rows in a table! */
        int pad = 24;
        int row_h = 28;
        int total_w = 600;
        
        /* Calculate height dynamically */
        int total_h = pad * 2 + title_h + 15 + row_h * row_count;
        if (total_h > vp_h - 60) {
            total_h = vp_h - 60;
        }

        /* Center overlay */
        float ox = (vp_w - total_w) / 2.0f;
        float oy = (vp_h - total_h) / 2.0f;

        /* Draw semi-transparent background */
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 15, 15, 15, 235);
        SDL_FRect bg = {ox, oy, (float)total_w, (float)total_h};
        SDL_RenderFillRect(renderer, &bg);

        /* Draw border */
        SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
        SDL_RenderRect(renderer, &bg);

        /* Draw title centered */
        if (title_texture) {
            SDL_FRect ttl = {ox + (total_w - title_w) / 2.0f, oy + pad - 10, (float)title_w, (float)title_h};
            SDL_RenderTexture(renderer, title_texture, NULL, &ttl);
        }

        /* Draw key-value table */
        float table_x = ox + pad;
        float table_y = oy + pad + title_h + 10;
        float table_w = total_w - pad * 2;
        float table_h_actual = row_h * row_count;
        if (table_y + table_h_actual > oy + total_h - pad) {
            table_h_actual = oy + total_h - pad - table_y;
        }

        /* Draw Table Background */
        SDL_FRect tbl_rect = {table_x, table_y, table_w, table_h_actual};
        SDL_SetRenderDrawColor(renderer, 25, 25, 25, 255);
        SDL_RenderFillRect(renderer, &tbl_rect);

        float key_col_w = table_w * 0.3f;
        float val_col_w = table_w - key_col_w;

        SDL_Color text_color = {230, 230, 230, 255};
        SDL_Color header_color = {220, 50, 50, 255};

        /* Draw Row background and Text */
        for (int i = 0; i < row_count; i++) {
            float ry = table_y + row_h * i;
            if (ry + row_h > oy + total_h - pad) break; /* clip if too tall */

            if (rows[i].is_header) {
                /* Header row */
                SDL_FRect r_rect = {table_x, ry, table_w, (float)row_h};
                SDL_SetRenderDrawColor(renderer, 45, 45, 45, 255);
                SDL_RenderFillRect(renderer, &r_rect);

                TTF_SetFontStyle(help_font, TTF_STYLE_BOLD);
                SDL_Surface *s = TTF_RenderText_Blended(help_font, rows[i].key, 0, header_color);
                if (s) {
                    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, s);
                    if (tex) {
                        SDL_FRect r = {table_x + 10, ry + (row_h - s->h)/2.0f, (float)s->w, (float)s->h};
                        SDL_RenderTexture(renderer, tex, NULL, &r);
                        SDL_DestroyTexture(tex);
                    }
                    SDL_DestroySurface(s);
                }
                TTF_SetFontStyle(help_font, TTF_STYLE_NORMAL);
            } else {
                /* Regular row, zebra striping */
                if (i % 2 == 1) {
                    SDL_FRect r_rect = {table_x, ry, table_w, (float)row_h};
                    SDL_SetRenderDrawColor(renderer, 32, 32, 32, 255);
                    SDL_RenderFillRect(renderer, &r_rect);
                }

                /* Key */
                TTF_SetFontStyle(help_font, TTF_STYLE_BOLD);
                SDL_Surface *s_k = TTF_RenderText_Blended(help_font, rows[i].key, 0, text_color);
                if (s_k) {
                    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, s_k);
                    if (tex) {
                        float kw = s_k->w > key_col_w - 20 ? key_col_w - 20 : s_k->w;
                        SDL_FRect r = {table_x + 10, ry + (row_h - s_k->h)/2.0f, kw, (float)s_k->h};
                        SDL_RenderTexture(renderer, tex, NULL, &r);
                        SDL_DestroyTexture(tex);
                    }
                    SDL_DestroySurface(s_k);
                }
                TTF_SetFontStyle(help_font, TTF_STYLE_NORMAL);

                /* Value */
                if (rows[i].val[0] != '\0') {
                    SDL_Surface *s_v = TTF_RenderText_Blended(help_font, rows[i].val, 0, text_color);
                    if (s_v) {
                        SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, s_v);
                        if (tex) {
                            float dw = s_v->w > val_col_w - 20 ? val_col_w - 20 : s_v->w;
                            SDL_FRect r = {table_x + key_col_w + 10, ry + (row_h - s_v->h)/2.0f, dw, (float)s_v->h};
                            SDL_RenderTexture(renderer, tex, NULL, &r);
                            SDL_DestroyTexture(tex);
                        }
                        SDL_DestroySurface(s_v);
                    }
                }
            }

            /* Draw horizontal separator line for this row */
            if (i > 0) {
                SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
                SDL_RenderLine(renderer, table_x, ry, table_x + table_w, ry);
            }
        }

        /* Draw Table Border */
        SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
        SDL_RenderRect(renderer, &tbl_rect);

        /* Draw vertical column separator line */
        SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
        SDL_RenderLine(renderer, table_x + key_col_w, table_y, table_x + key_col_w, table_y + table_h_actual);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;
    }

    /* Special rendering code if it's the ENTRY (rename) overlay */
    if (entry_mode_active) {
        if (!title_texture && current_title) {
            title_texture = render_text(current_title, title_font, renderer, &title_w, &title_h);
        }

        int pad = 24;
        int total_w = 520;
        int total_h = 190;

        /* Center overlay */
        float ox = (vp_w - total_w) / 2.0f;
        float oy = (vp_h - total_h) / 2.0f;

        /* Draw semi-transparent background */
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 18, 18, 18, 240);
        SDL_FRect bg = {ox, oy, (float)total_w, (float)total_h};
        SDL_RenderFillRect(renderer, &bg);

        /* Draw border */
        SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
        SDL_RenderRect(renderer, &bg);

        /* Draw title centered */
        if (title_texture) {
            SDL_FRect ttl = {ox + (total_w - title_w) / 2.0f, oy + pad, (float)title_w, (float)title_h};
            SDL_RenderTexture(renderer, title_texture, NULL, &ttl);
        }

        /* Draw Input Box Container */
        float input_x = ox + pad;
        float input_y = oy + pad + title_h + 15;
        float input_w = total_w - pad * 2;
        float input_h = 42;
        SDL_FRect input_rect = {input_x, input_y, input_w, input_h};

        /* Background of input box */
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderFillRect(renderer, &input_rect);

        /* Glowing red border to show focus */
        SDL_SetRenderDrawColor(renderer, 153, 0, 0, 255);
        SDL_RenderRect(renderer, &input_rect);

        /* Render input text */
        float text_y = input_y + (input_h - 18) / 2.0f; /* Using help_font */
        float cursor_x = input_x + 12;

        if (entry_buffer[0] != '\0') {
            SDL_Color text_color = {240, 240, 240, 255};
            SDL_Surface *surf = TTF_RenderText_Blended(help_font, entry_buffer, 0, text_color);
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                if (tex) {
                    float tw = surf->w > input_w - 24 ? input_w - 24 : surf->w;
                    SDL_FRect r = {input_x + 12, text_y, tw, (float)surf->h};
                    SDL_RenderTexture(renderer, tex, NULL, &r);
                    SDL_DestroyTexture(tex);
                }
                SDL_DestroySurface(surf);
            }

            /* Calculate cursor position based on substring up to entry_cursor */
            if (entry_cursor > 0) {
                char temp[512];
                int c_pos = entry_cursor;
                if (c_pos >= (int)sizeof(temp)) c_pos = sizeof(temp) - 1;
                strncpy(temp, entry_buffer, c_pos);
                temp[c_pos] = '\0';

                int size_w = 0;
                TTF_GetStringSize(help_font, temp, 0, &size_w, NULL);
                cursor_x += size_w;
            }
        }

        /* Draw blinking cursor */
        if ((SDL_GetTicks() / 500) % 2 == 0) {
            SDL_FRect cursor_rect = {cursor_x, text_y, 3, 18};
            SDL_SetRenderDrawColor(renderer, 153, 0, 0, 255);
            SDL_RenderFillRect(renderer, &cursor_rect);
        }

        /* Draw Buttons/Hints at the bottom */
        SDL_Color hint_color = {150, 150, 150, 255};
        SDL_Surface *hint_surf = TTF_RenderText_Blended(body_font, "[Enter] Confirm      [Esc] Cancel", 0, hint_color);
        if (hint_surf) {
            SDL_Texture *hint_tex = SDL_CreateTextureFromSurface(renderer, hint_surf);
            if (hint_tex) {
                SDL_FRect r = {ox + (total_w - hint_surf->w) / 2.0f, input_y + input_h + 20, (float)hint_surf->w, (float)hint_surf->h};
                SDL_RenderTexture(renderer, hint_tex, NULL, &r);
                SDL_DestroyTexture(hint_tex);
            }
            SDL_DestroySurface(hint_surf);
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;
    }

    /* Special rendering code if it's the HELP overlay */
    if (current_body && strcmp(current_body, "HELP_TABLE") == 0) {
        if (!title_texture && current_title) {
            title_texture = render_text(current_title, title_font, renderer, &title_w, &title_h);
        }

        /* Calculate table size dynamically */
        int pad = 30;
        int col_gap = 40;
        int row_h = 28;

        /* Total table width */
        int col_w = (vp_w - pad * 2 - col_gap) / 2;
        if (col_w > 450) col_w = 450;
        if (col_w < 300) col_w = 300;

        int total_w = col_w * 2 + col_gap + pad * 2;
        int total_h = vp_h - 100; /* leave some margin top/bottom */
        if (total_h > 580) total_h = 580;

        /* Center overlay */
        float ox = (vp_w - total_w) / 2.0f;
        float oy = (vp_h - total_h) / 2.0f;

        /* Draw semi-transparent background */
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 15, 15, 15, 235);
        SDL_FRect bg = {ox, oy, (float)total_w, (float)total_h};
        SDL_RenderFillRect(renderer, &bg);

        /* Draw border */
        SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
        SDL_RenderRect(renderer, &bg);

        /* Draw title centered */
        if (title_texture) {
            SDL_FRect ttl = {ox + (total_w - title_w) / 2.0f, oy + pad - 10, (float)title_w, (float)title_h};
            SDL_RenderTexture(renderer, title_texture, NULL, &ttl);
        }

        /* Render Category Tables side-by-side */
        float col1_x = ox + pad;
        float col2_x = col1_x + col_w + col_gap;
        float current_y1 = oy + pad + title_h + 10;
        float current_y2 = current_y1;

        /* Left Column: Navigation & General */
        render_shortcut_table(renderer, help_nav, sizeof(help_nav)/sizeof(help_nav[0]), "NAVIGATION", col1_x, current_y1, col_w, row_h);
        current_y1 += (sizeof(help_nav)/sizeof(help_nav[0]) + 1) * row_h + 50; /* spacer + title height offset */
        render_shortcut_table(renderer, help_gen, sizeof(help_gen)/sizeof(help_gen[0]), "GENERAL", col1_x, current_y1, col_w, row_h);

        /* Right Column: View & Image Operations */
        render_shortcut_table(renderer, help_view, sizeof(help_view)/sizeof(help_view[0]), "VIEW CONTROLS", col2_x, current_y2, col_w, row_h);
        current_y2 += (sizeof(help_view)/sizeof(help_view[0]) + 1) * row_h + 50;
        render_shortcut_table(renderer, help_ops, sizeof(help_ops)/sizeof(help_ops[0]), "IMAGE OPERATIONS", col2_x, current_y2, col_w, row_h);

        /* Restore blend mode */
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        return;
    }

    if (!text_texture && current_body) {
        text_texture = render_text(current_body, body_font, renderer, &text_w, &text_h);
    }
    if (!title_texture && current_title) {
        title_texture = render_text(current_title, title_font, renderer, &title_w, &title_h);
    }

    /* Calculate overlay dimensions */
    int pad = 20;
    int title_gap = 10;
    int total_w = (title_w > text_w ? title_w : text_w) + pad * 2;
    int total_h = (title_texture ? title_h : 0) +
                  (title_texture && text_texture ? title_gap : 0) +
                  (text_texture ? text_h : 0) + pad * 2;

    if (total_w > viewport_w - 40) total_w = viewport_w - 40;
    if (total_h > viewport_h - 40) total_h = viewport_h - 40;

    /* Center overlay */
    float ox = (viewport_w - total_w) / 2.0f;
    float oy = (viewport_h - total_h) / 2.0f;

    /* Draw semi-transparent background */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_FRect bg = {ox, oy, (float)total_w, (float)total_h};
    SDL_RenderFillRect(renderer, &bg);

    /* Draw border */
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    SDL_RenderRect(renderer, &bg);

    /* Draw title */
    float ty = oy + pad;
    if (title_texture) {
        SDL_FRect ttl = {ox + pad, ty, (float)title_w, (float)title_h};
        SDL_RenderTexture(renderer, title_texture, NULL, &ttl);
        ty += title_h + title_gap;
    }

    /* Draw body */
    if (text_texture) {
        SDL_FRect body_rect = {ox + pad, ty, (float)text_w, (float)text_h};
        SDL_RenderTexture(renderer, text_texture, NULL, &body_rect);
    }

    /* Restore blend mode */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}
