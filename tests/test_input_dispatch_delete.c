#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <glib.h>
#include <errno.h>

#include <string.h>

#include "app.h"
#include "input_dispatch_test_support.h"
#include "input_dispatch_delete_internal.h"
#include "input_dispatch_core.h"
#include "terminal_output.h"
#include "test_output_stream.h"

static InputEvent make_key_event(KeyCode key_code) {
    InputEvent event = {0};
    event.type = INPUT_KEY_PRESS;
    event.key_code = key_code;
    return event;
}

static void test_delete_request_arms_in_supported_single_mode(void) {
    PixelTermApp app = {0};
    InputEvent event = make_key_event((KeyCode)'r');

    input_dispatch_test_reset_stubs();
    app.mode = APP_MODE_SINGLE;

    g_assert_true(input_dispatch_handle_delete_request(&app, &event));
    g_assert_true(app.delete_pending);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 0);
}

static void test_delete_request_ignored_in_unsupported_modes(void) {
    InputEvent event = make_key_event((KeyCode)'r');
    AppMode blocked_modes[] = {APP_MODE_FILE_MANAGER, APP_MODE_BOOK, APP_MODE_BOOK_PREVIEW};

    for (guint i = 0; i < G_N_ELEMENTS(blocked_modes); i++) {
        PixelTermApp app = {0};
        input_dispatch_test_reset_stubs();
        app.mode = blocked_modes[i];

        g_assert_false(input_dispatch_handle_delete_request(&app, &event));
        g_assert_false(app.delete_pending);
        g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 0);
    }
}

static void test_delete_request_cancels_pending_on_other_key(void) {
    PixelTermApp app = {0};
    InputEvent arm = make_key_event((KeyCode)'r');
    InputEvent cancel = make_key_event(KEY_ESCAPE);

    input_dispatch_test_reset_stubs();
    app.mode = APP_MODE_SINGLE;

    g_assert_true(input_dispatch_handle_delete_request(&app, &arm));
    g_assert_true(app.delete_pending);

    g_assert_false(input_dispatch_handle_delete_request(&app, &cancel));
    g_assert_false(app.delete_pending);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 0);
}

static void test_delete_request_executes_single_mode_delete_on_second_r(void) {
    PixelTermApp app = {0};
    InputEvent event = make_key_event((KeyCode)'r');

    input_dispatch_test_reset_stubs();
    app.mode = APP_MODE_SINGLE;
    app.total_images = 3;
    app.current_index = 1;

    g_assert_true(input_dispatch_handle_delete_request(&app, &event));
    g_assert_true(input_dispatch_handle_delete_request(&app, &event));

    g_assert_false(app.delete_pending);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.render_by_mode_calls, ==, 1);
}

static void test_delete_request_executes_preview_delete_on_second_r(void) {
    PixelTermApp app = {0};
    InputEvent event = make_key_event((KeyCode)'r');

    input_dispatch_test_reset_stubs();
    app.mode = APP_MODE_PREVIEW;
    app.total_images = 4;
    app.current_index = 0;
    app.preview.selected = 2;

    g_assert_true(input_dispatch_handle_delete_request(&app, &event));
    g_assert_true(input_dispatch_handle_delete_request(&app, &event));

    g_assert_false(app.delete_pending);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 1);
    g_assert_cmpint(app.current_index, ==, 2);
    g_assert_cmpint(app.preview.selected, ==, 2);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_render_calls, ==, 1);
}

static void test_delete_request_deletes_last_preview_image_and_enters_file_manager(void) {
    PixelTermApp app = {0};
    InputEvent event = make_key_event((KeyCode)'r');

    input_dispatch_test_reset_stubs();
    app.mode = APP_MODE_PREVIEW;
    app.total_images = 1;
    app.current_index = 0;
    app.preview.selected = 0;

    g_assert_true(input_dispatch_handle_delete_request(&app, &event));
    g_assert_true(input_dispatch_handle_delete_request(&app, &event));

    g_assert_false(app.delete_pending);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 1);
    g_assert_false(app_has_images(&app));
    g_assert_cmpint(app.total_images, ==, 0);
    g_assert_cmpint(g_input_dispatch_stub_state.enter_file_manager_calls, ==, 1);
    g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 1);
}

static void test_delete_request_clears_pending_when_mode_becomes_unsupported(void) {
    PixelTermApp app = {0};
    InputEvent arm = make_key_event((KeyCode)'r');
    InputEvent followup = make_key_event(KEY_ESCAPE);

    input_dispatch_test_reset_stubs();
    app.mode = APP_MODE_SINGLE;

    g_assert_true(input_dispatch_handle_delete_request(&app, &arm));
    g_assert_true(app.delete_pending);

    app.mode = APP_MODE_FILE_MANAGER;
    g_assert_false(input_dispatch_handle_delete_request(&app, &followup));
    g_assert_false(app.delete_pending);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 0);
}

static void test_delete_output_errors(gconstpointer data) {
    gint path = GPOINTER_TO_INT(data);
    PixelTermApp app = {.mode = path < 2 ? APP_MODE_PREVIEW : APP_MODE_SINGLE,
                        .total_images = path == 0 || path == 2 ? 2 : 1,
                        .delete_pending = TRUE};
    InputEvent event = make_key_event((KeyCode)'r');
    input_dispatch_test_reset_stubs();
    if (path == 0) g_input_dispatch_stub_state.preview_render_result = ERROR_OUTPUT_IO;
    if (path == 1 || path == 3) g_input_dispatch_stub_state.enter_file_manager_result = ERROR_OUTPUT_IO;
    if (path == 2) g_input_dispatch_stub_state.render_by_mode_result = ERROR_OUTPUT_IO;
    if (path == 4) g_input_dispatch_stub_state.file_manager_render_result = ERROR_OUTPUT_IO;
    if (path == 5) g_input_dispatch_stub_state.delete_result = ERROR_OUTPUT_IO;
    g_assert_true(input_dispatch_handle_delete_request(&app, &event));
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 0);
    if (path == 1 || path == 3 || path == 5)
        g_assert_cmpint(g_input_dispatch_stub_state.file_manager_render_calls, ==, 0);
    if (path == 5) g_assert_cmpint(g_input_dispatch_stub_state.render_by_mode_calls, ==, 0);
}

static void test_delete_validation_error_remains_nonfatal(gconstpointer data) {
    gboolean preview = GPOINTER_TO_INT(data);
    PixelTermApp app = {.mode = preview ? APP_MODE_PREVIEW : APP_MODE_SINGLE,
                        .total_images = 2, .delete_pending = TRUE};
    InputEvent event = make_key_event((KeyCode)'r');
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.delete_result = ERROR_INVALID_IMAGE;
    g_assert_true(input_dispatch_handle_delete_request(&app, &event));
    g_assert_cmpint(app.last_error, ==, ERROR_NONE);
    g_assert_cmpint(g_input_dispatch_stub_state.preview_render_calls, ==, preview);
    g_assert_cmpint(g_input_dispatch_stub_state.render_by_mode_calls, ==, !preview);
}

static void test_delete_entry_validation_falls_back(void) {
    PixelTermApp app = {.mode = APP_MODE_SINGLE, .total_images = 1, .delete_pending = TRUE};
    InputEvent event = make_key_event((KeyCode)'r');
    input_dispatch_test_reset_stubs();
    g_input_dispatch_stub_state.enter_file_manager_result = ERROR_INVALID_IMAGE;
    g_assert_true(input_dispatch_handle_delete_request(&app, &event));
    g_assert_cmpint(app.last_error, ==, ERROR_NONE);
    g_assert_cmpint(g_input_dispatch_stub_state.refresh_display_calls, ==, 1);
}

#if defined(__GLIBC__) || defined(__APPLE__)
typedef struct {
    GString *bytes;
    gboolean fail;
} PromptSink;

static ssize_t prompt_sink_write(void *data, const char *bytes, size_t length) {
    PromptSink *sink = data;
    if (sink->fail) { errno = EIO; return -1; }
    g_string_append_len(sink->bytes, bytes, length);
    return (ssize_t)length;
}

static void test_delete_prompt_failed_output(gconstpointer data) {
    gboolean buffered = GPOINTER_TO_INT(data);
    PixelTermApp app = {.mode = APP_MODE_SINGLE, .total_images = 2};
    InputEvent event = make_key_event((KeyCode)'r');
    PromptSink sink = {.bytes = g_string_new(NULL), .fail = TRUE};
    FILE *stream = test_output_stream(&sink, prompt_sink_write);
    g_assert_nonnull(stream);
    char buffer[256];
    g_assert_cmpint(setvbuf(stream, buffer, buffered ? _IOFBF : _IONBF, sizeof(buffer)), ==, 0);
    input_dispatch_test_reset_stubs();
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    gboolean handled = input_dispatch_handle_delete_request(&app, &event);
    gboolean armed_after_failure = app.delete_pending;
    input_dispatch_handle_delete_request(&app, &event);
    gboolean armed_after_retry = app.delete_pending;
    sink.fail = FALSE;
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    g_assert_true(handled);
    g_assert_false(armed_after_failure);
    g_assert_false(armed_after_retry);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 0);
    g_assert_cmpint(app.total_images, ==, 2);
    g_assert_true(recovered);
    g_assert_cmpstr(sink.bytes->str, ==, "\033\\");
    g_string_free(sink.bytes, TRUE);
}

static void test_delete_prompt_cancel_failure_stops_dispatch(gconstpointer data) {
    AppMode mode = GPOINTER_TO_INT(data);
    PixelTermApp app = {.mode = mode, .delete_pending = TRUE, .total_images = 2, .running = TRUE};
    InputEvent event = make_key_event(KEY_ESCAPE);
    InputHandler input = {0};
    PromptSink sink = {.bytes = g_string_new(NULL), .fail = TRUE};
    FILE *stream = test_output_stream(&sink, prompt_sink_write);
    g_assert_nonnull(stream);
    input_dispatch_test_reset_stubs();
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    input_dispatch_core_handle_event(&app, &input, &event);
    sink.fail = FALSE;
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    g_assert_false(app.delete_pending);
    g_assert_true(app.running);
    g_assert_false(input.should_exit);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(g_input_dispatch_stub_state.delete_calls, ==, 0);
    g_assert_true(recovered);
    g_assert_cmpstr(sink.bytes->str, ==, "\033\\");
    g_string_free(sink.bytes, TRUE);
}
#endif

void register_input_dispatch_delete_tests(void) {
#if defined(__GLIBC__) || defined(__APPLE__)
    g_test_add_data_func("/input_dispatch_delete/prompt_failure/buffered", GINT_TO_POINTER(TRUE), test_delete_prompt_failed_output);
    g_test_add_data_func("/input_dispatch_delete/prompt_failure/unbuffered", GINT_TO_POINTER(FALSE), test_delete_prompt_failed_output);
    g_test_add_data_func("/input_dispatch_delete/cancel_failure/single", GINT_TO_POINTER(APP_MODE_SINGLE), test_delete_prompt_cancel_failure_stops_dispatch);
    g_test_add_data_func("/input_dispatch_delete/cancel_failure/unsupported", GINT_TO_POINTER(APP_MODE_FILE_MANAGER), test_delete_prompt_cancel_failure_stops_dispatch);
#endif
    g_test_add_data_func("/input_dispatch_delete/errors/preview_render", GINT_TO_POINTER(0), test_delete_output_errors);
    g_test_add_data_func("/input_dispatch_delete/errors/preview_entry", GINT_TO_POINTER(1), test_delete_output_errors);
    g_test_add_data_func("/input_dispatch_delete/errors/single_render", GINT_TO_POINTER(2), test_delete_output_errors);
    g_test_add_data_func("/input_dispatch_delete/errors/single_entry", GINT_TO_POINTER(3), test_delete_output_errors);
    g_test_add_data_func("/input_dispatch_delete/errors/file_manager_render", GINT_TO_POINTER(4), test_delete_output_errors);
    g_test_add_data_func("/input_dispatch_delete/errors/delete", GINT_TO_POINTER(5), test_delete_output_errors);
    g_test_add_data_func("/input_dispatch_delete/errors/preview_validation", GINT_TO_POINTER(TRUE), test_delete_validation_error_remains_nonfatal);
    g_test_add_data_func("/input_dispatch_delete/errors/single_validation", GINT_TO_POINTER(FALSE), test_delete_validation_error_remains_nonfatal);
    g_test_add_func("/input_dispatch_delete/errors/entry_validation_falls_back", test_delete_entry_validation_falls_back);
    g_test_add_func("/input_dispatch_delete/request/arms_in_supported_single_mode",
                    test_delete_request_arms_in_supported_single_mode);
    g_test_add_func("/input_dispatch_delete/request/ignored_in_unsupported_modes",
                    test_delete_request_ignored_in_unsupported_modes);
    g_test_add_func("/input_dispatch_delete/request/cancels_pending_on_other_key",
                    test_delete_request_cancels_pending_on_other_key);
    g_test_add_func("/input_dispatch_delete/request/executes_single_mode_delete_on_second_r",
                    test_delete_request_executes_single_mode_delete_on_second_r);
    g_test_add_func("/input_dispatch_delete/request/executes_preview_delete_on_second_r",
                    test_delete_request_executes_preview_delete_on_second_r);
    g_test_add_func("/input_dispatch_delete/request/deletes_last_preview_image_and_enters_file_manager",
                    test_delete_request_deletes_last_preview_image_and_enters_file_manager);
    g_test_add_func("/input_dispatch_delete/request/clears_pending_when_mode_becomes_unsupported",
                    test_delete_request_clears_pending_when_mode_becomes_unsupported);
}
