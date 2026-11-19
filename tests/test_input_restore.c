#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <glib.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "input.h"
#include "terminal_probe.h"

static int restore_pty(gint actions, const struct termios *state, gpointer data) {
    gboolean *fail = data;
    if (*fail) return -1;
    return tcsetattr(STDIN_FILENO, actions, state);
}

static void test_restored_outer_mode_reenters_raw(void) {
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    g_assert_cmpint(master, >=, 0);
    g_assert_cmpint(grantpt(master), ==, 0);
    g_assert_cmpint(unlockpt(master), ==, 0);
    int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
    g_assert_cmpint(slave, >=, 0);
    struct termios cooked, raw, after;
    g_assert_cmpint(tcgetattr(slave, &cooked), ==, 0);
    cooked.c_lflag |= ICANON | ECHO;
    raw = cooked;
    raw.c_lflag &= ~(ICANON | ECHO);
    g_assert_cmpint(tcsetattr(slave, TCSANOW, &raw), ==, 0);
    int saved_input = dup(STDIN_FILENO), saved_output = dup(STDOUT_FILENO);
    FILE *capture = tmpfile();
    g_assert_cmpint(saved_input, >=, 0);
    g_assert_cmpint(saved_output, >=, 0);
    g_assert_nonnull(capture);
    fflush(stdout);
    g_assert_cmpint(dup2(slave, STDIN_FILENO), >=, 0);
    g_assert_cmpint(dup2(fileno(capture), STDOUT_FILENO), >=, 0);
    gboolean fail = TRUE;
    TerminalProbeTransportHooks hooks = {.tcsetattr_fn = restore_pty};
    terminal_probe_set_transport_hooks_for_test(&hooks, &fail);
    gboolean restored = terminal_probe_restore_original(&cooked);
    fail = FALSE;
    InputHandler handler = {.raw_mode_enabled = TRUE, .has_orig_termios = TRUE, .orig_termios = cooked};
    ErrorCode result = input_enable_raw_mode(&handler);
    int read_state = tcgetattr(slave, &after);
    terminal_probe_reset_transport_hooks_for_test();
    int reset_state = tcsetattr(slave, TCSANOW, &cooked);
    fflush(stdout);
    g_assert_cmpint(dup2(saved_input, STDIN_FILENO), >=, 0);
    g_assert_cmpint(dup2(saved_output, STDOUT_FILENO), >=, 0);
    close(saved_input);
    close(saved_output);
    close(slave);
    close(master);
    fclose(capture);
    g_assert_false(restored);
    g_assert_cmpint(result, ==, ERROR_NONE);
    g_assert_cmpint(read_state, ==, 0);
    g_assert_cmpint(reset_state, ==, 0);
    g_assert_true(handler.raw_mode_enabled);
    g_assert_cmpint(after.c_lflag & (ICANON | ECHO), ==, 0);
    g_assert_cmpint(handler.orig_termios.c_lflag & (ICANON | ECHO), ==, ICANON | ECHO);
}

void register_input_restore_tests(void) {
    g_test_add_func("/input/restore/outer_retry_reenters_raw", test_restored_outer_mode_reenters_raw);
}
