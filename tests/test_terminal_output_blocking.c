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
        gboolean session = GPOINTER_TO_INT(data) >= 3;
        if (session) g_assert_true(terminal_output_begin_session());
        if (GPOINTER_TO_INT(data) == 1 || session) {
            g_assert_cmpint(fputs("BUFFERED", stdout), >=, 0);
            submitted = GPOINTER_TO_INT(data) == 4 ? fflush(stdout) == 0 : terminal_output_flush();
        } else {
            submitted = terminal_output_write("FRAME", 5);
        }
        if (GPOINTER_TO_INT(data) == 4) {
            g_assert_cmpint(fcntl(pipefd[0], F_SETFL, O_NONBLOCK), ==, 0);
            while (read(pipefd[0], bytes, sizeof(bytes)) > 0) {}
            fputs("LATER", stdout);
            g_assert_cmpint(fflush(stdout), !=, 0);
            g_assert_cmpint(read(pipefd[0], bytes, sizeof(bytes)), ==, -1);
            g_assert_true(errno == EAGAIN || errno == EWOULDBLOCK);
        }
        if (session) {
            g_assert_false(terminal_output_end_session());
            g_assert_true(stdout == stream);
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

typedef struct { int fd; GByteArray *bytes; } PipeReader;

static gpointer read_delayed_pipe(gpointer data) {
    PipeReader *reader = data;
    g_usleep(20000);
    guchar buffer[4096];
    ssize_t count;
    while ((count = read(reader->fd, buffer, sizeof(buffer))) > 0)
        g_byte_array_append(reader->bytes, buffer, (guint)count);
    return NULL;
}

static void test_transient_pipe_preserves_payload(void) {
    if (g_test_subprocess()) {
        int pipefd[2];
        g_assert_cmpint(pipe(pipefd), ==, 0);
        FILE *stream = fdopen(pipefd[1], "w");
        g_assert_nonnull(stream);
        PipeReader reader = {.fd = pipefd[0], .bytes = g_byte_array_new()};
        GThread *thread = g_thread_new("pipe-reader", read_delayed_pipe, &reader);
        gsize length = 256 * 1024;
        gchar *payload = g_malloc(length);
        for (gsize i = 0; i < length; i++) payload[i] = (gchar)(i % 251);
        FILE *saved = stdout;
        fflush(saved);
        stdout = stream;
        g_assert_true(terminal_output_begin_session());
        gboolean submitted = fwrite(payload, 1, length, stdout) == length && terminal_output_flush();
        g_assert_true(terminal_output_end_session());
        g_assert_true(stdout == stream);
        stdout = saved;
        fclose(stream);
        g_thread_join(thread);
        close(pipefd[0]);
        g_assert_true(submitted);
        g_assert_cmpmem(reader.bytes->data, reader.bytes->len, payload, length);
        g_byte_array_unref(reader.bytes);
        g_free(payload);
        return;
    }
    g_test_trap_subprocess(NULL, 2 * G_TIME_SPAN_SECOND, 0);
    g_test_trap_assert_passed();
}

void register_terminal_output_blocking_tests(void) {
    g_test_add_func("/terminal_output/transient_pipe_preserves_payload", test_transient_pipe_preserves_payload);
    g_test_add_data_func("/terminal_output/full_pipe/payload", GINT_TO_POINTER(0), test_full_pipe_returns);
    g_test_add_data_func("/terminal_output/full_pipe/buffered", GINT_TO_POINTER(1), test_full_pipe_returns);
    g_test_add_data_func("/terminal_output/full_pipe/legacy_session", GINT_TO_POINTER(3), test_full_pipe_returns);
    g_test_add_data_func("/terminal_output/full_pipe/legacy_stops_after_failure", GINT_TO_POINTER(4), test_full_pipe_returns);
    g_test_add_data_func("/terminal_output/stopped_terminal", GINT_TO_POINTER(2), test_full_pipe_returns);
}
