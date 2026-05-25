#define _GNU_SOURCE
#include "anim.h"
#include <stdlib.h>
#include <stdio.h>

struct Animation {
    IMG_Animation *img_anim;
};

Animation *anim_load(const char *path)
{
    IMG_Animation *img_anim = IMG_LoadAnimation(path);
    if (!img_anim) {
        return NULL;
    }

    if (img_anim->count <= 1) {
        IMG_FreeAnimation(img_anim);
        return NULL;
    }

    Animation *anim = calloc(1, sizeof(Animation));
    anim->img_anim = img_anim;
    return anim;
}

void anim_free(Animation *anim)
{
    if (!anim) return;
    if (anim->img_anim) IMG_FreeAnimation(anim->img_anim);
    free(anim);
}

int anim_frame_count(const Animation *anim)
{
    if (!anim || !anim->img_anim) return 0;
    return anim->img_anim->count;
}

SDL_Surface *anim_get_frame(Animation *anim, int frame_index)
{
    if (!anim || !anim->img_anim) return NULL;
    if (frame_index < 0 || frame_index >= anim->img_anim->count) return NULL;
    return anim->img_anim->frames[frame_index];
}

int anim_get_delay(const Animation *anim, int frame_index)
{
    if (!anim || !anim->img_anim) return 16;
    if (frame_index < 0 || frame_index >= anim->img_anim->count) return 16;
    int delay = anim->img_anim->delays ? anim->img_anim->delays[frame_index] : 100;
    if (delay < 10) delay = 10;  /* minimum 10ms to prevent CPU spin */
    return delay;
}
