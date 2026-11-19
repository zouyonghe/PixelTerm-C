#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <glib.h>
#include <glib/gstdio.h>

#include <unistd.h>

#include "input_dispatch_key_modes_internal.h"
#include "input_dispatch_test_support.h"
#include "terminal_output.h"
#include "test_output_stream.h"
#include <errno.h>

static gint scale_render_calls;
static ErrorCode scale_render_spy(PixelTermApp *app) {
    (void)app;
    scale_render_calls++;
    return ERROR_NONE;
}

/* Compile the dispatch boundary with an entry spy: a failed output flush can
 * otherwise hide an accidental call before renderer hooks are reached. */
#define app_render_current_image scale_render_spy
#define input_dispatch_handle_key_press_single spy_handle_key_press_single
#define input_dispatch_key_modes_toggle_video_playback spy_toggle_video_playback
#define input_dispatch_key_modes_toggle_video_fps spy_toggle_video_fps
#define input_dispatch_key_single_set_video_seek_for_test spy_set_video_seek
#define input_dispatch_key_single_get_video_seek_step_ms_for_test spy_get_video_seek_step
#include "../src/input_dispatch_key_single.c"
#undef app_render_current_image
#undef input_dispatch_handle_key_press_single
#undef input_dispatch_key_modes_toggle_video_playback
#undef input_dispatch_key_modes_toggle_video_fps
#undef input_dispatch_key_single_set_video_seek_for_test
#undef input_dispatch_key_single_get_video_seek_step_ms_for_test

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

static gint redirect_stdin_pipe(void) {
    gint pipe_fds[2] = {-1, -1};
    g_assert_cmpint(pipe(pipe_fds), ==, 0);

    StdinRedirect *redirect = g_new0(StdinRedirect, 1);
    redirect->saved_stdin_fd = dup(STDIN_FILENO);
    g_assert_cmpint(redirect->saved_stdin_fd, >=, 0);
    g_test_queue_destroy(restore_stdin, redirect);

    g_assert_cmpint(dup2(pipe_fds[0], STDIN_FILENO), >=, 0);
    close(pipe_fds[0]);

    return pipe_fds[1];
}

static void redirect_stdin_bytes(const gchar *bytes, gsize len) {
    gint writer = redirect_stdin_pipe();
    g_assert_cmpint(write(writer, bytes, len), ==, (gssize)len);
    close(writer);
}

static void test_input_discards_late_terminal_identity_reply(void) {
    static const char bytes[] = "\033P>|iTerm2 3.5.0\033\\x";
    InputHandler handler = {0};
    InputEvent event = {0};
    redirect_stdin_bytes(bytes, sizeof(bytes) - 1);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, KEY_UNKNOWN);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, 'x');
}

static void test_input_alt_upper_p_keeps_following_key(void) {
    static const char bytes[] = "\033Px";
    InputHandler handler = {0};
    InputEvent event = {0};
    redirect_stdin_bytes(bytes, sizeof(bytes) - 1);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, KEY_UNKNOWN);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, 'x');
}

static void test_input_truncated_reply_expires(gconstpointer user_data) {
    const char *prefix = user_data;
    gint writer = redirect_stdin_pipe();
    InputHandler handler = {0};
    InputEvent event = {0};
    g_assert_cmpint(write(writer, prefix, strlen(prefix)), ==, (gssize)strlen(prefix));
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, KEY_UNKNOWN);
    g_usleep(350000);
    g_assert_cmpint(write(writer, "x", 1), ==, 1);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, 'x');
    close(writer);
}

static void test_input_unmatched_csi_tail_is_replayed(void) {
    gint writer = redirect_stdin_pipe();
    InputHandler handler = {0};
    InputEvent event = {0};
    const char *prefix = "\033[?62;";
    g_assert_cmpint(write(writer, prefix, strlen(prefix)), ==, (gssize)strlen(prefix));
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(write(writer, "x", 1), ==, 1);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, KEY_UNKNOWN);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, 'x');
    close(writer);
}

static void test_input_ambiguous_csi_tail(gconstpointer user_data) {
    gboolean arrow = GPOINTER_TO_INT(user_data);
    gint writer = redirect_stdin_pipe();
    InputHandler handler = {0};
    InputEvent event = {0};
    g_assert_cmpint(write(writer, "\033[", 2), ==, 2);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, KEY_UNKNOWN);
    g_usleep(70000);
    g_assert_cmpint(write(writer, arrow ? "A" : "x", 1), ==, 1);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    if (arrow) {
        g_assert_cmpint(event.key_code, ==, KEY_UP);
    } else {
        g_assert_cmpint(event.key_code, ==, KEY_UNKNOWN);
        g_assert_true(input_has_pending_input(&handler));
        g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
        g_assert_cmpint(event.key_code, ==, 'x');
    }
    close(writer);
}

static void test_input_reply_total_deadline_expires(void) {
    gint writer = redirect_stdin_pipe();
    InputHandler handler = {0};
    InputEvent event = {0};
    const char *prefix = "\033P>|iTerm2";
    g_assert_cmpint(write(writer, prefix, strlen(prefix)), ==, (gssize)strlen(prefix));
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    handler.reply_deadline_us = g_get_monotonic_time() - 1;
    g_assert_cmpint(write(writer, "x", 1), ==, 1);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, 'x');
    close(writer);
}

static void test_input_expired_dcs_prefix_replays_buffered_key(void) {
    gint writer = redirect_stdin_pipe();
    InputHandler handler = {0};
    InputEvent event = {0};
    const char *prefix = "\033P>";
    g_assert_cmpint(write(writer, prefix, strlen(prefix)), ==, (gssize)strlen(prefix));
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_usleep(350000);
    g_assert_true(input_has_pending_input(&handler));
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, '>');
    g_assert_cmpint(write(writer, "x", 1), ==, 1);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, 'x');
    close(writer);
}

typedef struct {
    gint writer;
    const char *reply;
} DelayedReply;

static gpointer write_delayed_reply(gpointer user_data) {
    DelayedReply *reply = user_data;
    g_usleep(80000);
    g_assert_cmpint(write(reply->writer, reply->reply, strlen(reply->reply)), ==,
                    (gssize)strlen(reply->reply));
    return NULL;
}

static void test_input_accepts_delayed_escape_introducer(gconstpointer user_data) {
    gint writer = redirect_stdin_pipe();
    DelayedReply reply = {.writer = writer, .reply = user_data};
    InputHandler handler = {0};
    InputEvent event = {0};
    g_assert_cmpint(write(writer, "\033", 1), ==, 1);
    GThread *thread = g_thread_new("late-reply", write_delayed_reply, &reply);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_thread_join(thread);
    g_assert_cmpint(event.key_code, ==, KEY_UNKNOWN);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, 'x');
    close(writer);
}

static void test_input_discards_fragmented_terminal_reply(gconstpointer user_data) {
    const char *const *chunks = user_data;
    gint writer = redirect_stdin_pipe();
    InputHandler handler = {0};
    InputEvent event = {0};
    for (gsize i = 0; chunks[i]; i++) {
        g_assert_cmpint(write(writer, chunks[i], strlen(chunks[i])), ==, (gssize)strlen(chunks[i]));
        g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
        g_assert_cmpint(event.key_code, ==, KEY_UNKNOWN);
        g_usleep(60000);
    }
    g_assert_cmpint(write(writer, "x", 1), ==, 1);
    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.key_code, ==, 'x');
    close(writer);
}

static void test_input_eof_requests_exit(void) {
    InputHandler handler = {0};
    InputEvent event = {0};
    redirect_stdin_bytes(NULL, 0);

    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_INPUT_EOF);
    g_assert_true(handler.should_exit);
}

static void test_input_persistent_read_error_requests_exit(void) {
    InputHandler handler = {0};
    InputEvent event = {0};
    StdinRedirect *redirect = g_new0(StdinRedirect, 1);
    redirect->saved_stdin_fd = dup(STDIN_FILENO);
    g_assert_cmpint(redirect->saved_stdin_fd, >=, 0);
    g_test_queue_destroy(restore_stdin, redirect);
    close(STDIN_FILENO);

    g_assert_cmpint(input_get_event(&handler, &event), ==, ERROR_INPUT_EOF);
    g_assert_true(handler.should_exit);
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

static void test_video_seek_output_failure_does_not_switch_media(void) {
    VideoPlayer player = {0};
    PixelTermApp app = make_single_app(&player);
    InputHandler handler = {0};
    InputEvent event = make_key_event(KEY_RIGHT);
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    g_input_dispatch_stub_state.video_seek_result = ERROR_OUTPUT_IO;
    input_dispatch_key_single_set_video_seek_for_test(input_dispatch_test_video_seek);
    redirect_stdin_bytes("\033[C", 3);
    input_dispatch_handle_key_press_single(&app, &handler, &event);
    input_dispatch_key_single_set_video_seek_for_test(NULL);
    g_assert_cmpint(g_input_dispatch_stub_state.video_seek_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.next_image_calls, ==, 0);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    InputEvent queued = {0};
    g_assert_cmpint(input_get_event(&handler, &queued), ==, ERROR_NONE);
    g_assert_cmpint(queued.key_code, ==, KEY_RIGHT);
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

static ssize_t failed_protocol_clear(void *data, const char *bytes, size_t length) {
    if (data) (*(gint *)data)++;
    (void)bytes; (void)length;
    errno = EIO;
    return -1;
}

static void test_protocol_clear_failure_preserves_playback(void) {
    VideoPlayer *player = video_player_new(1, TRUE, FALSE, FALSE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    PixelTermApp app = make_single_app(player);
    app.force_kitty = TRUE;
    player->is_playing = TRUE;
    gboolean force_text = player->renderer->config.force_text;
    gboolean force_sixel = player->renderer->config.force_sixel;
    gboolean force_kitty = player->renderer->config.force_kitty;
    gboolean force_iterm2 = player->renderer->config.force_iterm2;
    ChafaPixelMode pixel_mode = chafa_canvas_config_get_pixel_mode(player->renderer->canvas_config);
    InputEvent event = make_key_event((KeyCode)'P');
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    FILE *stream = test_output_stream(NULL, failed_protocol_clear);
    FILE *recovery = tmpfile();
    g_assert_nonnull(stream);
    g_assert_nonnull(recovery);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    g_usleep(160000); /* Protocol keys share a debounce across dispatch calls. */
    input_dispatch_handle_key_press_single(&app, NULL, &event);
    gboolean playing = video_player_is_playing(player);
    g_assert_cmpint(player->renderer->config.force_text, ==, force_text);
    g_assert_cmpint(player->renderer->config.force_sixel, ==, force_sixel);
    g_assert_cmpint(player->renderer->config.force_kitty, ==, force_kitty);
    g_assert_cmpint(player->renderer->config.force_iterm2, ==, force_iterm2);
    g_assert_cmpint(chafa_canvas_config_get_pixel_mode(player->renderer->canvas_config), ==, pixel_mode);
    stdout = recovery;
    gboolean recovered = terminal_output_flush();
    rewind(recovery);
    char recovered_bytes[8] = {0};
    size_t recovered_length = fread(recovered_bytes, 1, sizeof(recovered_bytes), recovery);
    fclose(stream);
    stdout = saved;
    fclose(recovery);
    video_player_destroy(player);
    g_assert_true(playing);
    g_assert_true(recovered);
    g_assert_cmpuint(recovered_length, ==, 2);
    g_assert_cmpmem(recovered_bytes, recovered_length, "\033\\", 2);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
}

static void test_video_scale_render_failure_stays_stopped(void) {
    VideoPlayer *player = video_player_new(1, TRUE, FALSE, FALSE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    PixelTermApp app = make_single_app(player);
    app.total_images = 0;
    app.video_scale = 1.0;
    player->is_playing = TRUE;
    InputEvent event = make_key_event((KeyCode)'-');
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    input_dispatch_handle_key_press_single(&app, NULL, &event);
    g_assert_cmpint(app.last_error, ==, ERROR_NONE);
    g_assert_false(video_player_is_playing(player));
    video_player_destroy(player);
}

static void test_video_scale_stop_failure_aborts_redraw(void) {
    VideoPlayer *player = video_player_new(1, TRUE, FALSE, FALSE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    PixelTermApp app = make_single_app(player);
    app.video_scale = 1.0;
    player->is_playing = TRUE;
    player->kitty_direct_image_id = 31;
    player->kitty_direct_image_present = TRUE;
    InputEvent event = make_key_event((KeyCode)'-');
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    FILE *stream = test_output_stream(NULL, failed_protocol_clear);
    FILE *recovery = tmpfile();
    g_assert_nonnull(stream);
    g_assert_nonnull(recovery);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    scale_render_calls = 0;
    spy_handle_key_press_single(&app, NULL, &event);
    stdout = recovery;
    g_assert_true(terminal_output_flush());
    fclose(stream);
    stdout = saved;
    fclose(recovery);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_false(video_player_is_playing(player));
    g_assert_cmpint(scale_render_calls, ==, 0);
    spy_handle_key_press_single(&app, NULL, &event);
    g_assert_cmpint(scale_render_calls, ==, 1);
    video_player_destroy(player);
}

static void test_video_fps_output_failure_is_recorded(void) {
    VideoPlayer *player = video_player_new(1, TRUE, FALSE, FALSE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    PixelTermApp app = make_single_app(player);
    app.show_fps = TRUE;
    app.term_height = 24;
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    FILE *stream = test_output_stream(NULL, failed_protocol_clear);
    FILE *recovery = tmpfile();
    g_assert_nonnull(stream);
    g_assert_nonnull(recovery);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    input_dispatch_key_modes_toggle_video_fps(&app);
    stdout = recovery;
    g_assert_true(terminal_output_flush());
    fclose(stream);
    stdout = saved;
    rewind(recovery);
    char bytes[8] = {0};
    size_t length = fread(bytes, 1, sizeof(bytes), recovery);
    fclose(recovery);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_cmpmem(bytes, length, "\033\\", 2);
    video_player_destroy(player);
}

static void test_protocol_redraw_failure_stays_stopped(void) {
    VideoPlayer *player = video_player_new(1, TRUE, FALSE, FALSE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    PixelTermApp app = make_single_app(player);
    player->is_playing = TRUE;
    player->render_layout_valid = TRUE;
    player->render_area_top_row = 2;
    player->render_area_height = 1;
    player->render_term_height = 24;
    gboolean force_text = player->renderer->config.force_text;
    gboolean force_sixel = player->renderer->config.force_sixel;
    gboolean force_kitty = player->renderer->config.force_kitty;
    gboolean force_iterm2 = player->renderer->config.force_iterm2;
    InputEvent event = make_key_event((KeyCode)'P');
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    FILE *stream = test_output_stream(NULL, failed_protocol_clear);
    FILE *recovery = tmpfile();
    g_assert_nonnull(stream);
    g_assert_nonnull(recovery);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    g_usleep(160000); /* Protocol keys share a debounce across dispatch calls. */
    input_dispatch_handle_key_press_single(&app, NULL, &event);
    stdout = recovery;
    g_assert_true(terminal_output_flush());
    fclose(stream);
    stdout = saved;
    fclose(recovery);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_false(video_player_is_playing(player));
    g_assert_cmpint(player->renderer->config.force_text, ==, force_text);
    g_assert_cmpint(player->renderer->config.force_sixel, ==, force_sixel);
    g_assert_cmpint(player->renderer->config.force_kitty, ==, force_kitty);
    g_assert_cmpint(player->renderer->config.force_iterm2, ==, force_iterm2);
    video_player_destroy(player);
}

static void test_protocol_resume_failure_is_recorded(void) {
    VideoPlayer *player = video_player_new(1, TRUE, FALSE, FALSE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    PixelTermApp app = make_single_app(player);
    player->is_playing = TRUE;
    player->has_video = FALSE;
    InputEvent event = make_key_event((KeyCode)'P');
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    g_usleep(160000); /* Protocol keys share a debounce across dispatch calls. */
    input_dispatch_handle_key_press_single(&app, NULL, &event);
    g_assert_cmpint(app.last_error, ==, ERROR_INVALID_IMAGE);
    g_assert_false(video_player_is_playing(player));
    video_player_destroy(player);
}

static void test_preview_entry_and_render_errors(gconstpointer data) {
    gboolean entry_failure = GPOINTER_TO_INT(data);
    PixelTermApp app = make_single_app(NULL);
    InputEvent event = make_key_event(KEY_ENTER);
    input_dispatch_test_reset_stubs();
    if (entry_failure) g_input_dispatch_stub_state.enter_preview_result = ERROR_OUTPUT_IO;
    else g_input_dispatch_stub_state.preview_render_result = ERROR_OUTPUT_IO;
    input_dispatch_handle_key_press_single(&app, NULL, &event);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_render_calls, ==, !entry_failure);
}

static void test_navigation_refresh_failure_preserves_queued_input(void) {
    PixelTermApp app = make_single_app(NULL);
    InputHandler handler = {0};
    InputEvent event = make_key_event(KEY_RIGHT);
    InputEvent queued = make_key_event(KEY_RIGHT);
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.refresh_display_result = ERROR_OUTPUT_IO;
    input_unget_event(&handler, &queued);
    input_dispatch_handle_key_press_single(&app, &handler, &event);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 1);
    g_assert_cmpint(input_get_event(&handler, &queued), ==, ERROR_NONE);
    g_assert_cmpint(queued.key_code, ==, KEY_RIGHT);
}

static void test_navigation_bad_image_is_nonfatal(void) {
    PixelTermApp app = make_single_app(NULL);
    InputEvent event = make_key_event(KEY_RIGHT);
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.refresh_display_result = ERROR_INVALID_IMAGE;
    input_dispatch_handle_key_press_single(&app, NULL, &event);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 1);
    g_assert_cmpint(app.last_error, ==, ERROR_NONE);
}

static void test_paused_graphics_protocol_cycle_preserves_frame(void) {
    VideoPlayer *player = video_player_new(1, FALSE, TRUE, FALSE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    PixelTermApp app = make_single_app(player);
    player->render_layout_valid = TRUE;
    player->render_area_top_row = 2;
    player->render_area_height = 1;
    player->render_term_height = 24;
    player->last_frame_top_row = 2;
    player->last_frame_height = 1;
    player->last_frame_lines = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(player->last_frame_lines, g_strdup("paused graphics frame"));
    InputEvent event = make_key_event((KeyCode)'P');
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.current_is_video = TRUE;
    gint writes = 0;
    FILE *stream = test_output_stream(&writes, failed_protocol_clear);
    FILE *recovery = tmpfile();
    g_assert_nonnull(stream);
    g_assert_nonnull(recovery);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    g_usleep(160000);
    input_dispatch_handle_key_press_single(&app, NULL, &event);
    stdout = recovery;
    g_assert_true(terminal_output_flush());
    fclose(stream);
    stdout = saved;
    fclose(recovery);
    g_assert_cmpint(app.last_error, ==, ERROR_NONE);
    g_assert_true(player->renderer->config.force_iterm2);
    g_assert_false(video_player_is_playing(player));
    g_assert_cmpint(writes, ==, 0);
    g_assert_cmpint(player->last_frame_top_row, ==, 2);
    g_assert_cmpint(player->last_frame_height, ==, 1);
    g_assert_cmpuint(player->last_frame_lines->len, ==, 1);
    g_assert_cmpstr(g_ptr_array_index(player->last_frame_lines, 0), ==, "paused graphics frame");
    video_player_destroy(player);
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
    g_assert_cmpint(app.last_error, ==, ERROR_NONE);
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

static void test_input_rejects_mouse_sequence_without_sgr_prefix(void) {
    static const gchar malformed_mouse[] = "\033[0;12;7M";
    InputHandler input_handler = {0};
    InputEvent event = {0};

    redirect_stdin_bytes(malformed_mouse, sizeof(malformed_mouse) - 1);

    g_assert_cmpint(input_get_event(&input_handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.type, ==, INPUT_KEY_PRESS);
    g_assert_cmpint(event.key_code, ==, KEY_UNKNOWN);
}

static void test_input_parses_sgr_mouse_press_with_terminator(void) {
    static const gchar mouse_press[] = "\033[<0;12;7M";
    InputHandler input_handler = {0};
    InputEvent event = {0};

    redirect_stdin_bytes(mouse_press, sizeof(mouse_press) - 1);

    g_assert_cmpint(input_get_event(&input_handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.type, ==, INPUT_MOUSE_PRESS);
    g_assert_cmpint(event.mouse_button, ==, MOUSE_BUTTON_LEFT);
    g_assert_cmpint(event.mouse_x, ==, 12);
    g_assert_cmpint(event.mouse_y, ==, 7);
}

static void test_input_rejects_truncated_sgr_sequence(void) {
    static const gchar truncated_mouse[] = "\033[<0;12";
    InputHandler input_handler = {0};
    InputEvent event = {0};

    redirect_stdin_bytes(truncated_mouse, sizeof(truncated_mouse) - 1);

    g_assert_cmpint(input_get_event(&input_handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.type, ==, INPUT_KEY_PRESS);
    g_assert_cmpint(event.key_code, ==, KEY_UNKNOWN);
}

static void test_input_rejects_oversized_unterminated_escape(void) {
    static const gchar oversized_escape[] =
        "\033[1234567890123456789012345678901234567890";
    InputHandler input_handler = {0};
    InputEvent event = {0};

    redirect_stdin_bytes(oversized_escape, sizeof(oversized_escape) - 1);

    g_assert_cmpint(input_get_event(&input_handler, &event), ==, ERROR_NONE);
    g_assert_cmpint(event.type, ==, INPUT_KEY_PRESS);
    g_assert_cmpint(event.key_code, ==, KEY_UNKNOWN);
}

static void test_input_rejects_negative_sgr_coordinates(void) {
    static const gchar negative_mouse[] = "\033[<0;-1;7M";
    InputHandler input_handler = {0};
    InputEvent event = {0};

    redirect_stdin_bytes(negative_mouse, sizeof(negative_mouse) - 1);

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
    g_test_add_func("/input_dispatch_key_single/navigation/bad_image_is_nonfatal", test_navigation_bad_image_is_nonfatal);
    g_test_add_func("/input_dispatch_key_single/video/paused_graphics_cycle_preserves_frame", test_paused_graphics_protocol_cycle_preserves_frame);
    g_test_add_func("/input_dispatch_key_single/video/fps_output_failure_is_recorded", test_video_fps_output_failure_is_recorded);
    g_test_add_func("/input_dispatch_key_single/video/protocol_redraw_failure_stays_stopped", test_protocol_redraw_failure_stays_stopped);
    g_test_add_func("/input_dispatch_key_single/video/scale_render_failure_stays_stopped", test_video_scale_render_failure_stays_stopped);
    g_test_add_func("/input_dispatch_key_single/video/scale_stop_failure_aborts_redraw", test_video_scale_stop_failure_aborts_redraw);
    g_test_add_func("/input_dispatch_key_single/video/protocol_resume_failure_is_recorded", test_protocol_resume_failure_is_recorded);
    g_test_add_data_func("/input_dispatch_key_single/errors/preview_entry", GINT_TO_POINTER(TRUE), test_preview_entry_and_render_errors);
    g_test_add_data_func("/input_dispatch_key_single/errors/preview_render", GINT_TO_POINTER(FALSE), test_preview_entry_and_render_errors);
    g_test_add_func("/input_dispatch_key_single/errors/navigation_refresh", test_navigation_refresh_failure_preserves_queued_input);
    g_test_add_func("/input_dispatch_key_single/video/protocol_clear_failure_preserves_playback", test_protocol_clear_failure_preserves_playback);
    g_test_add_func("/input/discards_late_terminal_identity_reply",
                    test_input_discards_late_terminal_identity_reply);
    g_test_add_func("/input/alt_upper_p_keeps_following_key",
                    test_input_alt_upper_p_keeps_following_key);
    g_test_add_data_func("/input/truncated_dcs_expires", "\033P>|iTerm2",
                         test_input_truncated_reply_expires);
    g_test_add_data_func("/input/truncated_csi_expires", "\033[?62;",
                         test_input_truncated_reply_expires);
    g_test_add_data_func("/input/truncated_csi_introducer_expires", "\033[",
                         test_input_truncated_reply_expires);
    g_test_add_data_func("/input/truncated_apc_expires", "\033_Gi=31;OK",
                         test_input_truncated_reply_expires);
    g_test_add_data_func("/input/alt_upper_p_idle_expires", "\033P",
                         test_input_truncated_reply_expires);
    g_test_add_func("/input/unmatched_csi_tail_is_replayed", test_input_unmatched_csi_tail_is_replayed);
    g_test_add_data_func("/input/ambiguous_csi_ordinary_tail_is_replayed", GINT_TO_POINTER(FALSE),
                         test_input_ambiguous_csi_tail);
    g_test_add_data_func("/input/ambiguous_csi_arrow_tail_stays_navigation", GINT_TO_POINTER(TRUE),
                         test_input_ambiguous_csi_tail);
    g_test_add_func("/input/reply_total_deadline_expires", test_input_reply_total_deadline_expires);
    g_test_add_func("/input/expired_dcs_prefix_replays_buffered_key",
                    test_input_expired_dcs_prefix_replays_buffered_key);
    g_test_add_data_func("/input/delayed_dcs_introducer", "P>|iTerm2\033\\x",
                         test_input_accepts_delayed_escape_introducer);
    g_test_add_data_func("/input/delayed_apc_introducer", "_Gi=31;OK\033\\x",
                         test_input_accepts_delayed_escape_introducer);
    static const char *const dcs_chunks[] = {"\033P", ">", "|", "iTerm2 3.5", "\033", "\\", NULL};
    static const char *const csi_chunks[] = {"\033[", "?", "62;", "4", "c", NULL};
    g_test_add_data_func("/input/discards_fragmented_dcs_reply", dcs_chunks,
                         test_input_discards_fragmented_terminal_reply);
    g_test_add_data_func("/input/discards_fragmented_csi_reply", csi_chunks,
                         test_input_discards_fragmented_terminal_reply);
    g_test_add_func("/input/eof/requests_exit", test_input_eof_requests_exit);
    g_test_add_func("/input/read_error/requests_exit",
                    test_input_persistent_read_error_requests_exit);
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
    g_test_add_func("/input_dispatch_key_single/video/seek_output_failure_does_not_switch_media",
                    test_video_seek_output_failure_does_not_switch_media);
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
    g_test_add_func("/input/rejects_mouse_sequence_without_sgr_prefix",
                    test_input_rejects_mouse_sequence_without_sgr_prefix);
    g_test_add_func("/input/parses_sgr_mouse_press_with_terminator",
                    test_input_parses_sgr_mouse_press_with_terminator);
    g_test_add_func("/input/rejects_truncated_sgr_sequence",
                    test_input_rejects_truncated_sgr_sequence);
    g_test_add_func("/input/rejects_oversized_unterminated_escape",
                    test_input_rejects_oversized_unterminated_escape);
    g_test_add_func("/input/rejects_negative_sgr_coordinates",
                    test_input_rejects_negative_sgr_coordinates);
}
