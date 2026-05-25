#define _GNU_SOURCE
#include "exif.h"
#include <libexif/exif-data.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *exif_get_data(const char *path)
{
    ExifData *ed = exif_data_new_from_file(path);
    if (!ed) return NULL;

    char result[2048] = {0};
    char value[256];
    int has_data = 0;

    ExifEntry *entry;

    /* IFD_0: camera make/model */
    entry = exif_content_get_entry(ed->ifd[EXIF_IFD_0], EXIF_TAG_MAKE);
    if (entry) {
        exif_entry_get_value(entry, value, sizeof(value));
        if (value[0]) {
            snprintf(result + strlen(result), sizeof(result) - strlen(result),
                     "Make: %s\n", value);
            has_data = 1;
        }
    }

    entry = exif_content_get_entry(ed->ifd[EXIF_IFD_0], EXIF_TAG_MODEL);
    if (entry) {
        exif_entry_get_value(entry, value, sizeof(value));
        if (value[0]) {
            snprintf(result + strlen(result), sizeof(result) - strlen(result),
                     "Model: %s\n", value);
            has_data = 1;
        }
    }

    /* EXIF sub-IFD for photo settings */
    entry = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF], EXIF_TAG_DATE_TIME_ORIGINAL);
    if (entry) {
        exif_entry_get_value(entry, value, sizeof(value));
        if (value[0]) {
            snprintf(result + strlen(result), sizeof(result) - strlen(result),
                     "Date: %s\n", value);
            has_data = 1;
        }
    }

    entry = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF], EXIF_TAG_EXPOSURE_TIME);
    if (entry) {
        exif_entry_get_value(entry, value, sizeof(value));
        if (value[0]) {
            snprintf(result + strlen(result), sizeof(result) - strlen(result),
                     "Exposure: %ss\n", value);
            has_data = 1;
        }
    }

    entry = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF], EXIF_TAG_FNUMBER);
    if (entry) {
        exif_entry_get_value(entry, value, sizeof(value));
        if (value[0]) {
            snprintf(result + strlen(result), sizeof(result) - strlen(result),
                     "Aperture: f/%s\n", value);
            has_data = 1;
        }
    }

    entry = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF], EXIF_TAG_ISO_SPEED_RATINGS);
    if (entry) {
        exif_entry_get_value(entry, value, sizeof(value));
        if (value[0]) {
            snprintf(result + strlen(result), sizeof(result) - strlen(result),
                     "ISO: %s\n", value);
            has_data = 1;
        }
    }

    /* IFD_0: orientation */
    entry = exif_content_get_entry(ed->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
    if (entry) {
        exif_entry_get_value(entry, value, sizeof(value));
        if (value[0]) {
            snprintf(result + strlen(result), sizeof(result) - strlen(result),
                     "Orientation: %s\n", value);
            has_data = 1;
        }
    }

    entry = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF], EXIF_TAG_FOCAL_LENGTH);
    if (entry) {
        exif_entry_get_value(entry, value, sizeof(value));
        if (value[0]) {
            snprintf(result + strlen(result), sizeof(result) - strlen(result),
                     "Focal Length: %s\n", value);
            has_data = 1;
        }
    }

    entry = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF], EXIF_TAG_FLASH);
    if (entry) {
        exif_entry_get_value(entry, value, sizeof(value));
        if (value[0]) {
            snprintf(result + strlen(result), sizeof(result) - strlen(result),
                     "Flash: %s\n", value);
            has_data = 1;
        }
    }

    exif_data_unref(ed);

    if (!has_data) return NULL;
    return strdup(result);
}
