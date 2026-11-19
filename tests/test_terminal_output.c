#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <glib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "terminal_output.h"
#include "input.h"
#include "terminal_probe.h"
#include "ui_render_utils.h"
#include "gif_player.h"
#include "test_output_stream.h"

#if defined(__GLIBC__) || defined(__APPLE__)
typedef struct {
    GString *bytes;
    gboolean fail;
    gboolean fail_once;
    guint writes;
} OutputSink;

static ssize_t sink_write(void *data, const char *bytes, size_t length) {
    OutputSink *sink = data;
    sink->writes++;
    if (sink->fail || sink->fail_once) { sink->fail_once = FALSE; errno = EIO; return -1; }
    g_string_append_len(sink->bytes, bytes, length);
    return (ssize_t)length;
}

static void test_pending_policy(gconstpointer data) {
    TerminalOutputPolicy policy = GPOINTER_TO_INT(data);
    OutputSink sink = {.bytes = g_string_new(NULL), .fail = TRUE};
    FILE *stream = test_output_stream(&sink, sink_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    terminal_output_defer("CONTROL", policy);
    gint64 before_failure = g_get_monotonic_time();
    gboolean failed = terminal_output_ready(before_failure);
    gboolean waiting = terminal_output_ready(before_failure + 99999);
    guint before = sink.writes;
    sink.fail = FALSE;
    gboolean recovered = terminal_output_ready(g_get_monotonic_time() + 100000);
    fclose(stream);
    stdout = saved;
    g_assert_false(failed);
    g_assert_false(waiting);
    g_assert_cmpuint(before, ==, 1);
    g_assert_true(recovered);
    g_assert_cmpstr(sink.bytes->str, ==, policy == TERMINAL_OUTPUT_ONCE ? "\033\x5c" : "\033\x5c" "CONTROL");
    g_string_free(sink.bytes, TRUE);
}

static void test_checked_write_and_owned_controls(void) {
    OutputSink sink = {.bytes = g_string_new(NULL), .fail = TRUE};
    FILE *stream = test_output_stream(&sink, sink_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    gboolean first = terminal_output_write("FRAME", 5);
    for (guint i = 0; i < 32; i++) {
        terminal_output_defer("\033[?2026l", TERMINAL_OUTPUT_RESTORE);
    }
    sink.fail = FALSE;
    gboolean recovered = terminal_output_flush();
    gboolean second = terminal_output_write("NEXT", 4);
    fclose(stream);
    stdout = saved;
    g_assert_false(first);
    g_assert_true(recovered);
    g_assert_true(second);
    g_assert_cmpstr(sink.bytes->str, ==, "\033\x5c\033\x5c\033[?2026lNEXT");
    g_string_free(sink.bytes, TRUE);
}

static void test_sync_end_preserves_frame_failure(void) {
    OutputSink sink = {.bytes = g_string_new(NULL), .fail_once = TRUE};
    FILE *stream = test_output_stream(&sink, sink_write);
    g_assert_nonnull(stream);
    char buffer[256];
    g_assert_cmpint(setvbuf(stream, buffer, _IOFBF, sizeof(buffer)), ==, 0);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    gsize written = fwrite("FRAME", 1, 5, stream);
    ui_end_sync_update();
    gboolean submitted = terminal_output_flush();
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    g_assert_cmpuint(written, ==, 5);
    g_assert_false(submitted);
    g_assert_true(recovered);
    g_assert_null(strstr(sink.bytes->str, "FRAME"));
    g_assert_nonnull(strstr(sink.bytes->str, "\033[?2026l"));
    g_string_free(sink.bytes, TRUE);
}

static void test_direct_flush_sets_retry_deadline(void) {
    OutputSink sink = {.bytes = g_string_new(NULL), .fail = TRUE};
    FILE *stream = test_output_stream(&sink, sink_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    terminal_output_defer("CONTROL", TERMINAL_OUTPUT_RESTORE);
    gboolean flushed = terminal_output_flush();
    guint attempts = sink.writes;
    gboolean ready = terminal_output_ready(g_get_monotonic_time());
    guint after = sink.writes;
    sink.fail = FALSE;
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    g_assert_false(flushed);
    g_assert_false(ready);
    g_assert_cmpuint(after, ==, attempts);
    g_assert_true(recovered);
    g_string_free(sink.bytes, TRUE);
}

static void test_legacy_stdout_failure_blocks_ready(gconstpointer data) {
    gboolean buffered = GPOINTER_TO_INT(data);
    OutputSink sink = {.bytes = g_string_new(NULL), .fail = TRUE};
    FILE *stream = test_output_stream(&sink, sink_write);
    g_assert_nonnull(stream);
    char buffer[256];
    g_assert_cmpint(setvbuf(stream, buffer, buffered ? _IOFBF : _IONBF, sizeof(buffer)), ==, 0);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    int written = fputs("LEGACY", stdout);
    if (!buffered) fflush(stdout);
    gboolean ready = terminal_output_ready(g_get_monotonic_time());
    guint attempts = sink.writes;
    gboolean waiting = terminal_output_ready(g_get_monotonic_time());
    guint waiting_attempts = sink.writes;
    sink.fail = FALSE;
    gboolean recovered = terminal_output_ready(g_get_monotonic_time() + 100000);
    fclose(stream);
    stdout = saved;
    (void)written;
    g_assert_false(ready);
    g_assert_false(waiting);
    g_assert_cmpuint(waiting_attempts, ==, attempts);
    g_assert_true(recovered);
    g_assert_cmpstr(sink.bytes->str, ==, "\033\\");
    g_string_free(sink.bytes, TRUE);
}

static void test_ui_controls_report_failed_submission(gconstpointer data) {
    gboolean clear = GPOINTER_TO_INT(data);
    OutputSink sink = {.bytes = g_string_new(NULL), .fail = TRUE};
    FILE *stream = test_output_stream(&sink, sink_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    PixelTermApp app = {.force_kitty = TRUE};
    gboolean submitted = clear ? ui_clear_kitty_images(&app) : ui_begin_sync_update();
    if (submitted) fputs("UNSAFE", stdout);
    sink.fail = FALSE;
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    g_assert_false(submitted);
    g_assert_true(recovered);
    g_assert_null(strstr(sink.bytes->str, "UNSAFE"));
    if (clear) g_assert_null(strstr(sink.bytes->str, "a=d"));
    else g_assert_nonnull(strstr(sink.bytes->str, "\033[?2026l"));
    g_string_free(sink.bytes, TRUE);
}

static int restore_mode(gint action, const struct termios *state, gpointer data) {
    (void)action; (void)state; (void)data;
    return 0;
}

static void test_cleanup_controls_outlive_handler(void) {
    OutputSink sink = {.bytes = g_string_new(NULL), .fail = TRUE};
    FILE *stream = test_output_stream(&sink, sink_write);
    g_assert_nonnull(stream);
    TerminalProbeTransportHooks transport = {.tcsetattr_fn = restore_mode};
    terminal_probe_set_transport_hooks_for_test(&transport, NULL);
    InputHandler *handler = input_handler_create();
    handler->raw_mode_enabled = handler->has_orig_termios = TRUE;
    handler->alt_screen_enabled = handler->mouse_enabled = TRUE;
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    terminal_output_defer_recovery();
    ErrorCode mouse = input_disable_mouse(handler);
    ErrorCode mode = input_disable_raw_mode(handler);
    gboolean termios_done = !handler->raw_mode_enabled;
    input_handler_destroy(handler);
    ui_end_sync_update();
    sink.fail = FALSE;
    gboolean flushed = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    terminal_probe_reset_transport_hooks_for_test();
    g_assert_cmpint(mouse, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(mode, ==, ERROR_OUTPUT_IO);
    g_assert_true(termios_done);
    g_assert_true(flushed);
    g_assert_true(g_str_has_prefix(sink.bytes->str, "\033\x5c"));
    g_assert_nonnull(strstr(sink.bytes->str, "\033[?1002;1006l"));
    g_assert_nonnull(strstr(sink.bytes->str, "\033[?1049l"));
    g_assert_nonnull(strstr(sink.bytes->str, "\033[?25h"));
    g_assert_nonnull(strstr(sink.bytes->str, "\033[?2026l"));
    g_string_free(sink.bytes, TRUE);
}

static void test_unsent_gif_delete_outlives_player(void) {
    GifPlayer *player = gif_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);
    player->kitty_animation_id = 42;
    player->kitty_animation_prepared = TRUE;
    OutputSink sink = {.bytes = g_string_new(NULL), .fail = TRUE};
    FILE *stream = test_output_stream(&sink, sink_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    terminal_output_defer_recovery();
    gboolean ready = terminal_output_ready(g_get_monotonic_time());
    gif_player_destroy(player);
    sink.fail = FALSE;
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    g_assert_false(ready);
    g_assert_true(recovered);
    g_assert_cmpstr(sink.bytes->str, ==, "\033\x5c\033\x5c\033_Ga=d,d=I,i=42,q=2\033\x5c");
    g_string_free(sink.bytes, TRUE);
}
#endif

void register_terminal_output_tests(void) {
#if defined(__GLIBC__) || defined(__APPLE__)
    g_test_add_data_func("/terminal_output/policy/destructive_once", GINT_TO_POINTER(TERMINAL_OUTPUT_ONCE), test_pending_policy);
    g_test_add_data_func("/terminal_output/policy/idempotent_restore", GINT_TO_POINTER(TERMINAL_OUTPUT_RESTORE), test_pending_policy);
    g_test_add_func("/terminal_output/checked_write_and_owned_controls", test_checked_write_and_owned_controls);
    g_test_add_func("/terminal_output/sync_end_preserves_frame_failure", test_sync_end_preserves_frame_failure);
    g_test_add_func("/terminal_output/direct_flush_sets_retry_deadline", test_direct_flush_sets_retry_deadline);
    g_test_add_data_func("/terminal_output/legacy/stream_error", NULL, test_legacy_stdout_failure_blocks_ready);
    g_test_add_data_func("/terminal_output/legacy/buffered_failure", GINT_TO_POINTER(1), test_legacy_stdout_failure_blocks_ready);
    g_test_add_data_func("/terminal_output/ui/begin_failure", NULL, test_ui_controls_report_failed_submission);
    g_test_add_data_func("/terminal_output/ui/clear_failure", GINT_TO_POINTER(1), test_ui_controls_report_failed_submission);
    g_test_add_func("/terminal_output/session/controls_outlive_handler", test_cleanup_controls_outlive_handler);
    g_test_add_func("/terminal_output/session/unsent_gif_delete_outlives_player", test_unsent_gif_delete_outlives_player);
#endif
}
