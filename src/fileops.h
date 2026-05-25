#ifndef FRAME_FILEOPS_H
#define FRAME_FILEOPS_H

/* Move a file to the XDG trash directory (~/.local/share/Trash/files/).
   Follows the freedesktop.org Trash specification:
   - Creates ~/.local/share/Trash/files/ and ~/.local/share/Trash/info/ if needed.
   - Handles name collisions by appending _2, _3, etc.
   - Writes a .trashinfo file with original path and deletion date.
   Returns 0 on success, -1 on error. */
int fileops_trash(const char *path);

/* Rename a file within its directory. The new_name is just the filename
   (not the full path). Returns the new full path (caller must free),
   or NULL on error. */
char *fileops_rename(const char *old_path, const char *new_name);

#endif /* FRAME_FILEOPS_H */
