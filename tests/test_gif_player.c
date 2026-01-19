#include <glib.h>

#include "gif_player.h"

static void test_gif_player_new_renderer_state(void) {
    GifPlayer *player = gif_player_new(9, FALSE, FALSE, FALSE, FALSE, 1.0);
    g_assert_nonnull(player);

    if (player->renderer) {
        g_assert_true(player->owns_renderer);
    } else {
        g_assert_false(player->owns_renderer);
    }

    gif_player_destroy(player);
}

static void test_gif_player_set_renderer_ownership(void) {
    GifPlayer *player = gif_player_new(9, FALSE, FALSE, FALSE, FALSE, 1.0);
    g_assert_nonnull(player);

    ImageRenderer *renderer = renderer_create();
    g_assert_nonnull(renderer);

    gif_player_set_renderer(player, renderer);
    g_assert_true(player->renderer == renderer);
    g_assert_false(player->owns_renderer);

    gif_player_destroy(player);
    renderer_destroy(renderer);
}

static void test_gif_player_default_state(void) {
    GifPlayer *player = gif_player_new(9, FALSE, FALSE, FALSE, FALSE, 1.0);
    g_assert_nonnull(player);

    g_assert_false(gif_player_is_playing(player));
    g_assert_false(gif_player_is_animated(player));
    g_assert_cmpint(player->current_frame, ==, 0);
    g_assert_cmpint(player->total_frames, ==, 0);

    gif_player_destroy(player);
}

static void test_gif_player_play_without_load(void) {
    GifPlayer *player = gif_player_new(9, FALSE, FALSE, FALSE, FALSE, 1.0);
    g_assert_nonnull(player);

    g_assert_cmpint(gif_player_play(player), ==, ERROR_INVALID_IMAGE);

    gif_player_destroy(player);
}

static void test_gif_player_pause_stop_without_play(void) {
    GifPlayer *player = gif_player_new(9, FALSE, FALSE, FALSE, FALSE, 1.0);
    g_assert_nonnull(player);

    g_assert_cmpint(gif_player_pause(player), ==, ERROR_NONE);
    g_assert_cmpint(gif_player_stop(player), ==, ERROR_NONE);
    g_assert_false(gif_player_is_playing(player));

    gif_player_destroy(player);
}

static void test_gif_player_load_invalid_path(void) {
    GifPlayer *player = gif_player_new(9, FALSE, FALSE, FALSE, FALSE, 1.0);
    g_assert_nonnull(player);

    g_assert_cmpint(gif_player_load(player, "/path/does/not/exist.gif"), ==, ERROR_FILE_NOT_FOUND);

    gif_player_destroy(player);
}

void register_gif_player_tests(void) {
    g_test_add_func("/gif_player/new_renderer_state", test_gif_player_new_renderer_state);
    g_test_add_func("/gif_player/set_renderer_ownership", test_gif_player_set_renderer_ownership);
    g_test_add_func("/gif_player/default_state", test_gif_player_default_state);
    g_test_add_func("/gif_player/play_without_load", test_gif_player_play_without_load);
    g_test_add_func("/gif_player/pause_stop_without_play", test_gif_player_pause_stop_without_play);
    g_test_add_func("/gif_player/load_invalid_path", test_gif_player_load_invalid_path);
}
