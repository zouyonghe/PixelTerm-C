#ifndef MEDIA_UTILS_H
#define MEDIA_UTILS_H

#include <glib.h>

typedef enum {
    MEDIA_KIND_UNKNOWN = 0,
    MEDIA_KIND_IMAGE,
    MEDIA_KIND_ANIMATED_IMAGE,
    MEDIA_KIND_VIDEO
} MediaKind;

MediaKind media_classify(const char *path);
gboolean media_is_image(MediaKind kind);
gboolean media_is_animated_image(MediaKind kind);
gboolean media_is_video(MediaKind kind);

#endif
