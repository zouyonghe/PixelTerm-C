#include <glib.h>
#include <glib/gstdio.h>

#include <unistd.h>

#include "gif_player.h"
#include "kitty_graphics.h"
#include "process_env.h"

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "gif_player_test_internal.h"

typedef void (*GifPlayerCaptureFunc)(gpointer user_data);

typedef struct {
    GifPlayer *player;
    const GString *rendered;
    gint rendered_width;
    gint rendered_height;
    gboolean graphics_mode;
} GifPlayerPresentCall;

typedef struct {
    GifPlayer *player;
    gint width;
    gint height;
} GifPlayerLayoutCall;

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

static void pause_capture(gpointer user_data) {
    gif_player_pause((GifPlayer *)user_data);
}

static void stop_capture(gpointer user_data) {
    gif_player_stop((GifPlayer *)user_data);
}

static void play_capture(gpointer user_data) {
    gif_player_play((GifPlayer *)user_data);
}

static gboolean native_kitty_shm_available_for_test(void) {
    const guint8 pixel[4] = {255, 0, 0, 255};
    KittyGraphicsFrame *frame = kitty_graphics_animation_root_new_shm_rgba(
        pixel, 1, 1, 4, 1, 1, 1);
    if (!frame) {
        return FALSE;
    }
    kitty_graphics_frame_free(frame);
    return TRUE;
}

static void next_frame_capture(gpointer user_data) {
    gif_player_render_next_frame_for_test((GifPlayer *)user_data);
}

static gboolean unchanged_frame_advance(GdkPixbufAnimationIter *iter,
                                        gconstpointer current_time) {
    (void)iter;
    (void)current_time;
    return FALSE;
}

static void layout_capture(gpointer user_data) {
    GifPlayerLayoutCall *call = user_data;
    gif_player_set_render_area(call->player,
                               call->width,
                               call->height,
                               2,
                               call->height - 1,
                               call->width,
                               call->height - 1);
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
    g_assert_cmpuint(player->kitty_animation_id, ==, 0);
    g_assert_false(player->kitty_animation_prepared);
    g_assert_false(player->kitty_animation_complete);
    g_assert_false(player->kitty_animation_disabled);
    g_assert_cmpuint(player->kitty_animation_frame_count, ==, 0);

    gif_player_destroy(player);
}

static void test_gif_player_native_animation_pause_and_stop(void) {
    GifPlayer *player = gif_player_new(4, FALSE, FALSE, TRUE, FALSE,
                                      TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);

    player->is_playing = TRUE;
    player->kitty_animation_id = 42;
    player->kitty_animation_prepared = TRUE;
    gchar *pause_output = capture_output(pause_capture, player);

    g_assert_nonnull(strstr(pause_output, "\033_Ga=a,i=42,s=1,q=2\033\\"));
    g_assert_true(player->kitty_animation_prepared);
    g_assert_false(player->is_playing);

    gchar *stop_output = capture_output(stop_capture, player);
    g_assert_nonnull(strstr(stop_output, "\033_Ga=d,d=I,i=42,q=2\033\\"));
    g_assert_cmpuint(player->kitty_animation_id, ==, 0);
    g_assert_false(player->kitty_animation_prepared);
    g_assert_false(player->kitty_animation_complete);
    g_assert_cmpuint(player->kitty_animation_frame_count, ==, 0);

    g_free(pause_output);
    g_free(stop_output);
    gif_player_destroy(player);
}

static void test_gif_player_counts_gif_image_blocks(void) {
    static const guint8 gif[] = {
        'G', 'I', 'F', '8', '9', 'a',
        1, 0, 1, 0, 0, 0, 0,
        0x21, 0xF9, 4, 0, 10, 0, 0, 0,
        0x2C, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 1, 0, 0,
        0x2C, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 1, 0, 0,
        0x3B
    };
    static const guint8 truncated[] = {'G', 'I', 'F', '8', '9', 'a', 1};

    g_assert_cmpuint(gif_player_count_gif_frames_for_test(gif, sizeof(gif)), ==, 2);
    g_assert_cmpuint(gif_player_count_gif_frames_for_test(truncated, sizeof(truncated)), ==, 0);
    g_assert_cmpuint(gif_player_count_gif_frames_for_test(NULL, 0), ==, 0);
}

static void test_gif_player_counts_webp_animation_frames(void) {
    static const guint8 webp[92] = {
        [0] = 'R', [1] = 'I', [2] = 'F', [3] = 'F', [4] = 0x54,
        [8] = 'W', [9] = 'E', [10] = 'B', [11] = 'P',
        [12] = 'V', [13] = 'P', [14] = '8', [15] = 'X', [16] = 10,
        [20] = 2,
        [30] = 'A', [31] = 'N', [32] = 'I', [33] = 'M', [34] = 6,
        [44] = 'A', [45] = 'N', [46] = 'M', [47] = 'F', [48] = 16,
        [68] = 'A', [69] = 'N', [70] = 'M', [71] = 'F', [72] = 16
    };
    static const guint8 truncated[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'};
    static const guint8 trailing[93] = {
        [0] = 'R', [1] = 'I', [2] = 'F', [3] = 'F', [4] = 0x55,
        [8] = 'W', [9] = 'E', [10] = 'B', [11] = 'P',
        [12] = 'V', [13] = 'P', [14] = '8', [15] = 'X', [16] = 10,
        [20] = 2,
        [30] = 'A', [31] = 'N', [32] = 'I', [33] = 'M', [34] = 6,
        [44] = 'A', [45] = 'N', [46] = 'M', [47] = 'F', [48] = 16,
        [68] = 'A', [69] = 'N', [70] = 'M', [71] = 'F', [72] = 16
    };

    g_assert_cmpuint(gif_player_count_webp_frames_for_test(webp, sizeof(webp)), ==, 2);
    g_assert_cmpuint(gif_player_count_webp_frames_for_test(truncated, sizeof(truncated)), ==, 0);
    g_assert_cmpuint(gif_player_count_webp_frames_for_test(trailing, sizeof(trailing)), ==, 0);
    g_assert_cmpuint(gif_player_count_webp_frames_for_test(NULL, 0), ==, 0);
}

static void test_gif_player_native_animation_starts_with_root_frame(void) {
#ifdef __ANDROID__
    g_test_skip("POSIX shared memory unavailable on Android");
#else
    pixelterm_env_set_for_test("TERM", "xterm-kitty");
    pixelterm_env_set_for_test("KITTY_WINDOW_ID", "1");
    pixelterm_env_unset_for_test("SSH_CONNECTION");
    pixelterm_env_unset_for_test("SSH_CLIENT");
    pixelterm_env_unset_for_test("TMUX");
    pixelterm_env_unset_for_test("STY");
    if (!native_kitty_shm_available_for_test()) {
        g_test_skip("native Kitty SHM unavailable");
        pixelterm_env_reset_for_test();
        return;
    }

    GifPlayer *player = gif_player_new(4, FALSE, FALSE, TRUE, FALSE,
                                      TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);

    GdkPixbuf *red = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
    GdkPixbuf *blue = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
    gdk_pixbuf_fill(red, 0xff0000ff);
    gdk_pixbuf_fill(blue, 0x0000ffff);
    GdkPixbufSimpleAnim *animation = gdk_pixbuf_simple_anim_new(1, 1, 10.0);
    gdk_pixbuf_simple_anim_set_loop(animation, TRUE);
    gdk_pixbuf_simple_anim_add_frame(animation, red);
    gdk_pixbuf_simple_anim_add_frame(animation, blue);
    g_object_unref(red);
    g_object_unref(blue);

    player->animation = GDK_PIXBUF_ANIMATION(animation);
    player->iter = gdk_pixbuf_animation_get_iter(player->animation, NULL);
    player->is_animated = TRUE;
    player->total_frames = 2;

    gchar *output = capture_output(play_capture, player);

    g_assert_nonnull(strstr(output, "\033_Ga=T"));
    g_assert_nonnull(strstr(output, ",i="));
    g_assert_nonnull(strstr(output, "\033_Ga=a,i="));
    g_assert_nonnull(strstr(output, ",r=1,z=100,q=2\033\\"));
    g_assert_nonnull(strstr(output, ",s=2,q=2\033\\"));
    g_assert_true(player->kitty_animation_prepared);
    g_assert_false(player->kitty_animation_complete);
    g_assert_cmpuint(player->kitty_animation_frame_count, ==, 1);
    g_assert_cmpint(player->kitty_animation_source_width, ==, 1);
    g_assert_cmpint(player->kitty_animation_source_height, ==, 1);
    g_assert_cmpint(player->kitty_animation_display_width, >, 0);
    g_assert_cmpint(player->kitty_animation_display_height, >, 0);
    g_assert_cmpuint(player->timer_id, !=, 0);

    g_free(capture_output(pause_capture, player));
    g_free(capture_output(stop_capture, player));
    gif_player_destroy(player);
    g_free(output);
    pixelterm_env_reset_for_test();
#endif
}

static void test_gif_player_native_animation_rejects_geometry_changes(void) {
    GifPlayer *player = gif_player_new(4, FALSE, FALSE, TRUE, FALSE,
                                      TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);
    player->kitty_animation_source_width = 320;
    player->kitty_animation_source_height = 240;

    g_assert_true(gif_player_native_geometry_matches_for_test(player, 320, 240));
    g_assert_false(gif_player_native_geometry_matches_for_test(player, 319, 240));
    g_assert_false(gif_player_native_geometry_matches_for_test(player, 320, 239));
    g_assert_false(gif_player_native_geometry_matches_for_test(NULL, 320, 240));

    gif_player_destroy(player);
}

static void test_gif_player_native_animation_hands_complete_loop_to_kitty(void) {
#ifdef __ANDROID__
    g_test_skip("POSIX shared memory unavailable on Android");
#else
    pixelterm_env_set_for_test("TERM", "xterm-kitty");
    pixelterm_env_set_for_test("KITTY_WINDOW_ID", "1");
    pixelterm_env_unset_for_test("SSH_CONNECTION");
    pixelterm_env_unset_for_test("SSH_CLIENT");
    pixelterm_env_unset_for_test("TMUX");
    pixelterm_env_unset_for_test("STY");
    if (!native_kitty_shm_available_for_test()) {
        g_test_skip("native Kitty SHM unavailable");
        pixelterm_env_reset_for_test();
        return;
    }

    GifPlayer *player = gif_player_new(4, FALSE, FALSE, TRUE, FALSE,
                                      TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);

    GdkPixbuf *red = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
    GdkPixbuf *blue = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
    gdk_pixbuf_fill(red, 0xff0000ff);
    gdk_pixbuf_fill(blue, 0x0000ffff);
    GdkPixbufSimpleAnim *animation = gdk_pixbuf_simple_anim_new(1, 1, 10.0);
    gdk_pixbuf_simple_anim_set_loop(animation, TRUE);
    gdk_pixbuf_simple_anim_add_frame(animation, red);
    gdk_pixbuf_simple_anim_add_frame(animation, blue);
    g_object_unref(red);
    g_object_unref(blue);

    player->animation = GDK_PIXBUF_ANIMATION(animation);
    player->iter = gdk_pixbuf_animation_get_iter(player->animation, NULL);
    player->is_animated = TRUE;
    player->total_frames = 2;
    g_free(capture_output(play_capture, player));

    g_source_remove(player->timer_id);
    player->timer_id = 0;
    gchar *output = capture_output(next_frame_capture, player);

    g_assert_nonnull(strstr(output, "\033_Ga=f"));
    g_assert_nonnull(strstr(output, ",z="));
    g_assert_nonnull(strstr(output, ",s=3,v=1,q=2\033\\"));
    g_assert_true(player->kitty_animation_complete);
    g_assert_cmpuint(player->kitty_animation_frame_count, ==, 2);
    g_assert_cmpint(player->kitty_animation_elapsed_ms, ==, 100);
    g_assert_cmpuint(player->timer_id, ==, 0);

    GifPlayerLayoutCall layout_call = {player, 40, 20};
    gchar *layout_output = capture_output(layout_capture, &layout_call);
    g_assert_nonnull(strstr(layout_output, "\033_Ga=d,d=I"));
    g_assert_nonnull(strstr(layout_output, "\033_Ga=T"));
    g_assert_true(player->kitty_animation_prepared);
    g_assert_false(player->kitty_animation_complete);
    g_assert_cmpuint(player->kitty_animation_frame_count, ==, 1);
    g_assert_cmpuint(player->timer_id, !=, 0);

    g_free(capture_output(stop_capture, player));
    gif_player_destroy(player);
    g_free(output);
    g_free(layout_output);
    pixelterm_env_reset_for_test();
#endif
}

static void test_gif_player_native_animation_waits_when_frame_is_unchanged(void) {
#ifdef __ANDROID__
    g_test_skip("POSIX shared memory unavailable on Android");
#else
    pixelterm_env_set_for_test("TERM", "xterm-kitty");
    pixelterm_env_set_for_test("KITTY_WINDOW_ID", "1");
    pixelterm_env_unset_for_test("SSH_CONNECTION");
    pixelterm_env_unset_for_test("SSH_CLIENT");
    pixelterm_env_unset_for_test("TMUX");
    pixelterm_env_unset_for_test("STY");
    if (!native_kitty_shm_available_for_test()) {
        g_test_skip("native Kitty SHM unavailable");
        pixelterm_env_reset_for_test();
        return;
    }

    GifPlayer *player = gif_player_new(4, FALSE, FALSE, TRUE, FALSE,
                                      TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);
    GdkPixbuf *first = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
    GdkPixbuf *second = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
    GdkPixbufSimpleAnim *animation = gdk_pixbuf_simple_anim_new(1, 1, 10.0);
    gdk_pixbuf_simple_anim_set_loop(animation, TRUE);
    gdk_pixbuf_simple_anim_add_frame(animation, first);
    gdk_pixbuf_simple_anim_add_frame(animation, second);
    g_object_unref(first);
    g_object_unref(second);
    player->animation = GDK_PIXBUF_ANIMATION(animation);
    player->iter = gdk_pixbuf_animation_get_iter(player->animation, NULL);
    player->is_animated = TRUE;
    player->total_frames = 2;

    g_free(capture_output(play_capture, player));
    g_source_remove(player->timer_id);
    player->timer_id = 0;
    player->kitty_animation_current_delay_ms = 130;
    player->kitty_animation_elapsed_ms = 0;
    gif_player_set_advance_hook_for_test(unchanged_frame_advance);
    gchar *output = capture_output(next_frame_capture, player);

    g_assert_cmpstr(output, ==, "");
    g_assert_true(player->kitty_animation_prepared);
    g_assert_false(player->kitty_animation_disabled);
    g_assert_cmpuint(player->kitty_animation_frame_count, ==, 1);
    g_assert_cmpuint(player->kitty_animation_unchanged_ticks, ==, 1);

    for (guint i = 0; i < 8; i++) {
        if (player->timer_id != 0) {
            g_source_remove(player->timer_id);
            player->timer_id = 0;
        }
        g_free(capture_output(next_frame_capture, player));
    }
    gif_player_set_advance_hook_for_test(NULL);
    g_assert_false(player->kitty_animation_prepared);
    g_assert_true(player->kitty_animation_disabled);

    g_free(output);
    g_free(capture_output(stop_capture, player));
    gif_player_destroy(player);
    pixelterm_env_reset_for_test();
#endif
}

static void test_gif_player_public_accessors(void) {
    GifPlayer *player = gif_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    if (!player) {
        g_test_skip("gif player unavailable");
        return;
    }

    if (!player->renderer) {
        g_test_skip("gif player renderer unavailable");
        gif_player_destroy(player);
        return;
    }

    g_assert_false(gif_player_is_loaded_file(player, "image.gif"));
    gif_player_set_color_enhance(player, COLOR_ENHANCE_VIVID);
    g_assert_cmpint(player->renderer->config.color_enhance, ==, COLOR_ENHANCE_VIVID);
    player->filepath = g_strdup("image.gif");
    g_assert_true(gif_player_is_loaded_file(player, "image.gif"));
    g_assert_false(gif_player_is_loaded_file(player, "other.gif"));
    g_assert_false(gif_player_is_loaded_file(NULL, "image.gif"));
    g_assert_false(gif_player_is_loaded_file(player, NULL));

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

static void test_gif_player_stop_resets_iterator_for_replay(void) {
    GifPlayer *player = gif_player_new(4, TRUE, FALSE, FALSE, FALSE,
                                      TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);

    GdkPixbuf *first = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1, 1);
    GdkPixbuf *second = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1, 1);
    gdk_pixbuf_fill(first, 0xff0000ff);
    gdk_pixbuf_fill(second, 0x0000ffff);
    GdkPixbufSimpleAnim *animation = gdk_pixbuf_simple_anim_new(1, 1, 10.0);
    gdk_pixbuf_simple_anim_set_loop(animation, TRUE);
    gdk_pixbuf_simple_anim_add_frame(animation, first);
    gdk_pixbuf_simple_anim_add_frame(animation, second);
    g_object_unref(first);
    g_object_unref(second);

    player->animation = GDK_PIXBUF_ANIMATION(animation);
    player->iter = gdk_pixbuf_animation_get_iter(player->animation, NULL);
    player->is_animated = TRUE;
    gdk_pixbuf_animation_iter_advance(player->iter, NULL);
    g_assert_true(gdk_pixbuf_animation_iter_get_pixbuf(player->iter) != NULL);

    g_assert_cmpint(gif_player_stop(player), ==, ERROR_NONE);
    g_assert_null(player->iter);
    g_assert_cmpint(gif_player_play(player), ==, ERROR_NONE);
    g_assert_nonnull(player->iter);

    gif_player_stop(player);
    gif_player_destroy(player);
}

static void test_gif_player_prepare_for_redraw_preserves_iterator(void) {
    GifPlayer *player = gif_player_new(4, TRUE, FALSE, FALSE, FALSE,
                                      TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);
    GdkPixbuf *first = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1, 1);
    GdkPixbufSimpleAnim *animation = gdk_pixbuf_simple_anim_new(1, 1, 10.0);
    gdk_pixbuf_simple_anim_set_loop(animation, TRUE);
    gdk_pixbuf_simple_anim_add_frame(animation, first);
    g_object_unref(first);
    player->animation = GDK_PIXBUF_ANIMATION(animation);
    player->iter = gdk_pixbuf_animation_get_iter(player->animation, NULL);
    player->is_animated = TRUE;
    player->is_playing = TRUE;
    GdkPixbufAnimationIter *iter = player->iter;

    gif_player_prepare_for_redraw(player);

    g_assert_false(player->is_playing);
    g_assert_true(player->iter == iter);
    g_assert_true(player->resume_from_current_frame);

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
    g_test_add_func("/gif_player/native_animation/pause_and_stop", test_gif_player_native_animation_pause_and_stop);
    g_test_add_func("/gif_player/native_animation/count_gif_frames", test_gif_player_counts_gif_image_blocks);
    g_test_add_func("/gif_player/native_animation/count_webp_frames", test_gif_player_counts_webp_animation_frames);
    g_test_add_func("/gif_player/native_animation/starts_with_root_frame", test_gif_player_native_animation_starts_with_root_frame);
    g_test_add_func("/gif_player/native_animation/rejects_geometry_changes", test_gif_player_native_animation_rejects_geometry_changes);
    g_test_add_func("/gif_player/native_animation/hands_complete_loop_to_kitty", test_gif_player_native_animation_hands_complete_loop_to_kitty);
    g_test_add_func("/gif_player/native_animation/waits_when_frame_is_unchanged", test_gif_player_native_animation_waits_when_frame_is_unchanged);
    g_test_add_func("/gif_player/public_api/accessors", test_gif_player_public_accessors);
    g_test_add_func("/gif_player/play_without_load", test_gif_player_play_without_load);
    g_test_add_func("/gif_player/pause_stop_without_play", test_gif_player_pause_stop_without_play);
    g_test_add_func("/gif_player/stop_resets_iterator_for_replay", test_gif_player_stop_resets_iterator_for_replay);
    g_test_add_func("/gif_player/prepare_for_redraw_preserves_iterator", test_gif_player_prepare_for_redraw_preserves_iterator);
    g_test_add_func("/gif_player/load_invalid_path", test_gif_player_load_invalid_path);
    g_test_add_func("/gif_player/present_text_frame/skips_identical_lines",
                    test_gif_player_present_text_frame_skips_identical_lines);
    g_test_add_func("/gif_player/present_text_frame/repaints_after_layout_change",
                    test_gif_player_layout_change_repaints_cached_text_frame);
}
