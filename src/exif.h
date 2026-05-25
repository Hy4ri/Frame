#ifndef FRAME_EXIF_H
#define FRAME_EXIF_H

/* Extract EXIF metadata from an image file.
   Returns a dynamically allocated string with formatted EXIF data,
   or NULL if no EXIF data is present or extraction fails.
   The caller must free the returned string. */
char *exif_get_data(const char *path);

#endif
