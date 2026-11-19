#define _GNU_SOURCE
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "terminal_probe.h"
#include "app.h"
#include "input.h"
#include "kitty_graphics.h"

static gboolean gate_test;
static guint gate_attempts;
static guint premature_dispatches;
static gint inject_stage;
static gboolean recovery_injected;
static gboolean lifecycle_has_pending(InputHandler *handler) {
    return gate_test ? FALSE : input_has_pending_input(handler);
}
static gboolean lifecycle_cleanup_due(gint64 now_us) {
    if (!gate_test) return kitty_graphics_retry_pending_deletes_if_due(now_us);
    if (inject_stage && !recovery_injected) return TRUE;
    if (gate_attempts >= 3) return TRUE;
    return ++gate_attempts >= 3;
}

static ErrorCode lifecycle_render(PixelTermApp *app);
static void lifecycle_async_render(PixelTermApp *app) {
    if (gate_test && inject_stage) {
        if (gate_attempts < 3) premature_dispatches++;
        app->running = FALSE;
    }
}
static void lifecycle_dispatch(PixelTermApp *app) {
    if (gate_test) {
        if (inject_stage) {
            if (inject_stage == 1) recovery_injected = TRUE;
            return;
        }
        if (gate_attempts < 3) premature_dispatches++;
        app->running = FALSE;
    }
}
static void lifecycle_animation(PixelTermApp *app) {
    if (gate_test && inject_stage == 2) recovery_injected = TRUE;
    else if (!inject_stage) lifecycle_dispatch(app);
}
static void lifecycle_event(PixelTermApp *app, InputHandler *input, const InputEvent *event) { (void)app; (void)input; (void)event; }
#define input_dispatch_process_pending lifecycle_dispatch
#define input_dispatch_process_animations lifecycle_animation
#define input_dispatch_pause_video_for_resize lifecycle_dispatch
#define input_dispatch_handle_event lifecycle_event
#define app_render_by_mode lifecycle_render
#define app_process_async_render lifecycle_async_render
#define input_has_pending_input lifecycle_has_pending
#define kitty_graphics_retry_pending_deletes_if_due lifecycle_cleanup_due

#define main pixelterm_main_for_lifecycle_test
#include "../src/main.c"
#undef main
#undef app_render_by_mode
#undef app_process_async_render
#undef kitty_graphics_retry_pending_deletes_if_due

typedef struct {
    gint failures;
    gint calls;
    struct termios restored;
} CleanupTerminal;

static ErrorCode render_result;
static guint render_calls;
static ErrorCode lifecycle_render(PixelTermApp *app) { (void)app; render_calls++; return render_result; }

static void enable_owned_raw_mode(InputHandler *handler, gpointer data) {
    (void)data;
    handler->raw_mode_enabled = TRUE;
    handler->has_orig_termios = TRUE;
    handler->orig_termios.c_lflag = ICANON | ECHO;
    handler->orig_termios.c_cc[VMIN] = 1;
}

static int cleanup_setattr(gint action, const struct termios *state, gpointer data) {
    (void)action;
    CleanupTerminal *terminal = data;
    terminal->calls++;
    if (terminal->failures-- > 0) return -1;
    terminal->restored = *state;
    return 0;
}

static void test_cleanup_reports_final_restore_state(gconstpointer data) {
    gint failures = GPOINTER_TO_INT(data);
    CleanupTerminal terminal = {.failures = failures};
    TerminalProbeTransportHooks hooks = {.tcsetattr_fn = cleanup_setattr};
    terminal_probe_set_transport_hooks_for_test(&hooks, &terminal);
    InputHandler *handler = input_handler_create();
    handler->raw_mode_enabled = TRUE;
    handler->has_orig_termios = TRUE;
    handler->orig_termios.c_lflag = ICANON | ECHO;
    handler->orig_termios.c_cc[VMIN] = 1;
    FILE *saved = stdout;
    FILE *capture = tmpfile();
    FILE *saved_errors = stderr;
    FILE *errors = tmpfile();
    g_assert_nonnull(capture);
    g_assert_nonnull(errors);
    stdout = capture;
    stderr = errors;
    ErrorCode result = run_application_cleanup(handler);
    stdout = saved;
    stderr = saved_errors;
    fclose(capture);
    rewind(errors);
    gchar diagnostic[256] = {0};
    gsize length = fread(diagnostic, 1, sizeof(diagnostic) - 1, errors);
    g_assert_cmpuint(length, <, sizeof(diagnostic));
    fclose(errors);
    terminal.failures = 0;
    g_assert_true(terminal_probe_retry_restore());
    terminal_probe_reset_transport_hooks_for_test();
    g_assert_cmpint(result, ==, failures >= 3 ? ERROR_TERMINAL_SIZE : ERROR_NONE);
    /* Two outer attempts (including destructor) plus the final pending retry. */
    g_assert_cmpint(terminal.restored.c_lflag, ==, ICANON | ECHO);
    g_assert_cmpstr(diagnostic, ==, "");
}

static void test_render_error_keeps_primary_and_reports_cleanup_failure(void) {
    CleanupTerminal terminal = {.failures = 100};
    TerminalProbeTransportHooks hooks = {.tcsetattr_fn = cleanup_setattr};
    terminal_probe_set_transport_hooks_for_test(&hooks, &terminal);
    input_set_enable_raw_mode_observer_for_test(enable_owned_raw_mode, NULL);
    PixelTermApp app = {0};
    render_result = ERROR_INVALID_IMAGE;
    FILE *saved = stdout, *saved_errors = stderr;
    FILE *capture = tmpfile(), *errors = tmpfile();
    g_assert_nonnull(capture);
    g_assert_nonnull(errors);
    stdout = capture;
    stderr = errors;
    ErrorCode result = run_application(&app, FALSE);
    stdout = saved;
    stderr = saved_errors;
    fclose(capture);
    rewind(errors);
    gchar diagnostic[256] = {0};
    gsize length = fread(diagnostic, 1, sizeof(diagnostic) - 1, errors);
    g_assert_cmpuint(length, <, sizeof(diagnostic));
    fclose(errors);
    input_set_enable_raw_mode_observer_for_test(NULL, NULL);
    terminal.failures = 0;
    g_assert_true(terminal_probe_retry_restore());
    terminal_probe_reset_transport_hooks_for_test();
    g_assert_cmpint(result, ==, ERROR_INVALID_IMAGE);
    g_assert_nonnull(strstr(diagnostic, "Terminal restoration failed"));
}

static void test_main_waits_for_framing_recovery(gconstpointer data) {
    PixelTermApp app = {.running = TRUE};
    render_result = ERROR_NONE;
    gate_test = TRUE;
    inject_stage = GPOINTER_TO_INT(data);
    recovery_injected = FALSE;
    gate_attempts = premature_dispatches = 0;
    g_terminate_requested = 0;
    FILE *saved = stdout;
    FILE *capture = tmpfile();
    g_assert_nonnull(capture);
    int saved_input = dup(STDIN_FILENO);
    FILE *input = fopen("/dev/null", "r");
    g_assert_cmpint(saved_input, >=, 0);
    g_assert_nonnull(input);
    g_assert_cmpint(dup2(fileno(input), STDIN_FILENO), >=, 0);
    stdout = capture;
    ErrorCode result = run_application(&app, FALSE);
    stdout = saved;
    fclose(capture);
    g_assert_cmpint(dup2(saved_input, STDIN_FILENO), >=, 0);
    close(saved_input);
    fclose(input);
    gate_test = FALSE;
    inject_stage = 0;
    g_assert_cmpint(result, ==, ERROR_NONE);
    g_assert_cmpuint(gate_attempts, ==, 3);
    g_assert_cmpuint(premature_dispatches, ==, 0);
}

#ifdef __GLIBC__
typedef struct {
    gboolean writable;
    gboolean unsafe_output;
    GString *output;
} LifecycleOutput;

static ssize_t lifecycle_write(void *data, const char *bytes, size_t length) {
    LifecycleOutput *sink = data;
    if (length != 2 || memcmp(bytes, "\033\\", 2) != 0) sink->unsafe_output = TRUE;
    if (!sink->writable) { errno = EIO; return -1; }
    g_string_append_len(sink->output, bytes, length);
    return (ssize_t)length;
}

static void test_pending_framing_blocks_startup_and_cleanup(gconstpointer data) {
    gboolean cleanup = GPOINTER_TO_INT(data);
    LifecycleOutput sink = {.output = g_string_new(NULL)};
    cookie_io_functions_t hooks = {.write = lifecycle_write};
    FILE *capture = fopencookie(&sink, "w", hooks);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    kitty_graphics_defer_output_recovery();
    render_calls = 0;
    ErrorCode result;
    if (cleanup) {
        InputHandler *handler = input_handler_create();
        result = run_application_cleanup(handler);
    } else {
        PixelTermApp app = {0};
        result = run_application(&app, FALSE);
    }
    sink.writable = TRUE;
    gboolean recovered = kitty_graphics_retry_pending_deletes();
    fclose(capture);
    stdout = saved;
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_cmpuint(render_calls, ==, 0);
    g_assert_true(recovered);
    g_assert_false(sink.unsafe_output);
    g_assert_cmpstr(sink.output->str, ==, "\033\\");
    g_string_free(sink.output, TRUE);
}
#endif

void register_main_lifecycle_tests(void) {
#ifdef __GLIBC__
    g_test_add_data_func("/main/output/blocks_startup", NULL, test_pending_framing_blocks_startup_and_cleanup);
    g_test_add_data_func("/main/output/blocks_cleanup", GINT_TO_POINTER(1), test_pending_framing_blocks_startup_and_cleanup);
#endif
    g_test_add_data_func("/main/output/waits_for_framing_recovery", NULL, test_main_waits_for_framing_recovery);
    g_test_add_data_func("/main/output/rechecks_after_dispatch", GINT_TO_POINTER(1), test_main_waits_for_framing_recovery);
    g_test_add_data_func("/main/output/rechecks_after_animation", GINT_TO_POINTER(2), test_main_waits_for_framing_recovery);
    g_test_add_data_func("/main/cleanup/transient_restore", GINT_TO_POINTER(1), test_cleanup_reports_final_restore_state);
    g_test_add_data_func("/main/cleanup/persistent_restore", GINT_TO_POINTER(10), test_cleanup_reports_final_restore_state);
    g_test_add_data_func("/main/cleanup/late_restore", GINT_TO_POINTER(2), test_cleanup_reports_final_restore_state);
    g_test_add_data_func("/main/cleanup/three_failed_restores", GINT_TO_POINTER(3), test_cleanup_reports_final_restore_state);
    g_test_add_func("/main/cleanup/render_error_diagnostic", test_render_error_keeps_primary_and_reports_cleanup_failure);
}
