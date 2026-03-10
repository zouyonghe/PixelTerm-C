#include "app_media_session.h"

void app_media_stop_inactive_players(PixelTermApp *app, MediaKind active_kind) {
    if (!app) {
        return;
    }

    gboolean keep_gif = media_is_animated_image(active_kind);
    gboolean keep_video = media_is_video(active_kind);

    if (app->gif_player && !keep_gif) {
        gif_player_stop(app->gif_player);
    }
    if (app->video_player && !keep_video) {
        video_player_stop(app->video_player);
    }
}
