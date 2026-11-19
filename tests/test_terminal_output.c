#define _GNU_SOURCE
#include <glib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "terminal_output.h"

#ifdef __GLIBC__
typedef struct {
    GString *bytes;
    gboolean fail;
    guint writes;
} OutputSink;

static ssize_t sink_write(void *data, const char *bytes, size_t length) {
    OutputSink *sink = data;
    sink->writes++;
    if (sink->fail) { errno = EIO; return -1; }
    g_string_append_len(sink->bytes, bytes, length);
    return (ssize_t)length;
}

static void test_pending_policy(gconstpointer data) {
    TerminalOutputPolicy policy = GPOINTER_TO_INT(data);
    OutputSink sink = {.bytes = g_string_new(NULL), .fail = TRUE};
    cookie_io_functions_t hooks = {.write = sink_write};
    FILE *stream = fopencookie(&sink, "w", hooks);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    terminal_output_defer("CONTROL", policy);
    gboolean failed = terminal_output_ready(1000000);
    gboolean waiting = terminal_output_ready(1099999);
    guint before = sink.writes;
    sink.fail = FALSE;
    gboolean recovered = terminal_output_ready(1100000);
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
    cookie_io_functions_t hooks = {.write = sink_write};
    FILE *stream = fopencookie(&sink, "w", hooks);
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
#endif

void register_terminal_output_tests(void) {
#ifdef __GLIBC__
    g_test_add_data_func("/terminal_output/policy/destructive_once", GINT_TO_POINTER(TERMINAL_OUTPUT_ONCE), test_pending_policy);
    g_test_add_data_func("/terminal_output/policy/idempotent_restore", GINT_TO_POINTER(TERMINAL_OUTPUT_RESTORE), test_pending_policy);
    g_test_add_func("/terminal_output/checked_write_and_owned_controls", test_checked_write_and_owned_controls);
#endif
}
