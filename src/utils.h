#ifndef FRAME_UTILS_H
#define FRAME_UTILS_H

/* Format a file size in bytes to a human-readable string.
   The returned string must be freed by the caller. */
char *format_file_size(long long bytes);

/* Get the format name from a file extension (e.g. ".jpg" -> "JPEG").
   Returns a string literal — do not free. */
const char *format_from_ext(const char *ext);

#endif /* FRAME_UTILS_H */
