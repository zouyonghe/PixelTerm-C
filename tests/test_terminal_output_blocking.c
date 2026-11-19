#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "terminal_output.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static void test_full_pipe_returns(gconstpointer data) {
    if (g_test_subprocess()) {
        int pipefd[2];
        gboolean stopped_terminal = GPOINTER_TO_INT(data) == 2;
        if (stopped_terminal) {
            pipefd[0] = posix_openpt(O_RDWR | O_NOCTTY);
            g_assert_cmpint(pipefd[0], >=, 0);
            g_assert_cmpint(grantpt(pipefd[0]), ==, 0);
            g_assert_cmpint(unlockpt(pipefd[0]), ==, 0);
            pipefd[1] = open(ptsname(pipefd[0]), O_RDWR | O_NOCTTY);
            g_assert_cmpint(pipefd[1], >=, 0);
            g_assert_cmpint(tcflow(pipefd[1], TCOOFF), ==, 0);
        } else {
            g_assert_cmpint(pipe(pipefd), ==, 0);
        }
        int flags = fcntl(pipefd[1], F_GETFL);
        g_assert_cmpint(flags, >=, 0);
        g_assert_cmpint(fcntl(pipefd[1], F_SETFL, flags | O_NONBLOCK), ==, 0);
        char bytes[4096] = {0};
        while (write(pipefd[1], bytes, sizeof(bytes)) > 0) {}
        while (write(pipefd[1], bytes, 1) > 0) {}
        g_assert_true(errno == EAGAIN || errno == EWOULDBLOCK);
        g_assert_cmpint(fcntl(pipefd[1], F_SETFL, flags), ==, 0);
        FILE *stream = fdopen(pipefd[1], "w");
        FILE *recovery = tmpfile();
        g_assert_nonnull(stream);
        g_assert_nonnull(recovery);
        flags = fcntl(pipefd[1], F_GETFL);
        char buffer[256];
        g_assert_cmpint(setvbuf(stream, buffer, _IOFBF, sizeof(buffer)), ==, 0);
        FILE *saved = stdout;
        fflush(saved);
        stdout = stream;
        gboolean submitted;
        if (GPOINTER_TO_INT(data) == 1) {
            g_assert_cmpint(fputs("BUFFERED", stream), >=, 0);
            submitted = terminal_output_flush();
        } else {
            submitted = terminal_output_write("FRAME", 5);
        }
        int after = fcntl(pipefd[1], F_GETFL);
        stdout = recovery;
        g_assert_true(terminal_output_flush());
        stdout = saved;
        fclose(recovery);
        if (stopped_terminal) g_assert_cmpint(tcflow(pipefd[1], TCOON), ==, 0);
        fclose(stream);
        close(pipefd[0]);
        g_assert_false(submitted);
        g_assert_cmpint(after, ==, flags);
        return;
    }
    g_test_trap_subprocess(NULL, 2 * G_TIME_SPAN_SECOND, 0);
    g_test_trap_assert_passed();
}

void register_terminal_output_blocking_tests(void) {
    g_test_add_data_func("/terminal_output/full_pipe/payload", GINT_TO_POINTER(0), test_full_pipe_returns);
    g_test_add_data_func("/terminal_output/full_pipe/buffered", GINT_TO_POINTER(1), test_full_pipe_returns);
    g_test_add_data_func("/terminal_output/stopped_terminal", GINT_TO_POINTER(2), test_full_pipe_returns);
}
