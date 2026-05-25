#define _GNU_SOURCE
#include "overlay.h"
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
static bool active = false;
static SDL_Texture *text_texture = NULL;
static SDL_Texture *title_texture = NULL;
static int text_w = 0, text_h = 0;
static int title_w = 0, title_h = 0;
static char *current_title = NULL;
static char *current_body = NULL;
static int viewport_w = 0, viewport_h = 0;

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
            title_font = TTF_OpenFont(font_paths[i], 18.0f);
            if (!title_font) {
                title_font = body_font;
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

    return true;
}

/* ================================================================
   Confirm dialog
   ================================================================ */

bool overlay_modal_confirm(const char *title_text, const char *message,
                           SDL_Renderer *renderer) {
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

/* Build the entry display text from the current buffer */
static void build_entry_display_text(char *out, int out_size) {
    snprintf(out, out_size, "%s\n\n[Enter] Confirm    [Esc] Cancel",
             entry_buffer[0] ? entry_buffer : " ");
}

char *overlay_modal_entry(const char *title_text, const char *initial_text,
                          SDL_Renderer *renderer, SDL_Window *window) {
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

    /* Show initial state */
    char display[2048];
    build_entry_display_text(display, sizeof(display));
    overlay_show_info(title_text ? title_text : "Enter Text", display);
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
                /* Append typed text to buffer */
                {
                    const char *text = e.text.text;
                    int text_len = strlen(text);
                    int buf_len = strlen(entry_buffer);
                    int remaining = (int)sizeof(entry_buffer) - buf_len - 1;
                    if (remaining > 0) {
                        int to_copy = text_len < remaining ? text_len : remaining;
                        memcpy(entry_buffer + buf_len, text, to_copy);
                        entry_buffer[buf_len + to_copy] = '\0';
                        entry_cursor = strlen(entry_buffer);
                    }
                    /* Refresh display */
                    build_entry_display_text(display, sizeof(display));
                    overlay_show_info(title_text ? title_text : "Enter Text", display);
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
                        int len = strlen(entry_buffer);
                        if (len > 0) {
                            entry_buffer[len - 1] = '\0';
                            entry_cursor = len - 1;
                            build_entry_display_text(display, sizeof(display));
                            overlay_show_info(title_text ? title_text : "Enter Text", display);
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
    body_font = NULL;
    title_font = NULL;
    TTF_Quit();
}

bool overlay_is_active(void) { return active; }

bool overlay_is_available(void) {
    return (body_font != NULL);
}

void overlay_hide(void)
{
    active = false;
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
    current_body = strdup(
        "NAVIGATION\n"
        "  h / \xe2\x86\x90  Previous image\n"
        "  l / \xe2\x86\x92  Next image\n"
        "  j / \xe2\x86\x93  Next image\n"
        "  k / \xe2\x86\x91  Previous image\n"
        "  gg     First image\n"
        "  G      Last image\n"
        "\n"
        "VIEW\n"
        "  f      Toggle fullscreen\n"
        "  + / =  Zoom in (also: z)\n"
        "  -      Zoom out (also: x)\n"
        "  0      Fit to window\n"
        "  1      Original size (1:1)\n"
        "  Scroll  Zoom with mouse wheel\n"
        "  Drag   Pan with mouse\n"
        "\n"
        "IMAGE OPERATIONS\n"
        "  r      Rotate clockwise 90\xc2\xb0\n"
        "  R      Rotate counter-clockwise 90\xc2\xb0\n"
        "  d / Del Delete image (move to trash)\n"
        "  F2     Rename image\n"
        "  i      Show image info\n"
        "\n"
        "GENERAL\n"
        "  ?      Show this help\n"
        "  q / Esc Quit"
    );
    active = true;
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
