#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

char *format_file_size(long long bytes) {
    const long long KB = 1024;
    const long long MB = KB * 1024;
    const long long GB = MB * 1024;

    char *buf = malloc(32);
    if (!buf) return NULL;

    if (bytes >= GB) {
        snprintf(buf, 32, "%.2f GB", (double)bytes / (double)GB);
    } else if (bytes >= MB) {
        snprintf(buf, 32, "%.2f MB", (double)bytes / (double)MB);
    } else if (bytes >= KB) {
        snprintf(buf, 32, "%.2f KB", (double)bytes / (double)KB);
    } else {
        snprintf(buf, 32, "%lld bytes", bytes);
    }
    return buf;
}

const char *format_from_ext(const char *ext) {
    if (!ext) return "Unknown";

    /* Case-insensitive comparison helper */
    #define EXT_EQ(e, name) (strcasecmp(ext, e) == 0 ? name : NULL)

    const char *result = NULL;
    if ((result = EXT_EQ(".jpg", "JPEG"))) return result;
    if ((result = EXT_EQ(".jpeg", "JPEG"))) return result;
    if ((result = EXT_EQ(".png", "PNG"))) return result;
    if ((result = EXT_EQ(".gif", "GIF"))) return result;
    if ((result = EXT_EQ(".webp", "WebP"))) return result;
    if ((result = EXT_EQ(".bmp", "BMP"))) return result;
    if ((result = EXT_EQ(".tiff", "TIFF"))) return result;
    if ((result = EXT_EQ(".tif", "TIFF"))) return result;
    if ((result = EXT_EQ(".ico", "ICO"))) return result;
    if ((result = EXT_EQ(".apng", "APNG"))) return result;
    return "Unknown";

    #undef EXT_EQ
}
