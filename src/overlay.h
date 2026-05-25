#ifndef FRAME_OVERLAY_H
#define FRAME_OVERLAY_H

#include <SDL3/SDL.h>
#include <stdbool.h>

/* Initialize the overlay system. Tries to load a font.
   Returns false if no font could be loaded (overlays disabled). */
bool overlay_init(void);

/* Show the image info overlay. */
void overlay_show_info(const char *title, const char *text);

/* Show the keybindings help overlay with vim-style key table. */
void overlay_show_help(void);

/* Check whether an overlay is currently being displayed. */
bool overlay_is_active(void);

/* Check if the overlay system has a loaded font (i.e., can render dialogs).
   Returns false if no font was found — modal dialogs will default to cancel/fail. */
bool overlay_is_available(void);

/* Hide the currently displayed overlay. */
void overlay_hide(void);

/* Modal confirm dialog. Blocks until user presses Enter (confirm) or Esc (cancel).
   Returns true if confirmed, false if cancelled.
   renderer: used to render the dialog during the modal loop. */
bool overlay_modal_confirm(const char *title, const char *message,
                           SDL_Renderer *renderer);

/* Modal text entry dialog. Blocks until user presses Enter (confirm) or Esc (cancel).
   Returns a malloc'd string with the entered text, or NULL if cancelled.
   The caller must free the returned string.
   renderer: used to render the dialog.
   window: needed for SDL text input events. */
char *overlay_modal_entry(const char *title, const char *initial_text,
                          SDL_Renderer *renderer, SDL_Window *window);

/* Render the active overlay on top of the current frame.
   Must be called AFTER viewer_render() in the main loop. */
void overlay_render(SDL_Renderer *renderer);

/* Shutdown overlay system, free all resources. */
void overlay_shutdown(void);

#endif
