#include "media_utils.h"

#include "common.h"

MediaKind media_classify(const char *path) {
    if (!path) {
        return MEDIA_KIND_UNKNOWN;
    }

    gboolean is_animated = is_animated_image_candidate(path);
    gboolean is_video = is_video_file(path);
    gboolean is_image = FALSE;

    if (!is_video && !is_animated) {
        is_image = is_image_file(path);
        if (!is_image && is_valid_video_file(path)) {
            is_video = TRUE;
        }
    }

    if (is_animated && is_video) {
        is_video = FALSE;
    }

    if (is_video) {
        return MEDIA_KIND_VIDEO;
    }
    if (is_animated) {
        return MEDIA_KIND_ANIMATED_IMAGE;
    }
    if (is_image) {
        return MEDIA_KIND_IMAGE;
    }
    return MEDIA_KIND_UNKNOWN;
}

gboolean media_is_image(MediaKind kind) {
    return kind == MEDIA_KIND_IMAGE || kind == MEDIA_KIND_ANIMATED_IMAGE;
}

gboolean media_is_animated_image(MediaKind kind) {
    return kind == MEDIA_KIND_ANIMATED_IMAGE;
}

gboolean media_is_video(MediaKind kind) {
    return kind == MEDIA_KIND_VIDEO;
}
