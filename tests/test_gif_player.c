#include <glib.h>

#include "gif_player.h"

static void test_gif_player_new_renderer_state(void) {
    GifPlayer *player = gif_player_new(9, FALSE);
    g_assert_nonnull(player);

    if (player->renderer) {
        g_assert_true(player->owns_renderer);
    } else {
        g_assert_false(player->owns_renderer);
    }

    gif_player_destroy(player);
}

static void test_gif_player_set_renderer_ownership(void) {
    GifPlayer *player = gif_player_new(9, FALSE);
    g_assert_nonnull(player);

    ImageRenderer *renderer = renderer_create();
    g_assert_nonnull(renderer);

    gif_player_set_renderer(player, renderer);
    g_assert_true(player->renderer == renderer);
    g_assert_false(player->owns_renderer);

    gif_player_destroy(player);
    renderer_destroy(renderer);
}

void register_gif_player_tests(void) {
    g_test_add_func("/gif_player/new_renderer_state", test_gif_player_new_renderer_state);
    g_test_add_func("/gif_player/set_renderer_ownership", test_gif_player_set_renderer_ownership);
}
