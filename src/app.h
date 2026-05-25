#ifndef FRAME_APP_H
#define FRAME_APP_H

#include <stdbool.h>

/* Opaque application state */
typedef struct AppState AppState;

/* Create a new application state with an optional initial path.
   The path string is copied internally. Pass NULL if no initial path. */
AppState *app_create(const char *initial_path);

/* Destroy the application state and free all resources. */
void app_destroy(AppState *app);

/* Load images from the given directory (or extract the directory from a file path).
   Sorts the image list alphabetically. If the path points to a file, the directory
   containing that file is scanned and the specific file becomes the current image. */
void app_load_directory(AppState *app, const char *path);

/* Get the number of images in the current list. */
int app_image_count(const AppState *app);

/* Get the current image index (1-based, for display). Returns 0 if no images. */
int app_current_index(const AppState *app);

/* Get the full path of the currently displayed image. Returns NULL if no images. */
const char *app_current_path(const AppState *app);

/* Navigate to the next image. Clips at end — does nothing if at end. */
void app_next_image(AppState *app);

/* Navigate to the previous image. Clips at beginning. */
void app_prev_image(AppState *app);

/* Jump to the first image. No-op if no images. */
void app_first_image(AppState *app);

/* Jump to the last image. No-op if no images. */
void app_last_image(AppState *app);

/* Jump to a specific 0-based index. Clamps to valid range. Returns true on success. */
bool app_display_image(AppState *app, int index);

/* Remove the current image from the list (after it has been deleted/moved to trash).
   Does NOT delete the file — that's done by fileops module first.
   Returns true if the image was removed, false if the list is empty. */
bool app_remove_current(AppState *app);

/* Rename the current image path (the file was already renamed by fileops module).
   Updates the internal path string and re-sorts the list. */
void app_rename_current(AppState *app, const char *new_path);

/* Get the initial path that was passed on the command line (may be NULL). */
const char *app_initial_path(const AppState *app);

/* Get the path at a specific 0-based index. Returns NULL if out of range. */
const char *app_image_path(const AppState *app, int index);

#endif /* FRAME_APP_H */
