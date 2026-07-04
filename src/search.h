#ifndef FRAME_SEARCH_H
#define FRAME_SEARCH_H

#include <SDL3/SDL.h>
#include <stdbool.h>

struct AppState;
struct Viewer;

typedef enum {
    SEARCH_CONTINUE,
    SEARCH_SELECT,
    SEARCH_CANCEL
} SearchResult;

/* Initialize search system */
void search_init(void);

/* Open search grid overlay */
void search_open(struct AppState *app, struct Viewer *viewer, SDL_Renderer *renderer, SDL_Window *window);

/* Close search grid overlay */
void search_close(SDL_Window *window);

/* Check if search grid is active */
bool search_is_active(void);

/* Handle events when search is active. Returns status. */
SearchResult search_handle_event(const SDL_Event *event, SDL_Window *window);

/* Render search grid overlay */
void search_render(SDL_Renderer *renderer);

/* Get the selected index in app files (0-based) when search succeeds */
int search_selected_index(void);

/* Check if new thumbnails are loaded that require rendering */
bool search_check_dirty(void);

/* Free resources on shutdown */
void search_shutdown(void);

#endif /* FRAME_SEARCH_H */
