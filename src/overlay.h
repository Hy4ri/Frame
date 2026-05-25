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

/* Hide the currently displayed overlay. */
void overlay_hide(void);

/* Render the active overlay on top of the current frame.
   Must be called AFTER viewer_render() in the main loop. */
void overlay_render(SDL_Renderer *renderer);

/* Shutdown overlay system, free all resources. */
void overlay_shutdown(void);

#endif
