#define _GNU_SOURCE
#include "search.h"
#include "app.h"
#include "viewer.h"
#include "cache.h"
#include "loader.h"
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define GRID_COLS 5
#define GRID_ROWS 5
#define CELL_PADDING 10
#define TOP_BAR_HEIGHT 60
#define FILENAME_HEIGHT 20

static bool active = false;
static struct AppState *current_app = NULL;
static struct Viewer *current_viewer = NULL;

/* Filtered results list (stores 0-based indices into current_app's images) */
static int *filtered_indices = NULL;
static int filtered_count = 0;

/* Navigation / selection indexes (relative to filtered list) */
static int selected_item = 0;
static int scroll_offset = 0; /* Row index of first visible row in grid */

/* Search query string */
static char search_query[256] = {0};

/* Cache font / text textures */
static TTF_Font *search_font = NULL;
static SDL_Texture *query_texture = NULL;
static int query_w = 0, query_h = 0;

/* Grid cell texture cache to avoid reloading/re-creating textures on every frame */
typedef struct {
    int app_idx;
    SDL_Texture *texture;
} CellTextureCache;

#define MAX_VISIBLE_TEX (GRID_COLS * GRID_ROWS)
static CellTextureCache visible_textures[MAX_VISIBLE_TEX];

static void clear_visible_textures(void) {
    for (int i = 0; i < MAX_VISIBLE_TEX; i++) {
        if (visible_textures[i].texture) {
            SDL_DestroyTexture(visible_textures[i].texture);
            visible_textures[i].texture = NULL;
        }
        visible_textures[i].app_idx = -1;
    }
}

static int selected_app_index = 0;

static void request_visible_thumbnails(void) {
    if (!current_viewer || !current_app || filtered_count == 0) return;

    const char *paths[MAX_VISIBLE_TEX];
    int count = 0;

    int start_item = scroll_offset * GRID_COLS;
    for (int i = 0; i < MAX_VISIBLE_TEX; i++) {
        int item_idx = start_item + i;
        if (item_idx >= filtered_count) break;

        int app_idx = filtered_indices[item_idx];
        const char *path = app_image_path(current_app, app_idx);
        if (path) {
            paths[count++] = path;
        }
    }

    if (count > 0) {
        viewer_prefetch_paths(current_viewer, paths, count);
    }
}

static void update_filter(void) {
    if (!current_app) return;

    int total_files = app_image_count(current_app);
    filtered_count = 0;

    free(filtered_indices);
    filtered_indices = malloc(sizeof(int) * total_files);
    if (!filtered_indices) return;

    size_t query_len = strlen(search_query);

    for (int i = 0; i < total_files; i++) {
        const char *path = app_image_path(current_app, i);
        if (!path) continue;

        const char *filename = strrchr(path, '/');
        if (filename) filename++;
        else filename = path;

        if (query_len == 0 || strcasestr(filename, search_query) != NULL) {
            filtered_indices[filtered_count++] = i;
        }
    }

    /* Auto-select first matching element */
    selected_item = 0;
    scroll_offset = 0;
    if (filtered_count > 0) {
        selected_app_index = filtered_indices[0];
    } else {
        selected_app_index = 0;
    }

    clear_visible_textures();
    request_visible_thumbnails();
}

static void rebuild_query_texture(SDL_Renderer *renderer) {
    if (query_texture) {
        SDL_DestroyTexture(query_texture);
        query_texture = NULL;
    }
    if (!search_font) return;

    char display_text[512];
    snprintf(display_text, sizeof(display_text), "Search: %s", search_query);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface *surf = TTF_RenderText_Blended(search_font, display_text, 0, white);
    if (surf) {
        query_texture = SDL_CreateTextureFromSurface(renderer, surf);
        query_w = surf->w;
        query_h = surf->h;
        SDL_DestroySurface(surf);
    }
}

void search_init(void) {
    /* Fonts will be retrieved from the overlay system or local font search */
    const char *font_paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/run/current-system/sw/share/X11/fonts/DejaVuSans.ttf",
        NULL
    };
    for (int i = 0; font_paths[i]; i++) {
        search_font = TTF_OpenFont(font_paths[i], 16.0f);
        if (search_font) break;
    }
    for (int i = 0; i < MAX_VISIBLE_TEX; i++) {
        visible_textures[i].texture = NULL;
        visible_textures[i].app_idx = -1;
    }
}

void search_open(struct AppState *app, struct Viewer *viewer, SDL_Renderer *renderer, SDL_Window *window) {
    current_app = app;
    current_viewer = viewer;
    active = true;
    search_query[0] = '\0';
    update_filter();
    rebuild_query_texture(renderer);

    /* Start text input for search queries */
    SDL_StartTextInput(window);
}

void search_close(SDL_Window *window) {
    if (!active) return;
    active = false;
    clear_visible_textures();
    free(filtered_indices);
    filtered_indices = NULL;
    filtered_count = 0;
    if (query_texture) {
        SDL_DestroyTexture(query_texture);
        query_texture = NULL;
    }
    /* Stop text input */
    if (window) {
        SDL_StopTextInput(window);
    }
}

bool search_is_active(void) {
    return active;
}

int search_selected_index(void) {
    return selected_app_index;
}

bool search_check_dirty(void) {
    if (!active || !current_viewer) return false;

    struct ImageCache *thumb_cache = viewer_get_thumb_cache(current_viewer);
    if (!thumb_cache) return false;

    int start_item = scroll_offset * GRID_COLS;
    bool found_new = false;

    for (int i = 0; i < MAX_VISIBLE_TEX; i++) {
        int item_idx = start_item + i;
        if (item_idx >= filtered_count) break;

        int app_idx = filtered_indices[item_idx];
        const char *path = app_image_path(current_app, app_idx);
        if (!path) continue;

        /* If we don't have a texture cached in search grid, check if it's now in the thumb_cache */
        if (visible_textures[i].app_idx != app_idx) {
            SDL_Surface *surf = cache_get(thumb_cache, path);
            if (surf) {
                found_new = true;
                break;
            }
        }
    }

    return found_new;
}

SearchResult search_handle_event(const SDL_Event *event, SDL_Window *window) {
    if (!active) return SEARCH_CONTINUE;

    switch (event->type) {
    case SDL_EVENT_TEXT_INPUT: {
        size_t q_len = strlen(search_query);
        size_t t_len = strlen(event->text.text);
        if (q_len + t_len < sizeof(search_query)) {
            strcat(search_query, event->text.text);
            update_filter();
            rebuild_query_texture(SDL_GetRenderer(window));
        }
        return SEARCH_CONTINUE;
    }

    case SDL_EVENT_KEY_DOWN: {
        SDL_Keycode key = event->key.key;

        if (key == SDLK_ESCAPE) {
            search_close(window);
            return SEARCH_CANCEL;
        }

        if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            if (filtered_count > 0) {
                selected_app_index = filtered_indices[selected_item];
                SearchResult res = SEARCH_SELECT;
                search_close(window);
                return res;
            }
            search_close(window);
            return SEARCH_CANCEL;
        }

        if (key == SDLK_BACKSPACE) {
            size_t len = strlen(search_query);
            if (len > 0) {
                search_query[len - 1] = '\0';
                update_filter();
                rebuild_query_texture(SDL_GetRenderer(window));
            }
            return SEARCH_CONTINUE;
        }

        /* Keyboard navigation: hjkl + arrows */
        int row = selected_item / GRID_COLS;

        if (key == SDLK_LEFT || key == SDLK_H) {
            if (selected_item > 0) {
                selected_item--;
            }
        } else if (key == SDLK_RIGHT || key == SDLK_L) {
            if (selected_item < filtered_count - 1) {
                selected_item++;
            }
        } else if (key == SDLK_UP || key == SDLK_K) {
            if (row > 0) {
                selected_item -= GRID_COLS;
            }
        } else if (key == SDLK_DOWN || key == SDLK_J) {
            if (selected_item + GRID_COLS < filtered_count) {
                selected_item += GRID_COLS;
            } else {
                /* Go to the last item if in last partial row */
                int total_rows = (filtered_count + GRID_COLS - 1) / GRID_COLS;
                if (row < total_rows - 1) {
                    selected_item = filtered_count - 1;
                }
            }
        }

        selected_app_index = filtered_indices[selected_item];

        /* Adjust scroll_offset to keep selected item in view */
        int sel_row = selected_item / GRID_COLS;
        int prev_scroll = scroll_offset;
        if (sel_row < scroll_offset) {
            scroll_offset = sel_row;
        } else if (sel_row >= scroll_offset + GRID_ROWS) {
            scroll_offset = sel_row - GRID_ROWS + 1;
        }
        if (scroll_offset != prev_scroll) {
            request_visible_thumbnails();
        }

        return SEARCH_CONTINUE;
    }
    }

    return SEARCH_CONTINUE;
}

void search_render(SDL_Renderer *renderer) {
    if (!active) return;

    int vp_w, vp_h;
    if (!SDL_GetRenderOutputSize(renderer, &vp_w, &vp_h)) return;

    /* 1. Semi-transparent black background */
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 240);
    SDL_FRect bg = {0, 0, (float)vp_w, (float)vp_h};
    SDL_RenderFillRect(renderer, &bg);

    /* 2. Top Bar (Search Box) styled like rename modal */
    SDL_SetRenderDrawColor(renderer, 18, 18, 18, 255);
    SDL_FRect top_bar = {0, 0, (float)vp_w, (float)TOP_BAR_HEIGHT};
    SDL_RenderFillRect(renderer, &top_bar);

    /* Inner input field container */
    float input_x = 15;
    float input_y = 10;
    float input_w = vp_w - 30;
    float input_h = TOP_BAR_HEIGHT - 20;
    SDL_FRect input_rect = {input_x, input_y, input_w, input_h};

    /* Background of input box */
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderFillRect(renderer, &input_rect);

    /* Glowing red border to show focus */
    SDL_SetRenderDrawColor(renderer, 153, 0, 0, 255);
    SDL_RenderRect(renderer, &input_rect);

    /* Draw search text query */
    if (query_texture) {
        SDL_FRect q_rect = {input_x + 12, input_y + (input_h - query_h) / 2.0f, (float)query_w, (float)query_h};
        SDL_RenderTexture(renderer, query_texture, NULL, &q_rect);
    }

    /* Draw blinking accent-red cursor */
    if ((SDL_GetTicks() / 500) % 2 == 0) {
        /* Calculate cursor position dynamically */
        char display_text[512];
        snprintf(display_text, sizeof(display_text), "Search: %s", search_query);
        int text_size_w = 0;
        TTF_GetStringSize(search_font, display_text, 0, &text_size_w, NULL);
        
        float cursor_x = input_x + 12 + text_size_w;
        float text_y = input_y + (input_h - 18) / 2.0f;
        SDL_FRect cursor_rect = {cursor_x, text_y, 3, 18};
        SDL_SetRenderDrawColor(renderer, 153, 0, 0, 255);
        SDL_RenderFillRect(renderer, &cursor_rect);
    }

    /* Info text on right of top bar inside input box */
    char info_text[64];
    snprintf(info_text, sizeof(info_text), "%d matches", filtered_count);
    SDL_Surface *info_surf = TTF_RenderText_Blended(search_font, info_text, 0, (SDL_Color){150, 150, 150, 255});
    if (info_surf) {
        SDL_Texture *info_tex = SDL_CreateTextureFromSurface(renderer, info_surf);
        if (info_tex) {
            SDL_FRect info_rect = {(float)(vp_w - info_surf->w - 30), input_y + (input_h - info_surf->h) / 2.0f, (float)info_surf->w, (float)info_surf->h};
            SDL_RenderTexture(renderer, info_tex, NULL, &info_rect);
            SDL_DestroyTexture(info_tex);
        }
        SDL_DestroySurface(info_surf);
    }

    /* 3. Render 5x5 Grid */
    float grid_y = TOP_BAR_HEIGHT + CELL_PADDING;
    float grid_h = vp_h - grid_y - CELL_PADDING;
    float grid_w = vp_w - CELL_PADDING * 2;

    float cell_w = (grid_w - (GRID_COLS - 1) * CELL_PADDING) / GRID_COLS;
    float cell_h = (grid_h - (GRID_ROWS - 1) * CELL_PADDING) / GRID_ROWS;

    struct ImageCache *thumb_cache = viewer_get_thumb_cache(current_viewer);

    /* Render cell backgrounds and thumbnails */
    int start_item = scroll_offset * GRID_COLS;
    for (int i = 0; i < GRID_COLS * GRID_ROWS; i++) {
        int item_idx = start_item + i;
        if (item_idx >= filtered_count) break;

        int app_idx = filtered_indices[item_idx];
        const char *path = app_image_path(current_app, app_idx);
        if (!path) continue;

        int row = i / GRID_COLS;
        int col = i % GRID_COLS;

        float cx = CELL_PADDING + col * (cell_w + CELL_PADDING);
        float cy = grid_y + row * (cell_h + CELL_PADDING);

        SDL_FRect cell_rect = {cx, cy, cell_w, cell_h};

        /* Draw cell border / background */
        if (item_idx == selected_item) {
            SDL_SetRenderDrawColor(renderer, 153, 0, 0, 255); /* Selection Highlight Red */
            SDL_RenderRect(renderer, &cell_rect);
            SDL_SetRenderDrawColor(renderer, 60, 10, 10, 255);
            SDL_RenderFillRect(renderer, &cell_rect);
        } else {
            SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
            SDL_RenderRect(renderer, &cell_rect);
            SDL_SetRenderDrawColor(renderer, 25, 25, 25, 255);
            SDL_RenderFillRect(renderer, &cell_rect);
        }

        /* Load/retrieve thumbnail texture */
        SDL_Texture *tex = NULL;
        if (visible_textures[i].app_idx == app_idx) {
            tex = visible_textures[i].texture;
        } else {
            /* Destroy stale texture at this slot */
            if (visible_textures[i].texture) {
                SDL_DestroyTexture(visible_textures[i].texture);
                visible_textures[i].texture = NULL;
            }
            visible_textures[i].app_idx = -1;

            /* Try to fetch from thumb cache */
            SDL_Surface *surf = cache_get(thumb_cache, path);
            if (surf) {
                tex = SDL_CreateTextureFromSurface(renderer, surf);
                visible_textures[i].texture = tex;
                visible_textures[i].app_idx = app_idx;
            }
        }

        /* Render Thumbnail image scaled inside cell */
        float img_area_h = cell_h - FILENAME_HEIGHT - CELL_PADDING * 2;
        if (tex) {
            float tw, th;
            SDL_GetTextureSize(tex, &tw, &th);
            float scale_x = (cell_w - CELL_PADDING * 2) / tw;
            float scale_y = img_area_h / th;
            float scale = scale_x < scale_y ? scale_x : scale_y;

            float rw = tw * scale;
            float rh = th * scale;
            float rx = cx + (cell_w - rw) / 2.0f;
            float ry = cy + CELL_PADDING + (img_area_h - rh) / 2.0f;

            SDL_FRect dst_rect = {rx, ry, rw, rh};
            SDL_RenderTexture(renderer, tex, NULL, &dst_rect);
        } else {
            /* Render a simple gray placeholder if not cached yet */
            SDL_SetRenderDrawColor(renderer, 70, 70, 70, 255);
            SDL_FRect placeholder = {cx + cell_w / 4.0f, cy + CELL_PADDING, cell_w / 2.0f, img_area_h};
            SDL_RenderFillRect(renderer, &placeholder);
        }

        /* Render filename below thumbnail */
        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;

        SDL_Surface *name_surf = TTF_RenderText_Blended(search_font, name, 0, (SDL_Color){220, 220, 220, 255});
        if (name_surf) {
            SDL_Texture *name_tex = SDL_CreateTextureFromSurface(renderer, name_surf);
            if (name_tex) {
                /* Center text horizontally, limit width to cell width */
                float tw = name_surf->w;
                float th = name_surf->h;
                if (tw > cell_w - CELL_PADDING * 2) {
                    tw = cell_w - CELL_PADDING * 2;
                }
                float tx = cx + (cell_w - tw) / 2.0f;
                float ty = cy + cell_h - FILENAME_HEIGHT - 2;

                SDL_FRect name_rect = {tx, ty, tw, th};
                SDL_RenderTexture(renderer, name_tex, NULL, &name_rect);
                SDL_DestroyTexture(name_tex);
            }
            SDL_DestroySurface(name_surf);
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void search_shutdown(void) {
    search_close(NULL);
    if (search_font) {
        TTF_CloseFont(search_font);
        search_font = NULL;
    }
}
