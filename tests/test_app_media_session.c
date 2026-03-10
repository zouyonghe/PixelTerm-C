#include <glib.h>

#include "app_state.h"
#include "media_utils.h"

void app_media_stop_inactive_players(PixelTermApp *app, MediaKind active_kind);

static void destroy_test_players(PixelTermApp *app) {
    if (!app) {
        return;
    }
    if (app->gif_player) {
        gif_player_destroy(app->gif_player);
        app->gif_player = NULL;
    }
    if (app->video_player) {
        video_player_destroy(app->video_player);
        app->video_player = NULL;
    }
}

static gboolean init_test_players(PixelTermApp *app) {
    if (!app) {
        return FALSE;
    }
    app->gif_player = gif_player_new(1, TRUE, FALSE, FALSE, FALSE, 1.0);
    app->video_player = video_player_new(1, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!app->gif_player || !app->video_player) {
        destroy_test_players(app);
        return FALSE;
    }
    return TRUE;
}

static void test_animated_image_keeps_gif_and_stops_video(void) {
    PixelTermApp app = {0};
    if (!init_test_players(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app.gif_player->is_playing = TRUE;
    app.video_player->is_playing = TRUE;

    app_media_stop_inactive_players(&app, MEDIA_KIND_ANIMATED_IMAGE);

    g_assert_true(app.gif_player->is_playing);
    g_assert_false(app.video_player->is_playing);

    destroy_test_players(&app);
}

static void test_video_keeps_video_and_stops_gif(void) {
    PixelTermApp app = {0};
    if (!init_test_players(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app.gif_player->is_playing = TRUE;
    app.video_player->is_playing = TRUE;

    app_media_stop_inactive_players(&app, MEDIA_KIND_VIDEO);

    g_assert_false(app.gif_player->is_playing);
    g_assert_true(app.video_player->is_playing);

    destroy_test_players(&app);
}

static void test_static_image_stops_all_players(void) {
    PixelTermApp app = {0};
    if (!init_test_players(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app.gif_player->is_playing = TRUE;
    app.video_player->is_playing = TRUE;

    app_media_stop_inactive_players(&app, MEDIA_KIND_IMAGE);

    g_assert_false(app.gif_player->is_playing);
    g_assert_false(app.video_player->is_playing);

    destroy_test_players(&app);
}

void register_app_media_session_tests(void) {
    g_test_add_func("/app_media_session/animated_image/keeps_gif_and_stops_video",
                    test_animated_image_keeps_gif_and_stops_video);
    g_test_add_func("/app_media_session/video/keeps_video_and_stops_gif",
                    test_video_keeps_video_and_stops_gif);
    g_test_add_func("/app_media_session/static_image/stops_all_players",
                    test_static_image_stops_all_players);
}
