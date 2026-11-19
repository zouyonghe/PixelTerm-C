#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "terminal_probe.h"
#include "terminal_output.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static guint stdout_get_flags_calls;

static int track_probe_fcntl(int fd, int operation, int flags) {
    if (fd == STDOUT_FILENO && operation == F_GETFL) stdout_get_flags_calls++;
    return fcntl(fd, operation, flags);
}

static void test_default_probe_blocked_terminal(gconstpointer data) {
    (void)data;
    if (g_test_subprocess()) {
        int master = posix_openpt(O_RDWR | O_NOCTTY);
        g_assert_cmpint(master, >=, 0);
        g_assert_cmpint(grantpt(master), ==, 0);
        g_assert_cmpint(unlockpt(master), ==, 0);
        int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
        g_assert_cmpint(slave, >=, 0);
        /* Linux can drain a filled PTY asynchronously; stop output before filling. */
        g_assert_cmpint(tcflow(slave, TCOOFF), ==, 0);
        int flags = fcntl(slave, F_GETFL);
        g_assert_cmpint(flags, >=, 0);
        g_assert_cmpint(fcntl(slave, F_SETFL, flags | O_NONBLOCK), ==, 0);
        char bytes[4096] = {0};
        while (write(slave, bytes, sizeof(bytes)) > 0) {}
        while (write(slave, bytes, 1) > 0) {}
        g_assert_true(errno == EAGAIN || errno == EWOULDBLOCK);
        g_assert_cmpint(fcntl(slave, F_SETFL, flags), ==, 0);
        g_assert_cmpint(dup2(slave, STDIN_FILENO), ==, STDIN_FILENO);
        g_assert_cmpint(dup2(slave, STDOUT_FILENO), ==, STDOUT_FILENO);
        struct termios before, after;
        g_assert_cmpint(tcgetattr(slave, &before), ==, 0);
        flags = fcntl(slave, F_GETFL);
        /* Probe transport owns FD 1 even when stdio points at a capture stream. */
        FILE *saved = stdout;
        FILE *capture = tmpfile();
        g_assert_nonnull(capture);
        stdout = capture;
        char reply[64];
        gboolean written = TRUE;
        stdout_get_flags_calls = 0;
        terminal_output_set_fcntl_for_test(track_probe_fcntl);
        gint64 start = g_get_monotonic_time();
        gssize result = terminal_probe_query_response_tracked("\033[c", 120, 20, 'c',
                                                             reply, sizeof(reply), &written);
        gint64 elapsed = g_get_monotonic_time() - start;
        terminal_output_set_fcntl_for_test(NULL);
        g_assert_cmpuint(stdout_get_flags_calls, >, 0);
        g_assert_cmpint(result, ==, -1);
        g_assert_false(written);
        g_assert_cmpint(elapsed, >=, 100000);
        g_assert_cmpint(elapsed, <, G_TIME_SPAN_SECOND);
        g_assert_cmpint(fcntl(slave, F_GETFL), ==, flags);
        g_assert_cmpint(tcgetattr(slave, &after), ==, 0);
        g_assert_cmpuint(after.c_iflag, ==, before.c_iflag);
        g_assert_cmpuint(after.c_oflag, ==, before.c_oflag);
        g_assert_cmpuint(after.c_cflag, ==, before.c_cflag);
        /* Darwin sets PENDIN when canonical input is restored. */
        g_assert_cmpuint(after.c_lflag & ~PENDIN, ==, before.c_lflag & ~PENDIN);
        g_assert_cmpmem(after.c_cc, sizeof(after.c_cc), before.c_cc, sizeof(before.c_cc));
        g_assert_cmpint(ftell(capture), ==, 0);
        stdout = saved;
        fclose(capture);
        g_assert_cmpint(tcflow(slave, TCOON), ==, 0);
        close(slave);
        close(master);
        return;
    }
    g_test_trap_subprocess(NULL, 2 * G_TIME_SPAN_SECOND, 0);
    g_test_trap_assert_passed();
}

static void test_probe_descriptor_full_pipe(void) {
    if (g_test_subprocess()) {
        int fds[2];
        g_assert_cmpint(pipe(fds), ==, 0);
        int flags = fcntl(fds[1], F_GETFL);
        g_assert_cmpint(flags, >=, 0);
        g_assert_cmpint(fcntl(fds[1], F_SETFL, flags | O_NONBLOCK), ==, 0);
        char bytes[4096] = {0};
        while (write(fds[1], bytes, sizeof(bytes)) > 0) {}
        while (write(fds[1], bytes, 1) > 0) {}
        g_assert_true(errno == EAGAIN || errno == EWOULDBLOCK);
        g_assert_cmpint(fcntl(fds[1], F_SETFL, flags), ==, 0);
        flags = fcntl(fds[1], F_GETFL);
        gboolean attempted = FALSE;
        gint64 start = g_get_monotonic_time();
        g_assert_cmpint(terminal_output_descriptor_write(fds[1], "\033[c", 3, &attempted), ==, -1);
        g_assert_true(errno == EAGAIN || errno == EWOULDBLOCK);
        g_assert_true(attempted);
        g_assert_cmpint(g_get_monotonic_time() - start, <, G_TIME_SPAN_SECOND);
        g_assert_cmpint(fcntl(fds[1], F_GETFL), ==, flags);
        g_assert_cmpint(terminal_output_descriptor_write(fds[1], NULL, 0, NULL), ==, 0);
        close(fds[1]);
        close(fds[0]);
        g_assert_cmpint(terminal_output_descriptor_write(-1, "x", 1, NULL), ==, -1);
        g_assert_cmpint(errno, ==, EBADF);
        return;
    }
    g_test_trap_subprocess(NULL, 2 * G_TIME_SPAN_SECOND, 0);
    g_test_trap_assert_passed();
}

void register_terminal_probe_tests(void) {
    g_test_add_func("/terminal_probe/descriptor/full_pipe", test_probe_descriptor_full_pipe);
    g_test_add_data_func("/terminal_probe/default/full_terminal", NULL,
                         test_default_probe_blocked_terminal);
    g_test_add_data_func("/terminal_probe/default/stopped_terminal", GINT_TO_POINTER(1),
                         test_default_probe_blocked_terminal);
}
