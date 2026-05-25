#ifndef FRAME_ANIM_H
#define FRAME_ANIM_H

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <stdbool.h>

/* Opaque animation state */
typedef struct Animation Animation;

/* Load an animation from a file.
   Returns NULL if the file is not animated or loading fails. */
Animation *anim_load(const char *path);

/* Free an animation and all its resources. */
void anim_free(Animation *anim);

/* Get the number of frames. */
int anim_frame_count(const Animation *anim);

/* Get the surface for a specific frame.
   The returned surface is borrowed — do NOT free it.
   Returns NULL if index is out of bounds. */
SDL_Surface *anim_get_frame(Animation *anim, int frame_index);

/* Get the delay for a specific frame in milliseconds. */
int anim_get_delay(const Animation *anim, int frame_index);

#endif
