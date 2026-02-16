#include "input_dispatch_media_internal.h"

#include <glib.h>

#include "media_utils.h"

gboolean input_dispatch_current_is_video(const PixelTermApp *app) {
    if (!app_is_single_mode(app)) {
        return FALSE;
    }
    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return FALSE;
    }
    MediaKind media_kind = media_classify(filepath);
    return media_is_video(media_kind);
}

gboolean input_dispatch_current_is_animated_image(const PixelTermApp *app) {
    if (!app_is_single_mode(app)) {
        return FALSE;
    }
    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return FALSE;
    }
    MediaKind media_kind = media_classify(filepath);
    if (!media_is_animated_image(media_kind)) {
        return FALSE;
    }
    if (app->gif_player && app->gif_player->filepath &&
        g_strcmp0(app->gif_player->filepath, filepath) == 0) {
        return gif_player_is_animated(app->gif_player);
    }
    return FALSE;
}
