#include <glib.h>
#include <glib/gstdio.h>

#include <unistd.h>

#include "input_dispatch_key_modes_internal.h"
#include "input_dispatch_test_support.h"

static PixelTermApp make_single_app(VideoPlayer *player) {
    PixelTermApp app = {0};
    app.mode = APP_MODE_SINGLE;
    app.current_index = 1;
    app.total_images = 3;
    if (player) {
        player->has_video = TRUE;
    }
    app.video_player = player;
    return app;
}

static InputEvent make_key_event(KeyCode key_code) {
    InputEvent event = {0};
    event.type = INPUT_KEY_PRESS;
    event.key_code = key_code;
    return event;
}

typedef void (*StdoutCaptureFunc)(gpointer user_data);

static gchar *capture_output(StdoutCaptureFunc func, gpointer user_data) {
    gchar *template = g_strdup_printf("%s/pixelterm-input-dispatch-XXXXXX", g_get_tmp_dir());
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

typedef struct {
    gint saved_stdin_fd;
} StdinRedirect;

static void restore_stdin(gpointer data) {
    StdinRedirect *redirect = (StdinRedirect *)data;
    if (!redirect) {
        return;
    }

    if (redirect->saved_stdin_fd >= 0) {
        (void)dup2(redirect->saved_stdin_fd, STDIN_FILENO);
        close(redirect->saved_stdin_fd);
    }

    g_free(redirect);
}

static void redirect_stdin_bytes(const gchar *bytes, gsize len) {
    gint pipe_fds[2] = {-1, -1};
    g_assert_cmpint(pipe(pipe_fds), ==, 0);

    StdinRedirect *redirect = g_new0(StdinRedirect, 1);
    redirect->saved_stdin_fd = dup(STDIN_FILENO);
    g_assert_cmpint(redirect->saved_stdin_fd, >=, 0);
    g_test_queue_destroy(restore_stdin, redirect);

    g_assert_cmpint(dup2(pipe_fds[0], STDIN_FILENO), >=, 0);
    close(pipe_fds[0]);

    g_assert_cmpint(write(pipe_fds[1], bytes, len), ==, (gssize)len);
    close(pipe_fds[1]);
}

static void assert_previous_navigation(KeyCode key_code) {
    PixelTermApp app = make_single_app(NULL);
    InputEvent event = make_key_event(key_code);

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = FALSE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &event);

    g_assert_cmpint(app.current_index, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 1);
    g_assert_true(app.suppress_full_clear);
    g_assert_true(app.async.render_request);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 0);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void assert_next_navigation(KeyCode key_code) {
    PixelTermApp app = make_single_app(NULL);
    InputEvent event = make_key_event(key_code);

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = FALSE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &event);

    g_assert_cmpint(app.current_index, ==, 2);
    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 1);
    g_assert_true(app.suppress_full_clear);
    g_assert_true(app.async.render_request);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 0);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_left_matches_h_navigation(void) {
    assert_previous_navigation(KEY_LEFT);
    assert_previous_navigation((KeyCode)'h');
}

static void test_right_matches_l_navigation(void) {
    assert_next_navigation(KEY_RIGHT);
    assert_next_navigation((KeyCode)'l');
}

static void test_video_left_does_not_switch_media(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent event = make_key_event(KEY_LEFT);
    gint64 seek_step_ms = input_dispatch_key_single_get_video_seek_step_ms_for_test();

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &event);

    g_assert_cmpint(app.current_index, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.last_video_seek_delta_ms, ==, -seek_step_ms);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_video_right_does_not_switch_media(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent event = make_key_event(KEY_RIGHT);
    gint64 seek_step_ms = input_dispatch_key_single_get_video_seek_step_ms_for_test();

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &event);

    g_assert_cmpint(app.current_index, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.last_video_seek_delta_ms, ==, seek_step_ms);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_video_right_coalesces_queued_repeats_and_preserves_next_event(void) {
    static const gchar queued_input[] = "\033[C\033[C\033[A";

    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent event = make_key_event(KEY_RIGHT);
    InputHandler input_handler = {0};
    InputEvent next_event = {0};
    gint64 seek_step_ms = input_dispatch_key_single_get_video_seek_step_ms_for_test();

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);
    redirect_stdin_bytes(queued_input, sizeof(queued_input) - 1);

    input_dispatch_handle_key_press_single(&app, &input_handler, &event);

    g_assert_cmpint(app.current_index, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 3);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_total_delta_ms, ==, seek_step_ms * 3);
    g_assert_cmpint(g_input_dispatch_stub_state.last_video_seek_delta_ms, ==, seek_step_ms);
    g_assert_cmpint(input_get_event(&input_handler, &next_event), ==, ERROR_NONE);
    g_assert_cmpint(next_event.type, ==, INPUT_KEY_PRESS);
    g_assert_cmpint(next_event.key_code, ==, KEY_UP);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_video_seek_failure_preserves_queued_repeats_for_retry(void) {
    static const gchar queued_input[] = "\033[C\033[C\033[A";

    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent event = make_key_event(KEY_RIGHT);
    InputHandler input_handler = {0};
    InputEvent next_event = {0};
    gint64 seek_step_ms = input_dispatch_key_single_get_video_seek_step_ms_for_test();

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    g_input_dispatch_stub_state.video_seek_result = ERROR_INVALID_IMAGE;
    g_input_dispatch_stub_state.next_image_result = ERROR_INVALID_IMAGE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);
    redirect_stdin_bytes(queued_input, sizeof(queued_input) - 1);

    input_dispatch_handle_key_press_single(&app, &input_handler, &event);

    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.last_video_seek_delta_ms, ==, seek_step_ms);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 1);
    g_assert_cmpint(input_get_event(&input_handler, &next_event), ==, ERROR_NONE);
    g_assert_cmpint(next_event.type, ==, INPUT_KEY_PRESS);
    g_assert_cmpint(next_event.key_code, ==, KEY_RIGHT);
    g_assert_cmpint(input_get_event(&input_handler, &next_event), ==, ERROR_NONE);
    g_assert_cmpint(next_event.type, ==, INPUT_KEY_PRESS);
    g_assert_cmpint(next_event.key_code, ==, KEY_RIGHT);
    g_assert_cmpint(input_get_event(&input_handler, &next_event), ==, ERROR_NONE);
    g_assert_cmpint(next_event.type, ==, INPUT_KEY_PRESS);
    g_assert_cmpint(next_event.key_code, ==, KEY_UP);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_video_up_and_down_keep_media_switching(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent up_event = make_key_event(KEY_UP);
    InputEvent down_event = make_key_event(KEY_DOWN);

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &up_event);
    input_dispatch_handle_key_press_single(&app, NULL, &down_event);

    g_assert_cmpint(app.current_index, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.previous_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 0);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_video_scale_keeps_paused_video_paused(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputEvent event = make_key_event((KeyCode)'-');

    app.term_width = 120;
    app.term_height = 40;
    app.video_scale = 1.0;
    player.filepath = g_strdup("clip.mp4");
    player.is_playing = FALSE;

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);

    input_dispatch_handle_key_press_single(&app, NULL, &event);

    g_assert_cmpfloat_with_epsilon(app.video_scale, 0.9, 0.0001);
    g_assert_false(player.is_playing);

    g_free(player.filepath);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_navigation_failure_does_not_refresh_or_advance_queue(void) {
    PixelTermApp app = make_single_app(NULL);
    InputEvent event = make_key_event(KEY_RIGHT);
    InputEvent queued = make_key_event(KEY_RIGHT);
    InputHandler input_handler = {0};

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = FALSE;
    g_input_dispatch_stub_state.next_image_result = ERROR_INVALID_IMAGE;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);
    input_unget_event(&input_handler, &queued);

    input_dispatch_handle_key_press_single(&app, &input_handler, &event);

    g_assert_cmpint(app.current_index, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);
    g_assert_false(app.suppress_full_clear);
    g_assert_false(app.async.render_request);
    InputEvent preserved = {0};
    g_assert_cmpint(input_get_event(&input_handler, &preserved), ==, ERROR_NONE);
    g_assert_cmpint(preserved.key_code, ==, KEY_RIGHT);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
}

static void test_input_maps_fullwidth_question_to_help_key(void) {
    static const gchar fullwidth_question[] = "\xEF\xBC\x9F";
    InputHandler input_handler = {0};
    InputEvent event = {0};

    redirect_stdin_bytes(fullwidth_question, sizeof(fullwidth_question) - 1);

    g_assert_cmpint(input_get_event(&input_handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.type, ==, INPUT_KEY_PRESS);
    g_assert_cmpint(event.key_code, ==, (KeyCode)'?');
}

static void test_input_maps_fullwidth_tilde_to_zen_key(void) {
    static const gchar fullwidth_tilde[] = "\xEF\xBD\x9E";
    InputHandler input_handler = {0};
    InputEvent event = {0};

    redirect_stdin_bytes(fullwidth_tilde, sizeof(fullwidth_tilde) - 1);

    g_assert_cmpint(input_get_event(&input_handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.type, ==, INPUT_KEY_PRESS);
    g_assert_cmpint(event.key_code, ==, (KeyCode)'~');
}

static void test_input_rejects_malformed_sgr_mouse_parameters(void) {
    static const gchar malformed_mouse[] = "\033[<9999999999;2;1M";
    InputHandler input_handler = {0};
    InputEvent event = {0};

    redirect_stdin_bytes(malformed_mouse, sizeof(malformed_mouse) - 1);

    g_assert_cmpint(input_get_event(&input_handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.type, ==, INPUT_KEY_PRESS);
    g_assert_cmpint(event.key_code, ==, KEY_UNKNOWN);
}

typedef struct {
    PixelTermApp *app;
} ToggleVideoFpsCall;

static void toggle_video_fps_capture(gpointer user_data) {
    ToggleVideoFpsCall *call = (ToggleVideoFpsCall *)user_data;
    g_assert_nonnull(call);
    input_dispatch_key_modes_toggle_video_fps(call->app);
}

static void test_video_fps_second_toggle_restores_stats_row(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    ToggleVideoFpsCall call = {.app = &app};

    app.term_height = 24;
    app.show_fps = TRUE;
    app.ui_text_hidden = FALSE;
    player.show_stats = TRUE;
    player.last_frame_top_row = 3;
    player.last_frame_height = 1;
    player.last_frame_lines = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(player.last_frame_lines, g_strdup("restored video row"));

    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;

    gchar *output = capture_output(toggle_video_fps_capture, &call);

    g_assert_false(app.show_fps);
    g_assert_false(player.show_stats);
    g_assert_nonnull(g_strstr_len(output, -1, "\033[3;1H\033[2K"));
    g_assert_null(g_strstr_len(output, -1, "\033[4;1H\033[2K"));

    g_free(output);
    g_ptr_array_free(player.last_frame_lines, TRUE);
}

void register_input_dispatch_key_single_tests(void) {
    g_test_add_func("/input_dispatch_key_single/navigation/left_matches_h",
                    test_left_matches_h_navigation);
    g_test_add_func("/input_dispatch_key_single/navigation/right_matches_l",
                    test_right_matches_l_navigation);
    g_test_add_func("/input_dispatch_key_single/video/left_does_not_switch_media",
                    test_video_left_does_not_switch_media);
    g_test_add_func("/input_dispatch_key_single/video/right_does_not_switch_media",
                    test_video_right_does_not_switch_media);
    g_test_add_func("/input_dispatch_key_single/video/right_coalesces_queued_repeats_and_preserves_next_event",
                    test_video_right_coalesces_queued_repeats_and_preserves_next_event);
    g_test_add_func("/input_dispatch_key_single/video/seek_failure_preserves_queued_repeats_for_retry",
                    test_video_seek_failure_preserves_queued_repeats_for_retry);
    g_test_add_func("/input_dispatch_key_single/video/up_and_down_keep_media_switching",
                    test_video_up_and_down_keep_media_switching);
    g_test_add_func("/input_dispatch_key_single/video/scale_keeps_paused_video_paused",
                    test_video_scale_keeps_paused_video_paused);
    g_test_add_func("/input_dispatch_key_single/video/fps_second_toggle_restores_stats_row",
                    test_video_fps_second_toggle_restores_stats_row);
    g_test_add_func("/input_dispatch_key_single/navigation/failure_does_not_refresh_or_advance_queue",
                    test_navigation_failure_does_not_refresh_or_advance_queue);
    g_test_add_func("/input/input_maps_fullwidth_question_to_help_key",
                    test_input_maps_fullwidth_question_to_help_key);
    g_test_add_func("/input/input_maps_fullwidth_tilde_to_zen_key",
                    test_input_maps_fullwidth_tilde_to_zen_key);
    g_test_add_func("/input/rejects_malformed_sgr_mouse_parameters",
                    test_input_rejects_malformed_sgr_mouse_parameters);
}
