#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "terminal_probe.h"
#include "app.h"
#include "input.h"
#include "kitty_graphics.h"
#include "terminal_output.h"
#include "test_output_stream.h"

static gint startup_failure_stage;
static gboolean resize_test;
static gboolean resize_state_test;
static gboolean resize_expected_pause;
static guint resize_updates;
static gboolean resize_pause_failure;
static ErrorCode lifecycle_gif_pause(GifPlayer *player) {
    if (resize_pause_failure) { player->is_playing = FALSE; return ERROR_OUTPUT_IO; }
    return gif_player_pause(player);
}
static guint resume_calls;
static guint initialize_calls;
static guint raw_mode_calls;
static gboolean dispatch_output_error;
static guint error_dispatches;
static ErrorCode lifecycle_initialize(InputHandler *handler) {
    initialize_calls++;
    return input_handler_initialize(handler);
}
static ErrorCode lifecycle_enable_raw_mode(InputHandler *handler);
static ErrorCode lifecycle_enable_mouse(InputHandler *handler);
static void lifecycle_update_size(InputHandler *handler) {
    if (resize_state_test) {
        handler->terminal_width = ++resize_updates == 1 ? 123 : 124;
        return;
    }
    if (resize_test) handler->terminal_width = 123;
    else input_update_terminal_size(handler);
}
static ErrorCode lifecycle_play(VideoPlayer *player) {
    if (resize_test) { resume_calls++; return ERROR_NONE; }
    return video_player_play(player);
}

static gboolean gate_test;
static guint gate_attempts;
static guint premature_dispatches;
static gint inject_stage;
static gboolean recovery_injected;
static gboolean recovery_blocked;
static gboolean async_completed;
static gboolean persistent_block;
static gboolean terminate_block;
static gint64 blocked_clock;
static gboolean healthy_chunk_progress;
static gboolean blocked_escape;
static gboolean blocked_navigation_first;
static gboolean blocked_unknown_first;
static guint blocked_events;
static gint64 lifecycle_chunk_progress(void) {
    return healthy_chunk_progress ? blocked_clock : 0;
}
static void request_termination_for_test(void);
static gint64 lifecycle_monotonic_time(void) {
    return persistent_block ? blocked_clock : g_get_monotonic_time();
}
static gboolean lifecycle_has_pending(InputHandler *handler) {
    if (blocked_escape && gate_attempts >= 2) return TRUE;
    if (!gate_test) return input_has_pending_input(handler);
    return (inject_stage == 1 && recovery_injected) || (inject_stage == 3 && async_completed);
}
static gboolean lifecycle_cleanup_due(gint64 now_us) {
    if (persistent_block) {
        blocked_clock += 100000;
        if (terminate_block && gate_attempts == 3) request_termination_for_test();
        return ++gate_attempts > 25; /* Bound the unfixed implementation too. */
    }
    if (!gate_test) return terminal_output_ready(now_us);
    if (inject_stage && !recovery_injected) return TRUE;
    if (gate_attempts >= 3) return TRUE;
    gboolean ready = ++gate_attempts >= 3;
    if (ready) recovery_blocked = FALSE;
    return ready;
}

static ErrorCode lifecycle_render(PixelTermApp *app);
static void lifecycle_async_render(PixelTermApp *app) {
    if (gate_test && inject_stage) {
        if (inject_stage == 3 && !async_completed) {
            recovery_injected = recovery_blocked = async_completed = TRUE;
        } else if (inject_stage != 3) {
            if (recovery_blocked) premature_dispatches++;
            app->running = FALSE;
        }
    }
}
static void lifecycle_dispatch(PixelTermApp *app) {
    if (dispatch_output_error) {
        app->last_error = ERROR_OUTPUT_IO;
        if (++error_dispatches > 1) app->running = FALSE;
        return;
    }
    if (gate_test) {
        if (inject_stage) {
            if (inject_stage == 1 && !recovery_injected) recovery_injected = recovery_blocked = TRUE;
            else if (recovery_blocked) premature_dispatches++;
            return;
        }
        if (gate_attempts < 3) premature_dispatches++;
        app->running = FALSE;
    }
}
static void lifecycle_animation(PixelTermApp *app) {
    if (dispatch_output_error && app->last_error != ERROR_NONE) premature_dispatches++;
    if (gate_test && inject_stage == 2 && !recovery_injected) {
        recovery_injected = recovery_blocked = TRUE;
    } else if (gate_test && inject_stage) {
        if (recovery_blocked) premature_dispatches++;
    } else if (!inject_stage) lifecycle_dispatch(app);
}
static void lifecycle_event(PixelTermApp *app, InputHandler *input, const InputEvent *event) {
    (void)input; (void)event;
    if (gate_test && (inject_stage == 1 || inject_stage == 3)) {
        if (recovery_blocked) premature_dispatches++;
        app->running = FALSE;
    }
}
static ErrorCode lifecycle_get_event(InputHandler *handler, InputEvent *event) {
    if (blocked_escape) {
        event->type = INPUT_KEY_PRESS;
        event->key_code = (blocked_navigation_first || blocked_unknown_first) && blocked_events++ == 0 ?
            (blocked_unknown_first ? KEY_UNKNOWN : KEY_RIGHT) : KEY_ESCAPE;
        return ERROR_NONE;
    }
    return gate_test ? ERROR_NONE : input_get_event(handler, event);
}
#define input_dispatch_process_pending lifecycle_dispatch
#define input_dispatch_process_animations lifecycle_animation
#define input_dispatch_pause_video_for_resize lifecycle_dispatch
#define input_dispatch_handle_event lifecycle_event
#define app_render_by_mode lifecycle_render
#define app_process_async_render lifecycle_async_render
#define input_has_pending_input lifecycle_has_pending
#define input_get_event lifecycle_get_event
#define terminal_output_ready lifecycle_cleanup_due
#define terminal_output_chunk_progress_us lifecycle_chunk_progress
#define g_get_monotonic_time lifecycle_monotonic_time
#define input_enable_raw_mode lifecycle_enable_raw_mode
#define input_handler_initialize lifecycle_initialize
#define input_enable_mouse lifecycle_enable_mouse
#define input_update_terminal_size lifecycle_update_size
#define video_player_play lifecycle_play
#define gif_player_pause lifecycle_gif_pause

#define main pixelterm_main_for_lifecycle_test
#include "../src/main.c"
#undef main
#undef app_render_by_mode
#undef app_process_async_render
#undef terminal_output_ready
#undef terminal_output_chunk_progress_us
#undef g_get_monotonic_time
#undef input_enable_raw_mode
#undef input_handler_initialize
#undef input_enable_mouse
#undef input_update_terminal_size
#undef video_player_play
#undef gif_player_pause

static void request_termination_for_test(void) {
    signal_handler(SIGTERM);
}

typedef struct {
    gint failures;
    gint calls;
    struct termios restored;
} CleanupTerminal;

static ErrorCode render_result;
static guint render_calls;
static ErrorCode lifecycle_render(PixelTermApp *app) {
    render_calls++;
    if (resize_state_test && render_calls > 1) {
        g_assert_cmpint(app->preserve_media_pause_on_redraw, ==, resize_expected_pause);
        app->running = FALSE;
        return ERROR_NONE;
    }
    if (resize_test) {
        app->video_was_playing_before_resize = TRUE;
        if (render_calls > 2) app->running = FALSE;
        return render_calls > 1 ? ERROR_OUTPUT_IO : ERROR_NONE;
    }
    return render_result;
}

static void enable_owned_raw_mode(InputHandler *handler, gpointer data) {
    (void)data;
    handler->raw_mode_enabled = TRUE;
    handler->has_orig_termios = TRUE;
    handler->orig_termios.c_lflag = ICANON | ECHO;
    handler->orig_termios.c_cc[VMIN] = 1;
}

static ErrorCode lifecycle_enable_raw_mode(InputHandler *handler) {
    raw_mode_calls++;
    if (startup_failure_stage == 3) return ERROR_TERMINAL_SIZE;
    if (!startup_failure_stage) return input_enable_raw_mode(handler);
    enable_owned_raw_mode(handler, NULL);
    handler->alt_screen_enabled = TRUE;
    const char entry[] = "\033[?1049h\033[?25l";
    return startup_failure_stage == 1 && !terminal_output_write(entry, sizeof(entry) - 1) ?
        ERROR_OUTPUT_IO : ERROR_NONE;
}

static ErrorCode lifecycle_enable_mouse(InputHandler *handler) {
    if (startup_failure_stage != 2) return input_enable_mouse(handler);
    handler->mouse_enabled = TRUE;
    const char entry[] = "\033[?1002;1006h";
    return terminal_output_write(entry, sizeof(entry) - 1) ? ERROR_NONE : ERROR_OUTPUT_IO;
}

static int cleanup_setattr(gint action, const struct termios *state, gpointer data) {
    g_assert_cmpint(action, ==, TCSAFLUSH);
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
    g_assert_true(stdout == capture);
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
    recovery_blocked = inject_stage == 0;
    async_completed = FALSE;
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
    if (persistent_block) {
        rewind(capture);
        gchar output[512] = {0};
        gsize length = fread(output, 1, sizeof(output) - 1, capture);
        g_assert_cmpuint(length, >, 0);
        g_assert_false(ferror(capture));
        g_assert_nonnull(strstr(output, "\033[2J\033[H\033[0m"));
    }
    fclose(capture);
    g_assert_cmpint(dup2(saved_input, STDIN_FILENO), >=, 0);
    close(saved_input);
    fclose(input);
    gate_test = FALSE;
    inject_stage = 0;
    g_assert_cmpint(result, ==, persistent_block && !terminate_block && !healthy_chunk_progress && !blocked_escape ? ERROR_OUTPUT_IO : ERROR_NONE);
    g_assert_cmpuint(premature_dispatches, ==, 0);
    if (blocked_escape) {
        g_assert_cmpuint(gate_attempts, <=, blocked_navigation_first || blocked_unknown_first ? 4 : 3);
    } else if (healthy_chunk_progress) {
        g_assert_cmpuint(gate_attempts, >=, 26);
    } else if (persistent_block) {
        g_assert_cmpuint(gate_attempts, <=, terminate_block ? 4 : 22);
        g_assert_cmpuint(gate_attempts, >=, terminate_block ? 4 : 21);
        g_assert_cmpint(blocked_clock, <=, 2200001);
    } else g_assert_cmpuint(gate_attempts, ==, 3);
}

static void test_main_blocked_output_escape(void) {
    blocked_events = 0;
    persistent_block = blocked_escape = TRUE;
    terminate_block = FALSE;
    blocked_clock = 1;
    test_main_waits_for_framing_recovery(NULL);
    persistent_block = blocked_escape = FALSE;
}

static void test_main_blocked_navigation_then_escape(void) {
    blocked_navigation_first = TRUE;
    test_main_blocked_output_escape();
    blocked_navigation_first = FALSE;
}

static void test_main_blocked_unknown_then_escape(void) {
    blocked_unknown_first = TRUE;
    test_main_blocked_output_escape();
    blocked_unknown_first = FALSE;
}

static void test_main_healthy_chunk_progress(void) {
    persistent_block = healthy_chunk_progress = TRUE;
    terminate_block = FALSE;
    blocked_clock = 1;
    test_main_waits_for_framing_recovery(NULL);
    persistent_block = healthy_chunk_progress = FALSE;
}

static void test_main_persistent_output_block(gconstpointer terminate) {
    persistent_block = TRUE;
    terminate_block = GPOINTER_TO_INT(terminate);
    blocked_clock = 1;
    test_main_waits_for_framing_recovery(NULL);
    persistent_block = terminate_block = FALSE;
    g_terminate_requested = g_last_signal = 0;
}

#if defined(__GLIBC__) || defined(__APPLE__)
typedef struct {
    gboolean writable;
    gboolean unsafe_output;
    GString *output;
    gboolean fail_once;
} LifecycleOutput;

static ssize_t lifecycle_write(void *data, const char *bytes, size_t length) {
    LifecycleOutput *sink = data;
    if (sink->fail_once) { sink->fail_once = FALSE; errno = EIO; return -1; }
    if (length != 2 || memcmp(bytes, "\033\\", 2) != 0) sink->unsafe_output = TRUE;
    if (!sink->writable) { errno = EIO; return -1; }
    g_string_append_len(sink->output, bytes, length);
    return (ssize_t)length;
}

static void test_pending_framing_blocks_startup_and_cleanup(gconstpointer data) {
    gboolean cleanup = GPOINTER_TO_INT(data);
    LifecycleOutput sink = {.output = g_string_new(NULL)};
    FILE *capture = test_output_stream(&sink, lifecycle_write);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    int saved_input = dup(STDIN_FILENO);
    FILE *input = fopen("/dev/null", "r");
    g_assert_cmpint(saved_input, >=, 0);
    g_assert_nonnull(input);
    g_assert_cmpint(dup2(fileno(input), STDIN_FILENO), >=, 0);
    stdout = capture;
    terminal_output_defer_recovery();
    render_calls = 0;
    initialize_calls = raw_mode_calls = 0;
    ErrorCode result;
    if (cleanup) {
        InputHandler *handler = input_handler_create();
        result = run_application_cleanup(handler);
    } else {
        PixelTermApp app = {0};
        result = run_application(&app, FALSE);
    }
    sink.writable = TRUE;
    gboolean recovered = terminal_output_flush();
    fclose(capture);
    stdout = saved;
    g_assert_cmpint(dup2(saved_input, STDIN_FILENO), >=, 0);
    close(saved_input);
    fclose(input);
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_cmpuint(render_calls, ==, 0);
    g_assert_cmpuint(initialize_calls, ==, 0);
    g_assert_cmpuint(raw_mode_calls, ==, 0);
    g_assert_true(recovered);
    g_assert_false(sink.unsafe_output);
    g_assert_cmpstr(sink.output->str, ==, "\033\\");
    g_string_free(sink.output, TRUE);
}

static void test_startup_failure_unwinds_controls(gconstpointer data) {
    startup_failure_stage = GPOINTER_TO_INT(data);
    CleanupTerminal terminal = {0};
    TerminalProbeTransportHooks hooks = {.tcsetattr_fn = cleanup_setattr};
    terminal_probe_set_transport_hooks_for_test(&hooks, &terminal);
    LifecycleOutput sink = {.output = g_string_new(NULL), .writable = TRUE, .fail_once = TRUE};
    FILE *capture = test_output_stream(&sink, lifecycle_write);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    PixelTermApp app = {0};
    ErrorCode result = run_application(&app, TRUE);
    gboolean recovered = terminal_output_flush();
    fclose(capture);
    stdout = saved;
    terminal_probe_reset_transport_hooks_for_test();
    gint stage = startup_failure_stage;
    startup_failure_stage = 0;
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_true(recovered);
    g_assert_cmpint(terminal.calls, >, 0);
    g_assert_cmpint(terminal.restored.c_lflag, ==, ICANON | ECHO);
    g_assert_nonnull(strstr(sink.output->str, "\033[?1049l"));
    g_assert_nonnull(strstr(sink.output->str, "\033[?25h"));
    if (stage == 2) g_assert_nonnull(strstr(sink.output->str, "\033[?1002;1006l"));
    g_string_free(sink.output, TRUE);
}

static void test_cleanup_preserves_clear_failure(void) {
    LifecycleOutput sink = {.output = g_string_new(NULL), .writable = TRUE, .fail_once = TRUE};
    FILE *capture = test_output_stream(&sink, lifecycle_write);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    ErrorCode result = run_application_cleanup(input_handler_create());
    gboolean recovered = terminal_output_flush();
    fclose(capture);
    stdout = saved;
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_true(recovered);
    g_string_free(sink.output, TRUE);
}

static void test_raw_setup_failure_preserves_shell_output(void) {
    LifecycleOutput sink = {.output = g_string_new(NULL), .writable = TRUE};
    FILE *capture = test_output_stream(&sink, lifecycle_write);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    PixelTermApp app = {0};
    startup_failure_stage = 3;
    ErrorCode result = run_application(&app, TRUE);
    startup_failure_stage = 0;
    fclose(capture);
    stdout = saved;
    g_assert_cmpint(result, ==, ERROR_TERMINAL_SIZE);
    g_assert_cmpuint(sink.output->len, ==, 0);
    g_string_free(sink.output, TRUE);
}
#endif

static void test_resize_render_failure_does_not_resume(gconstpointer data) {
    PixelTermApp app = {.running = TRUE};
    resize_pause_failure = GPOINTER_TO_INT(data);
    if (resize_pause_failure) {
        app.gif_player = gif_player_new(1, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
        g_assert_nonnull(app.gif_player);
        app.gif_player->is_playing = TRUE;
    }
    /* The playback call is intercepted: no real player is needed. */
    app.video_player = (VideoPlayer *)&app;
    resize_test = TRUE;
    render_calls = resume_calls = 0;
    g_terminate_requested = 0;
    FILE *saved = stdout, *capture = tmpfile();
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
    resize_test = FALSE;
    gboolean pause_failure = resize_pause_failure;
    resize_pause_failure = FALSE;
    if (app.gif_player) gif_player_destroy(app.gif_player);
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_cmpuint(resume_calls, ==, 0);
    g_assert_cmpuint(render_calls, ==, pause_failure ? 1 : 2);
}

static void test_repeated_resize_preserves_playback_intent(gconstpointer data) {
    gint mode = GPOINTER_TO_INT(data);
    PixelTermApp app = {.running = TRUE, .mode = APP_MODE_SINGLE};
    app.image_files = g_list_append(NULL, g_strdup(mode < 2 ? "clip.mp4" : "anim.gif"));
    app.total_images = 1;
    resize_expected_pause = mode % 2 == 0;
    if (mode < 2) {
        app.video_player = video_player_new(1, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
        app.video_player->is_playing = !resize_expected_pause;
    } else {
        app.gif_player = gif_player_new(1, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
        app.gif_player->is_playing = !resize_expected_pause;
    }
    resize_state_test = TRUE;
    render_result = ERROR_NONE;
    resize_updates = render_calls = 0;
    g_terminate_requested = 0;
    FILE *saved = stdout, *capture = tmpfile();
    g_assert_nonnull(capture);
    int saved_input = dup(STDIN_FILENO);
    FILE *input = fopen("/dev/null", "r");
    g_assert_nonnull(input);
    g_assert_cmpint(dup2(fileno(input), STDIN_FILENO), >=, 0);
    stdout = capture;
    ErrorCode result = run_application(&app, FALSE);
    stdout = saved; fclose(capture);
    g_assert_cmpint(dup2(saved_input, STDIN_FILENO), >=, 0);
    close(saved_input); fclose(input);
    resize_state_test = FALSE;
    g_assert_cmpint(result, ==, ERROR_NONE);
    g_assert_cmpuint(render_calls, ==, 2);
    g_assert_cmpuint(resize_updates, >=, 3);
    g_assert_false(app.preserve_media_pause_on_redraw);
    if (app.video_player) video_player_destroy(app.video_player);
    if (app.gif_player) gif_player_destroy(app.gif_player);
    g_list_free_full(app.image_files, g_free);
}

static void test_already_raw_retries_pending_probe_restore(void) {
    CleanupTerminal terminal = {.failures = 100};
    TerminalProbeTransportHooks hooks = {.tcsetattr_fn = cleanup_setattr};
    terminal_probe_set_transport_hooks_for_test(&hooks, &terminal);
    InputHandler handler = {.raw_mode_enabled = TRUE};
    struct termios original = {0};
    g_assert_false(terminal_probe_restore_original(&original));
    gint before = terminal.calls;
    ErrorCode result = input_enable_raw_mode(&handler);
    gint after = terminal.calls;
    terminal.failures = 0;
    g_assert_true(terminal_probe_retry_restore());
    terminal_probe_reset_transport_hooks_for_test();
    g_assert_cmpint(result, ==, ERROR_TERMINAL_SIZE);
    g_assert_cmpint(after, >, before);
}

static void test_dispatch_output_error_reaches_application(void) {
    PixelTermApp app = {.running = TRUE};
    dispatch_output_error = TRUE;
    error_dispatches = premature_dispatches = 0;
    render_result = ERROR_NONE;
    g_terminate_requested = 0;
    int saved_input = dup(STDIN_FILENO);
    FILE *input = fopen("/dev/null", "r");
    FILE *capture = tmpfile();
    g_assert_cmpint(saved_input, >=, 0);
    g_assert_nonnull(input);
    g_assert_nonnull(capture);
    g_assert_cmpint(dup2(fileno(input), STDIN_FILENO), >=, 0);
    FILE *saved = stdout;
    stdout = capture;
    ErrorCode result = run_application(&app, FALSE);
    stdout = saved;
    fclose(capture);
    g_assert_cmpint(dup2(saved_input, STDIN_FILENO), >=, 0);
    close(saved_input);
    fclose(input);
    dispatch_output_error = FALSE;
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_cmpuint(error_dispatches, ==, 1);
    g_assert_cmpuint(premature_dispatches, ==, 0);
}

static void test_full_pipe_initial_flush_is_bounded(void) {
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 5 * G_TIME_SPAN_SECOND, 0);
        g_test_trap_assert_passed();
        return;
    }
    int descriptors[2];
    g_assert_cmpint(pipe(descriptors), ==, 0);
    int flags = fcntl(descriptors[1], F_GETFL);
    g_assert_cmpint(flags, >=, 0);
    g_assert_cmpint(fcntl(descriptors[1], F_SETFL, flags | O_NONBLOCK), ==, 0);
    char bytes[4096] = {0};
    while (write(descriptors[1], bytes, sizeof(bytes)) > 0) {}
    while (write(descriptors[1], bytes, 1) > 0) {}
    g_assert_cmpint(errno, ==, EAGAIN);
    g_assert_cmpint(fcntl(descriptors[1], F_SETFL, flags), ==, 0);
    FILE *capture = fdopen(descriptors[1], "w");
    g_assert_nonnull(capture);
    char buffer[BUFSIZ];
    g_assert_cmpint(setvbuf(capture, buffer, _IOFBF, sizeof(buffer)), ==, 0);
    g_assert_cmpint(fputs("pending", capture), >=, 0);
    FILE *saved = stdout;
    stdout = capture;
    PixelTermApp app = {0};
    initialize_calls = 0;
    gint64 started = g_get_monotonic_time();
    ErrorCode result = run_application(&app, FALSE);
    gint64 elapsed = g_get_monotonic_time() - started;
    g_assert_true(stdout == capture);
    stdout = saved;
    g_assert_cmpint(fcntl(descriptors[0], F_SETFL, O_NONBLOCK), ==, 0);
    while (read(descriptors[0], bytes, sizeof(bytes)) > 0) {}
    fclose(capture);
    close(descriptors[0]);
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_cmpuint(initialize_calls, ==, 0);
    g_assert_cmpint(elapsed, <, 2 * G_TIME_SPAN_SECOND);
}

static void test_raw_setup_failure_restores_stdout(void) {
    FILE *capture = tmpfile();
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    stdout = capture;
    PixelTermApp app = {0};
    startup_failure_stage = 3;
    ErrorCode result = run_application(&app, TRUE);
    startup_failure_stage = 0;
    g_assert_true(stdout == capture);
    g_assert_cmpint(fputs("restored", stdout), >=, 0);
    g_assert_cmpint(fflush(stdout), ==, 0);
    stdout = saved;
    fclose(capture);
    g_assert_cmpint(result, ==, ERROR_TERMINAL_SIZE);
}

void register_main_lifecycle_tests(void) {
    g_test_add_func("/main/output/unknown_then_escape_exits", test_main_blocked_unknown_then_escape);
    g_test_add_func("/main/output/navigation_then_escape_exits", test_main_blocked_navigation_then_escape);
    g_test_add_func("/main/output/pending_chunk_escape_exits", test_main_blocked_output_escape);
    g_test_add_func("/main/output/healthy_chunk_progress_survives", test_main_healthy_chunk_progress);
    g_test_add_func("/main/startup/raw_setup_failure_restores_stdout", test_raw_setup_failure_restores_stdout);
    g_test_add_func("/main/output/full_pipe_initial_flush_is_bounded", test_full_pipe_initial_flush_is_bounded);
    g_test_add_data_func("/main/output/persistent_block_times_out", NULL, test_main_persistent_output_block);
    g_test_add_data_func("/main/output/persistent_block_signal_exits", GINT_TO_POINTER(1), test_main_persistent_output_block);
    g_test_add_func("/main/startup/already_raw_retries_restore", test_already_raw_retries_pending_probe_restore);
    g_test_add_func("/main/output/dispatch_error_reaches_application", test_dispatch_output_error_reaches_application);
#if defined(__GLIBC__) || defined(__APPLE__)
    g_test_add_func("/main/startup/raw_setup_failure_preserves_shell", test_raw_setup_failure_preserves_shell_output);
    g_test_add_data_func("/main/startup/raw_failure_unwinds", GINT_TO_POINTER(1), test_startup_failure_unwinds_controls);
    g_test_add_data_func("/main/startup/mouse_failure_unwinds", GINT_TO_POINTER(2), test_startup_failure_unwinds_controls);
    g_test_add_func("/main/cleanup/preserves_clear_failure", test_cleanup_preserves_clear_failure);
    g_test_add_data_func("/main/output/blocks_startup", NULL, test_pending_framing_blocks_startup_and_cleanup);
    g_test_add_data_func("/main/output/blocks_cleanup", GINT_TO_POINTER(1), test_pending_framing_blocks_startup_and_cleanup);
#endif
    g_test_add_data_func("/main/resize/paused_video_stays_paused", GINT_TO_POINTER(0), test_repeated_resize_preserves_playback_intent);
    g_test_add_data_func("/main/resize/playing_video_resumes", GINT_TO_POINTER(1), test_repeated_resize_preserves_playback_intent);
    g_test_add_data_func("/main/resize/paused_gif_stays_paused", GINT_TO_POINTER(2), test_repeated_resize_preserves_playback_intent);
    g_test_add_data_func("/main/resize/playing_gif_resumes", GINT_TO_POINTER(3), test_repeated_resize_preserves_playback_intent);
    g_test_add_data_func("/main/resize/render_failure_does_not_resume", NULL, test_resize_render_failure_does_not_resume);
    g_test_add_data_func("/main/resize/gif_pause_failure_aborts", GINT_TO_POINTER(TRUE), test_resize_render_failure_does_not_resume);
    g_test_add_data_func("/main/output/waits_for_framing_recovery", NULL, test_main_waits_for_framing_recovery);
    g_test_add_data_func("/main/output/rechecks_after_dispatch", GINT_TO_POINTER(1), test_main_waits_for_framing_recovery);
    g_test_add_data_func("/main/output/rechecks_after_animation", GINT_TO_POINTER(2), test_main_waits_for_framing_recovery);
    g_test_add_data_func("/main/output/rechecks_after_async_render", GINT_TO_POINTER(3), test_main_waits_for_framing_recovery);
    g_test_add_data_func("/main/cleanup/transient_restore", GINT_TO_POINTER(1), test_cleanup_reports_final_restore_state);
    g_test_add_data_func("/main/cleanup/persistent_restore", GINT_TO_POINTER(10), test_cleanup_reports_final_restore_state);
    g_test_add_data_func("/main/cleanup/late_restore", GINT_TO_POINTER(2), test_cleanup_reports_final_restore_state);
    g_test_add_data_func("/main/cleanup/three_failed_restores", GINT_TO_POINTER(3), test_cleanup_reports_final_restore_state);
    g_test_add_func("/main/cleanup/render_error_diagnostic", test_render_error_keeps_primary_and_reports_cleanup_failure);
}
