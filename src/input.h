#ifndef FRAME_INPUT_H
#define FRAME_INPUT_H

#include <SDL3/SDL.h>
#include <stdbool.h>

struct AppState;
struct Viewer;

/* Process a keyboard event. Returns false if the app should quit.
   Handles ALL keybindings: vim nav, zoom, rotate, delete, rename, info, help.
   Also handles 'gg' double-tap timing internally. */
bool input_handle_keyboard(struct AppState *app, struct Viewer *viewer,
                           const SDL_KeyboardEvent *event, SDL_Window *window);

/* Reset the 'gg' sequence state (e.g., when app loses focus). */
void input_reset_gg(void);

#endif /* FRAME_INPUT_H */
