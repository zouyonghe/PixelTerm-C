#include <glib.h>

#include "app_media_session.h"

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

static gboolean init_gif_player_only(PixelTermApp *app) {
    if (!app) {
        return FALSE;
    }
    app->gif_player = gif_player_new(1, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!app->gif_player) {
        destroy_test_players(app);
        return FALSE;
    }
    return TRUE;
}

static gboolean init_video_player_only(PixelTermApp *app) {
    if (!app) {
        return FALSE;
    }
    app->video_player = video_player_new(1, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!app->video_player) {
        destroy_test_players(app);
        return FALSE;
    }
    return TRUE;
}

static void test_stop_inactive_players_null_app(void) {
    app_media_stop_inactive_players(NULL, MEDIA_KIND_IMAGE);
}

static void test_stop_inactive_players_both_null(void) {
    PixelTermApp app = {0};

    app_media_stop_inactive_players(&app, MEDIA_KIND_ANIMATED_IMAGE);

    g_assert_null(app.gif_player);
    g_assert_null(app.video_player);
}

static void test_stop_inactive_players_null_gif_only_video_initialized(void) {
    PixelTermApp app = {0};
    if (!init_video_player_only(&app)) {
        g_test_skip("video player unavailable");
        return;
    }

    app.video_player->is_playing = TRUE;

    app_media_stop_inactive_players(&app, MEDIA_KIND_ANIMATED_IMAGE);

    g_assert_null(app.gif_player);
    g_assert_false(app.video_player->is_playing);

    destroy_test_players(&app);
}

static void test_stop_inactive_players_null_video_only_gif_initialized(void) {
    PixelTermApp app = {0};
    if (!init_gif_player_only(&app)) {
        g_test_skip("gif player unavailable");
        return;
    }

    app.gif_player->is_playing = TRUE;

    app_media_stop_inactive_players(&app, MEDIA_KIND_ANIMATED_IMAGE);

    g_assert_true(app.gif_player->is_playing);
    g_assert_null(app.video_player);

    destroy_test_players(&app);
}

static void test_stop_inactive_players_rejects_invalid_media_kind(void) {
    PixelTermApp app = {0};
    if (!init_test_players(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app.gif_player->is_playing = TRUE;
    app.video_player->is_playing = TRUE;

    g_test_expect_message(NULL, G_LOG_LEVEL_CRITICAL, "*active_kind*");
    app_media_stop_inactive_players(&app, (MediaKind)99);
    g_test_assert_expected_messages();

    g_assert_true(app.gif_player->is_playing);
    g_assert_true(app.video_player->is_playing);

    destroy_test_players(&app);
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
    g_test_add_func("/app_media_session/null_app", test_stop_inactive_players_null_app);
    g_test_add_func("/app_media_session/both_null", test_stop_inactive_players_both_null);
    g_test_add_func("/app_media_session/null_gif_only_video_initialized",
                    test_stop_inactive_players_null_gif_only_video_initialized);
    g_test_add_func("/app_media_session/null_video_only_gif_initialized",
                    test_stop_inactive_players_null_video_only_gif_initialized);
    g_test_add_func("/app_media_session/rejects_invalid_media_kind",
                    test_stop_inactive_players_rejects_invalid_media_kind);
    g_test_add_func("/app_media_session/animated_image/keeps_gif_and_stops_video",
                    test_animated_image_keeps_gif_and_stops_video);
    g_test_add_func("/app_media_session/video/keeps_video_and_stops_gif",
                    test_video_keeps_video_and_stops_gif);
    g_test_add_func("/app_media_session/static_image/stops_all_players",
                    test_static_image_stops_all_players);
}
