#include "app_media_session.h"

static gboolean app_media_kind_is_valid(MediaKind active_kind) {
    return active_kind >= MEDIA_KIND_UNKNOWN && active_kind < MEDIA_KIND_COUNT;
}

ErrorCode app_media_stop_inactive_players(PixelTermApp *app, MediaKind active_kind) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_media_kind_is_valid(active_kind)) {
        g_warning("%s: active_kind=%d is invalid", G_STRFUNC, (gint)active_kind);
        return ERROR_INVALID_ARGS;
    }

    gboolean keep_gif = media_is_animated_image(active_kind);
    gboolean keep_video = media_is_video(active_kind);

    ErrorCode result = ERROR_NONE;
    if (app->gif_player && !keep_gif) {
        result = gif_player_stop(app->gif_player);
    }
    if (app->video_player && !keep_video) {
        ErrorCode stopped = video_player_stop(app->video_player);
        if (result == ERROR_NONE) result = stopped;
    }
    return result;
}
