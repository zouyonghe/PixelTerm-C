#include <glib.h>

#include "app.h"

static void test_transition_null_app(void) {
    g_assert_cmpint(app_transition_mode(NULL, APP_MODE_SINGLE), ==, ERROR_MEMORY_ALLOC);
}

static void test_transition_invalid_mode(void) {
    PixelTermApp app = {0};
    app.mode = APP_MODE_SINGLE;
    g_assert_cmpint(app_transition_mode(&app, (AppMode)999), ==, ERROR_INVALID_ARGS);
}

static void test_transition_same_mode_no_side_effects(void) {
    PixelTermApp app = {0};
    app.mode = APP_MODE_SINGLE;
    app.input.single_click.pending = TRUE;
    g_assert_cmpint(app_transition_mode(&app, APP_MODE_SINGLE), ==, ERROR_NONE);
    /* No transition -> exit hook not called. */
    g_assert_true(app.input.single_click.pending);
}

static void test_transition_resets_pending_clicks(void) {
    PixelTermApp app = {0};

    /* Exit SINGLE clears single_click.pending. */
    app.mode = APP_MODE_SINGLE;
    app.input.single_click.pending = TRUE;
    g_assert_cmpint(app_transition_mode(&app, APP_MODE_PREVIEW), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_PREVIEW);
    g_assert_false(app.input.single_click.pending);

    /* Exit PREVIEW clears preview_click.pending. */
    app.input.preview_click.pending = TRUE;
    g_assert_cmpint(app_transition_mode(&app, APP_MODE_SINGLE), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_SINGLE);
    g_assert_false(app.input.preview_click.pending);

    /* Exit FILE_MANAGER clears file_manager_click.pending. */
    app.mode = APP_MODE_FILE_MANAGER;
    app.input.file_manager_click.pending = TRUE;
    g_assert_cmpint(app_transition_mode(&app, APP_MODE_PREVIEW), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_PREVIEW);
    g_assert_false(app.input.file_manager_click.pending);

    /* Exit BOOK also clears single_click.pending (shares single-click tracker). */
    app.mode = APP_MODE_BOOK;
    app.input.single_click.pending = TRUE;
    g_assert_cmpint(app_transition_mode(&app, APP_MODE_BOOK_PREVIEW), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_BOOK_PREVIEW);
    g_assert_false(app.input.single_click.pending);
}

static void test_transition_from_invalid_current_mode(void) {
    PixelTermApp app = {0};
    app.mode = (AppMode)999;
    app.input.single_click.pending = TRUE;
    g_assert_cmpint(app_transition_mode(&app, APP_MODE_PREVIEW), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_PREVIEW);
    /* Current invalid coerces to SINGLE before transitioning -> SINGLE exit hook runs. */
    g_assert_false(app.input.single_click.pending);
}

static void test_transition_matrix_all_modes_allowed(void) {
    for (gint from = APP_MODE_SINGLE; from <= APP_MODE_BOOK_PREVIEW; from++) {
        for (gint to = APP_MODE_SINGLE; to <= APP_MODE_BOOK_PREVIEW; to++) {
            PixelTermApp app = {0};
            app.mode = (AppMode)from;
            g_assert_cmpint(app_transition_mode(&app, (AppMode)to), ==, ERROR_NONE);
            g_assert_cmpint(app.mode, ==, to);
        }
    }
}

static void test_transition_to_non_single_stops_media(void) {
    PixelTermApp app = {0};
    app.mode = APP_MODE_SINGLE;

    app.gif_player = gif_player_new(4, FALSE, FALSE, FALSE, FALSE, 1.0);
    app.video_player = video_player_new(4, FALSE, FALSE, FALSE, FALSE, 1.0);
    if (!app.gif_player || !app.video_player) {
        if (app.gif_player) {
            gif_player_destroy(app.gif_player);
        }
        if (app.video_player) {
            video_player_destroy(app.video_player);
        }
        g_test_skip("media players unavailable");
        return;
    }

    app.gif_player->is_playing = TRUE;
    app.video_player->is_playing = TRUE;

    g_assert_cmpint(app_transition_mode(&app, APP_MODE_PREVIEW), ==, ERROR_NONE);
    g_assert_false(gif_player_is_playing(app.gif_player));
    g_assert_false(video_player_is_playing(app.video_player));

    gif_player_destroy(app.gif_player);
    video_player_destroy(app.video_player);
    app.gif_player = NULL;
    app.video_player = NULL;
}

static void test_transition_common_round_trip_paths(void) {
    PixelTermApp app = {0};
    app.mode = APP_MODE_SINGLE;

    g_assert_cmpint(app_transition_mode(&app, APP_MODE_PREVIEW), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_PREVIEW);

    g_assert_cmpint(app_transition_mode(&app, APP_MODE_FILE_MANAGER), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_FILE_MANAGER);

    g_assert_cmpint(app_transition_mode(&app, APP_MODE_SINGLE), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_SINGLE);

    g_assert_cmpint(app_transition_mode(&app, APP_MODE_BOOK), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_BOOK);

    g_assert_cmpint(app_transition_mode(&app, APP_MODE_BOOK_PREVIEW), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_BOOK_PREVIEW);

    g_assert_cmpint(app_transition_mode(&app, APP_MODE_BOOK), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_BOOK);
}

void register_app_mode_tests(void) {
    g_test_add_func("/app_mode/transition/null_app", test_transition_null_app);
    g_test_add_func("/app_mode/transition/invalid_mode", test_transition_invalid_mode);
    g_test_add_func("/app_mode/transition/same_mode_no_side_effects", test_transition_same_mode_no_side_effects);
    g_test_add_func("/app_mode/transition/resets_pending_clicks", test_transition_resets_pending_clicks);
    g_test_add_func("/app_mode/transition/from_invalid_current_mode", test_transition_from_invalid_current_mode);
    g_test_add_func("/app_mode/transition/matrix_all_modes_allowed", test_transition_matrix_all_modes_allowed);
    g_test_add_func("/app_mode/transition/to_non_single_stops_media", test_transition_to_non_single_stops_media);
    g_test_add_func("/app_mode/transition/common_round_trip_paths", test_transition_common_round_trip_paths);
}
