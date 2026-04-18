#include <glib.h>
#include <glib/gstdio.h>

#include <unistd.h>

#include "gif_player.h"
#include "gif_player_test_internal.h"

typedef void (*GifPlayerCaptureFunc)(gpointer user_data);

typedef struct {
    GifPlayer *player;
    const GString *rendered;
    gint rendered_width;
    gint rendered_height;
    gboolean graphics_mode;
} GifPlayerPresentCall;

static gchar *capture_output(GifPlayerCaptureFunc func, gpointer user_data) {
    gchar *template = g_strdup_printf("%s/pixelterm-gif-player-XXXXXX", g_get_tmp_dir());
    int fd = g_mkstemp(template);
    g_assert_cmpint(fd, >=, 0);

    int saved_stdout = dup(STDOUT_FILENO);
    g_assert_cmpint(saved_stdout, >=, 0);

    fflush(stdout);
    g_assert_cmpint(dup2(fd, STDOUT_FILENO), >=, 0);
    close(fd);

    func(user_data);

    fflush(stdout);
    g_assert_cmpint(dup2(saved_stdout, STDOUT_FILENO), >=, 0);
    close(saved_stdout);

    gchar *output = NULL;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(template, &output, NULL, &error));
    g_assert_no_error(error);
    g_remove(template);
    g_free(template);
    return output;
}

static void present_frame_capture(gpointer user_data) {
    GifPlayerPresentCall *call = (GifPlayerPresentCall *)user_data;
    g_assert_nonnull(call);
    gif_player_present_rendered_frame_for_test(call->player,
                                               call->rendered,
                                               call->rendered_width,
                                               call->rendered_height,
                                               call->graphics_mode);
}

static void test_gif_player_new_renderer_state(void) {
    GifPlayer *player = gif_player_new(9, FALSE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);

    if (player->renderer) {
        g_assert_true(player->owns_renderer);
    } else {
        g_assert_false(player->owns_renderer);
    }

    gif_player_destroy(player);
}

static void test_gif_player_set_renderer_ownership(void) {
    GifPlayer *player = gif_player_new(9, FALSE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
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
    GifPlayer *player = gif_player_new(9, FALSE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);

    g_assert_false(gif_player_is_playing(player));
    g_assert_false(gif_player_is_animated(player));
    g_assert_cmpint(player->current_frame, ==, 0);
    g_assert_cmpint(player->total_frames, ==, 0);

    gif_player_destroy(player);
}

static void test_gif_player_play_without_load(void) {
    GifPlayer *player = gif_player_new(9, FALSE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);

    g_assert_cmpint(gif_player_play(player), ==, ERROR_INVALID_IMAGE);

    gif_player_destroy(player);
}

static void test_gif_player_pause_stop_without_play(void) {
    GifPlayer *player = gif_player_new(9, FALSE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);

    g_assert_cmpint(gif_player_pause(player), ==, ERROR_NONE);
    g_assert_cmpint(gif_player_stop(player), ==, ERROR_NONE);
    g_assert_false(gif_player_is_playing(player));

    gif_player_destroy(player);
}

static void test_gif_player_load_invalid_path(void) {
    GifPlayer *player = gif_player_new(9, FALSE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);

    g_assert_cmpint(gif_player_load(player, "/path/does/not/exist.gif"), ==, ERROR_FILE_NOT_FOUND);

    gif_player_destroy(player);
}

static void test_gif_player_present_text_frame_skips_identical_lines(void) {
    GifPlayer *player = gif_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    if (!player) {
        g_test_skip("gif player unavailable");
        return;
    }

    gif_player_set_render_area(player, 4, 10, 2, 2, 4, 2);

    GString *rendered = g_string_new("AB\nCD\n");
    GifPlayerPresentCall call = {
        .player = player,
        .rendered = rendered,
        .rendered_width = 2,
        .rendered_height = 2,
        .graphics_mode = FALSE,
    };

    gchar *first = capture_output(present_frame_capture, &call);
    gchar *second = capture_output(present_frame_capture, &call);

    g_assert_nonnull(g_strstr_len(first, -1, "\033[2;1H"));
    g_assert_cmpstr(second, ==, "");

    g_free(first);
    g_free(second);
    g_string_free(rendered, TRUE);
    gif_player_destroy(player);
}

static void test_gif_player_layout_change_repaints_cached_text_frame(void) {
    GifPlayer *player = gif_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    if (!player) {
        g_test_skip("gif player unavailable");
        return;
    }

    GString *rendered = g_string_new("AB\nCD\n");
    GifPlayerPresentCall call = {
        .player = player,
        .rendered = rendered,
        .rendered_width = 2,
        .rendered_height = 2,
        .graphics_mode = FALSE,
    };

    gif_player_set_render_area(player, 4, 10, 2, 2, 4, 2);
    g_free(capture_output(present_frame_capture, &call));

    gif_player_set_render_area(player, 4, 10, 4, 2, 4, 2);
    gchar *output = capture_output(present_frame_capture, &call);

    g_assert_nonnull(g_strstr_len(output, -1, "\033[4;1H"));

    g_free(output);
    g_string_free(rendered, TRUE);
    gif_player_destroy(player);
}

void register_gif_player_tests(void) {
    g_test_add_func("/gif_player/new_renderer_state", test_gif_player_new_renderer_state);
    g_test_add_func("/gif_player/set_renderer_ownership", test_gif_player_set_renderer_ownership);
    g_test_add_func("/gif_player/default_state", test_gif_player_default_state);
    g_test_add_func("/gif_player/play_without_load", test_gif_player_play_without_load);
    g_test_add_func("/gif_player/pause_stop_without_play", test_gif_player_pause_stop_without_play);
    g_test_add_func("/gif_player/load_invalid_path", test_gif_player_load_invalid_path);
    g_test_add_func("/gif_player/present_text_frame/skips_identical_lines",
                    test_gif_player_present_text_frame_skips_identical_lines);
    g_test_add_func("/gif_player/present_text_frame/repaints_after_layout_change",
                    test_gif_player_layout_change_repaints_cached_text_frame);
}
