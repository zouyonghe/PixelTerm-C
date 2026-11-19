#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <glib.h>

#include "app_media_session.h"
#include "app_render.h"
#include "app_runtime.h"
#include "terminal_output.h"
#include "test_output_stream.h"
#include <errno.h>

void app_preloader_clear_queue(PixelTermApp *app) {
    if (app && app->preloader) preloader_clear_queue(app->preloader);
}

typedef struct {
    guint attempts;
    guint failures;
    GString *bytes;
} MediaStopOutput;

static ssize_t failed_media_stop_write(void *data, const char *bytes, size_t length) {
    MediaStopOutput *output = data;
    if (++output->attempts <= output->failures) {
        errno = EIO;
        return -1;
    }
    g_string_append_len(output->bytes, bytes, length);
    return (ssize_t)length;
}

static void test_failed_stop_is_reported_before_render(gconstpointer data) {
    gint mode = GPOINTER_TO_INT(data);
    gboolean gif = mode == 1;
    PixelTermApp app = {.mode = APP_MODE_SINGLE, .current_index = 0, .total_images = 1};
    app.image_files = g_list_append(NULL, g_strdup("still.png"));
    app.gif_player = gif_player_new(1, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    app.video_player = video_player_new(1, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(app.gif_player);
    g_assert_nonnull(app.video_player);
    if (gif) {
        app.gif_player->kitty_animation_id = 42;
        app.gif_player->kitty_animation_prepared = TRUE;
    } else {
        app.video_player->kitty_direct_image_id = 42;
        app.video_player->kitty_direct_image_present = TRUE;
    }
    MediaStopOutput output = {.failures = gif ? 1 : 2, .bytes = g_string_new(NULL)};
    FILE *stream = test_output_stream(&output, failed_media_stop_write);
    FILE *recovery = tmpfile();
    g_assert_nonnull(stream);
    g_assert_nonnull(recovery);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    ErrorCode result;
    if (mode == 2) {
        result = app_transition_mode(&app, APP_MODE_PREVIEW);
    } else if (mode == 3) {
        result = app_prepare_mode_entry(&app, TRUE);
    } else {
        result = app_render_current_image(&app);
    }
    guint attempts_before_recovery = output.attempts;
    stdout = recovery;
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    rewind(recovery);
    gchar bytes[256] = {0};
    gsize length = fread(bytes, 1, sizeof(bytes) - 1, recovery);
    fclose(recovery);
    gif_player_destroy(app.gif_player);
    video_player_destroy(app.video_player);
    g_list_free_full(app.image_files, g_free);
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(app.mode, ==, APP_MODE_SINGLE);
    g_assert_true(recovered);
    g_assert_cmpmem(bytes, length, "\033\\", 2);
    g_assert_cmpuint(attempts_before_recovery, ==, output.failures);
    g_assert_cmpuint(output.bytes->len, ==, 0);
    g_string_free(output.bytes, TRUE);
}

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
    app->gif_player = gif_player_new(1, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    app->video_player = video_player_new(1, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    app->gif_player = gif_player_new(1, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
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
    app->video_player = video_player_new(1, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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

static void test_stop_inactive_players_null_gif_only_video_kept(void) {
    PixelTermApp app = {0};
    if (!init_video_player_only(&app)) {
        g_test_skip("video player unavailable");
        return;
    }

    app.video_player->is_playing = TRUE;

    app_media_stop_inactive_players(&app, MEDIA_KIND_VIDEO);

    g_assert_null(app.gif_player);
    g_assert_true(app.video_player->is_playing);

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

    g_test_expect_message(NULL, G_LOG_LEVEL_WARNING, "*active_kind*");
    app_media_stop_inactive_players(&app, (MediaKind)99);
    g_test_assert_expected_messages();

    g_assert_true(app.gif_player->is_playing);
    g_assert_true(app.video_player->is_playing);

    destroy_test_players(&app);
}

static void test_unknown_stops_all_players(void) {
    PixelTermApp app = {0};
    if (!init_test_players(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app.gif_player->is_playing = TRUE;
    app.video_player->is_playing = TRUE;

    app_media_stop_inactive_players(&app, MEDIA_KIND_UNKNOWN);

    g_assert_false(app.gif_player->is_playing);
    g_assert_false(app.video_player->is_playing);

    destroy_test_players(&app);
}

static void test_unknown_stops_all_players_idempotent(void) {
    PixelTermApp app = {0};
    if (!init_test_players(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app.gif_player->is_playing = FALSE;
    app.video_player->is_playing = FALSE;

    app_media_stop_inactive_players(&app, MEDIA_KIND_UNKNOWN);

    g_assert_false(app.gif_player->is_playing);
    g_assert_false(app.video_player->is_playing);

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
    g_test_add_data_func("/app_media_session/output/video_stop_aborts_render", NULL, test_failed_stop_is_reported_before_render);
    g_test_add_data_func("/app_media_session/output/gif_stop_aborts_render", GINT_TO_POINTER(1), test_failed_stop_is_reported_before_render);
    g_test_add_data_func("/app_media_session/output/stop_failure_preserves_mode", GINT_TO_POINTER(2), test_failed_stop_is_reported_before_render);
    g_test_add_data_func("/app_media_session/output/prepare_stop_failure_reports_error", GINT_TO_POINTER(3), test_failed_stop_is_reported_before_render);
    g_test_add_func("/app_media_session/null_app", test_stop_inactive_players_null_app);
    g_test_add_func("/app_media_session/both_null", test_stop_inactive_players_both_null);
    g_test_add_func("/app_media_session/null_gif_only_video_initialized",
                    test_stop_inactive_players_null_gif_only_video_initialized);
    g_test_add_func("/app_media_session/null_gif_only_video_kept",
                    test_stop_inactive_players_null_gif_only_video_kept);
    g_test_add_func("/app_media_session/null_video_only_gif_initialized",
                    test_stop_inactive_players_null_video_only_gif_initialized);
    g_test_add_func("/app_media_session/rejects_invalid_media_kind",
                    test_stop_inactive_players_rejects_invalid_media_kind);
    g_test_add_func("/app_media_session/unknown/stops_all_players",
                    test_unknown_stops_all_players);
    g_test_add_func("/app_media_session/unknown/stops_all_players_idempotent",
                    test_unknown_stops_all_players_idempotent);
    g_test_add_func("/app_media_session/animated_image/keeps_gif_and_stops_video",
                    test_animated_image_keeps_gif_and_stops_video);
    g_test_add_func("/app_media_session/video/keeps_video_and_stops_gif",
                    test_video_keeps_video_and_stops_gif);
    g_test_add_func("/app_media_session/static_image/stops_all_players",
                    test_static_image_stops_all_players);
}
