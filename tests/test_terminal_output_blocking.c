#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "terminal_output.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

static guint get_calls, restore_calls, enable_calls, fail_get, fail_restore, fail_enable;
static int failed_get_fd;
static int get_failure_errno;
static int fail_get_fd = -1;

static int failing_fcntl(int fd, int operation, int flags) {
    if ((operation == F_GETFL && (++get_calls == fail_get || fd == fail_get_fd)) ||
        (operation == F_SETFL && (flags & O_NONBLOCK) && ++enable_calls == fail_enable) ||
        (operation == F_SETFL && !(flags & O_NONBLOCK) &&
         ++restore_calls >= fail_restore && fail_restore)) {
        if (operation == F_GETFL) failed_get_fd = fd;
        errno = operation == F_GETFL && get_failure_errno ? get_failure_errno : EIO;
        return -1;
    }
    return fcntl(fd, operation, flags);
}

static void test_once_preflight_retained(gconstpointer data) {
    FILE *stream = tmpfile();
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    guint failure = GPOINTER_TO_UINT(data);
    gboolean session = failure == 1;
    if (session) g_assert_true(terminal_output_begin_session());
    get_calls = restore_calls = enable_calls = fail_restore = 0;
    fail_get = failure == 5 ? 0 : failure;
    fail_enable = failure == 5 ? 2 : 0;
    terminal_output_set_fcntl_for_test(failing_fcntl);
    terminal_output_defer("ONCE", TERMINAL_OUTPUT_ONCE);
    g_assert_false(terminal_output_flush());
    g_assert_cmpint(ftell(stream), ==, 0);
    terminal_output_set_fcntl_for_test(NULL);
    g_assert_true(terminal_output_flush());
    if (session) g_assert_true(terminal_output_end_session());
    rewind(stream);
    char bytes[32] = {0};
    gsize length = fread(bytes, 1, sizeof(bytes), stream);
    stdout = saved;
    fclose(stream);
    g_assert_cmpmem(bytes, length, "\033\\ONCE", 6);
}

static void test_restore_flags_retry(gconstpointer data) {
    gboolean session = GPOINTER_TO_INT(data);
    FILE *stream = tmpfile();
    g_assert_nonnull(stream);
    int fd = fileno(stream);
    /* Darwin adds a written-status flag after the first write. */
    g_assert_cmpint(write(fd, "x", 1), ==, 1);
    g_assert_cmpint(ftruncate(fd, 0), ==, 0);
    rewind(stream);
    int flags = fcntl(fd, F_GETFL);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    if (session) g_assert_true(terminal_output_begin_session());
    get_calls = restore_calls = enable_calls = fail_get = fail_enable = 0;
    fail_restore = session ? 1 : 2;
    terminal_output_set_fcntl_for_test(failing_fcntl);
    if (session) {
        char payload[32768];
        memset(payload, 'P', sizeof(payload));
        g_assert_cmpuint(fwrite(payload, 1, sizeof(payload), stdout), ==, sizeof(payload));
        g_assert_false(terminal_output_flush());
        g_assert_cmpint(lseek(fd, 0, SEEK_CUR), ==, sizeof(payload));
        fputs("LATER", stdout);
        g_assert_cmpint(fflush(stdout), !=, 0);
        g_assert_cmpint(lseek(fd, 0, SEEK_CUR), ==, sizeof(payload));
    } else {
        terminal_output_defer("ONCE", TERMINAL_OUTPUT_ONCE);
        g_assert_false(terminal_output_flush());
        g_assert_cmpint(lseek(fd, 0, SEEK_CUR), ==, 6);
    }
    g_assert_cmpint(fcntl(fd, F_GETFL), ==, flags | O_NONBLOCK);
    g_assert_false(terminal_output_flush());
    g_assert_cmpint(fcntl(fd, F_GETFL), ==, flags | O_NONBLOCK);
    terminal_output_set_fcntl_for_test(NULL);
    g_assert_true(terminal_output_flush());
    g_assert_cmpint(fcntl(fd, F_GETFL), ==, flags);
    if (session) g_assert_true(terminal_output_end_session());
    else {
        rewind(stream);
        char bytes[32] = {0};
        gsize length = fread(bytes, 1, sizeof(bytes), stream);
        g_assert_cmpmem(bytes, length, "\033\\ONCE\033\\", 8);
    }
    stdout = saved;
    fclose(stream);
}

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
            g_assert_cmpint(fcntl(pipefd[0], F_SETFL, O_NONBLOCK), ==, 0);
            while (read(pipefd[0], bytes, sizeof(bytes)) > 0) {}
        }
        int after = fcntl(pipefd[1], F_GETFL);
        stdout = recovery;
        g_assert_true(terminal_output_flush());
        if (session) {
            g_assert_cmpint(ftell(recovery), ==, 0);
            g_assert_cmpint(read(pipefd[0], bytes, sizeof(bytes)), ==, 2);
            g_assert_cmpint(bytes[0], ==, 27);
            g_assert_cmpint(bytes[1], ==, 92);
        }
        stdout = saved;
        fclose(recovery);
        if (stopped_terminal) g_assert_cmpint(tcflow(pipefd[1], TCOON), ==, 0);
        fclose(stream);
        close(pipefd[0]);
        g_assert_false(submitted);
        g_assert_cmpint(after, ==, flags);
        return;
    }
    g_test_trap_subprocess(NULL, 20 * G_TIME_SPAN_SECOND, 0);
    g_test_trap_assert_passed();
}

typedef struct { int fd; GByteArray *bytes; guint delay_us; } PipeReader;

static gpointer read_delayed_pipe(gpointer data) {
    PipeReader *reader = data;
    g_usleep(reader->delay_us);
    guchar buffer[4096];
    ssize_t count;
    gsize next_pause = 64 * 1024;
    while ((count = read(reader->fd, buffer, sizeof(buffer))) > 0) {
        g_byte_array_append(reader->bytes, buffer, (guint)count);
        if (reader->bytes->len >= next_pause) {
            g_usleep(reader->delay_us);
            next_pause = reader->bytes->len + 64 * 1024;
        }
    }
    return NULL;
}

static void test_transient_pipe_preserves_payload(gconstpointer data) {
    if (g_test_subprocess()) {
        guint mode = GPOINTER_TO_UINT(data);
        gboolean session = mode != 1;
        int pipefd[2];
        g_assert_cmpint(pipe(pipefd), ==, 0);
        /* Darwin adds a written-status flag on the first write. */
        char warmup;
        g_assert_cmpint(write(pipefd[1], "x", 1), ==, 1);
        g_assert_cmpint(read(pipefd[0], &warmup, 1), ==, 1);
        int flags = fcntl(pipefd[1], F_GETFL);
        g_assert_cmpint(flags, >=, 0);
        FILE *stream = fdopen(pipefd[1], "w");
        g_assert_nonnull(stream);
        PipeReader reader = {.fd = pipefd[0], .bytes = g_byte_array_new(),
                             .delay_us = mode ? 250000 : 20000};
        GThread *thread = g_thread_new("pipe-reader", read_delayed_pipe, &reader);
        gsize length = (mode ? 512 : 256) * 1024;
        gchar *payload = g_malloc(length);
        for (gsize i = 0; i < length; i++) payload[i] = (gchar)(i % 251);
        FILE *saved = stdout;
        fflush(saved);
        stdout = stream;
        if (session) g_assert_true(terminal_output_begin_session());
        gboolean submitted = session
            ? fwrite(payload, 1, length, stdout) == length && terminal_output_flush()
            : terminal_output_write(payload, length);
        if (session) g_assert_true(terminal_output_end_session());
        g_assert_true(stdout == stream);
        g_assert_cmpint(fcntl(pipefd[1], F_GETFL), ==, flags);
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
    g_test_trap_subprocess(NULL, 5 * G_TIME_SPAN_SECOND, 0);
    g_test_trap_assert_passed();
}

static void test_partial_session_preserves_progress(void) {
    if (g_test_subprocess()) {
        int pipefd[2];
        g_assert_cmpint(pipe(pipefd), ==, 0);
        g_assert_cmpint(fcntl(pipefd[0], F_SETFL, O_NONBLOCK), ==, 0);
        int flags = fcntl(pipefd[1], F_GETFL);
        g_assert_cmpint(flags, >=, 0);
        g_assert_cmpint(fcntl(pipefd[1], F_SETFL, flags | O_NONBLOCK), ==, 0);
        guchar bytes[4096] = {0};
        gsize filled = 0;
        ssize_t count;
        while ((count = write(pipefd[1], bytes, sizeof(bytes))) > 0) filled += (gsize)count;
        while ((count = write(pipefd[1], bytes, 1)) > 0) filled += (gsize)count;
        g_assert_true(errno == EAGAIN || errno == EWOULDBLOCK);
        gsize drained = 0;
        while (drained < sizeof(bytes)) {
            count = read(pipefd[0], bytes, sizeof(bytes) - drained);
            if (count < 0 && errno == EINTR) continue;
            g_assert_cmpint(count, >, 0); /* EOF or another error invalidates setup. */
            drained += (gsize)count;
        }
        gsize prefix = filled - sizeof(bytes);
        g_assert_cmpint(fcntl(pipefd[1], F_SETFL, flags), ==, 0);
        FILE *stream = fdopen(pipefd[1], "w");
        g_assert_nonnull(stream);
        FILE *saved = stdout;
        fflush(saved);
        stdout = stream;
        g_assert_true(terminal_output_begin_session());
        g_assert_cmpint(setvbuf(stdout, NULL, _IONBF, 0), ==, 0);
        gsize length = filled + 256 * 1024;
        gchar *payload = g_malloc(length);
        for (gsize i = 0; i < length; i++) payload[i] = (gchar)(i % 251);
        gsize accepted = fwrite(payload, 1, length, stdout);
        g_assert_cmpuint(accepted, >, 0);
        g_assert_cmpuint(accepted, <, length);
        GByteArray *received = g_byte_array_new();
        while ((count = read(pipefd[0], bytes, sizeof(bytes))) > 0)
            g_byte_array_append(received, bytes, (guint)count);
        g_assert_cmpuint(received->len, ==, prefix + accepted);
        g_assert_cmpmem(received->data + prefix, received->len - prefix, payload, accepted);
        fputs("LATER", stdout);
        fflush(stdout);
        g_assert_true(ferror(stdout) != 0);
        g_assert_cmpint(read(pipefd[0], bytes, sizeof(bytes)), ==, -1);
        g_assert_true(errno == EAGAIN || errno == EWOULDBLOCK);
        g_assert_false(terminal_output_flush());
        g_assert_true(terminal_output_flush());
        g_assert_true(terminal_output_end_session());
        g_assert_true(stdout == stream);
        g_assert_cmpint(read(pipefd[0], bytes, sizeof(bytes)), ==, 2);
        g_assert_cmpmem(bytes, 2, "\033\\", 2);
        stdout = saved;
        fclose(stream);
        close(pipefd[0]);
        g_byte_array_unref(received);
        g_free(payload);
        return;
    }
    g_test_trap_subprocess(NULL, 5 * G_TIME_SPAN_SECOND, 0);
    g_test_trap_assert_passed();
}

static void test_graphics_chunk_resumes_without_blocking(gconstpointer data) {
    int pipefd[2];
    g_assert_cmpint(pipe(pipefd), ==, 0);
    /* Darwin records the first write in F_GETFL; snapshot after warming the pipe. */
    gchar warmup;
    g_assert_cmpint(write(pipefd[1], "x", 1), ==, 1);
    g_assert_cmpint(read(pipefd[0], &warmup, 1), ==, 1);
    int flags = fcntl(pipefd[1], F_GETFL);
    g_assert_cmpint(fcntl(pipefd[1], F_SETFL, flags | O_NONBLOCK), ==, 0);
    g_assert_cmpint(fcntl(pipefd[0], F_SETFL, O_NONBLOCK), ==, 0);
    gchar fill[4096]; memset(fill, 'F', sizeof(fill));
    gsize prefix = 0; ssize_t count;
    while ((count = write(pipefd[1], fill, sizeof(fill))) > 0) prefix += count;
    g_assert_cmpint(fcntl(pipefd[1], F_SETFL, flags), ==, 0);
    FILE *saved = stdout;
    FILE *stream = fdopen(pipefd[1], "w"); stdout = stream;
    gboolean session = GPOINTER_TO_INT(data);
    if (session) g_assert_true(terminal_output_begin_session());
    const gchar *chunk = "\033_Ga=f,m=0;AQIDBA==\033\\";
    g_assert_true(terminal_output_write_chunk(chunk, strlen(chunk)));
    gint64 start = g_get_monotonic_time();
    g_assert_false(terminal_output_flush());
    g_assert_cmpint(g_get_monotonic_time() - start, <, 100000);
    /* Cancellation cleanup must follow the retained partial packet, never overtake it. */
    terminal_output_defer("DELETE", TERMINAL_OUTPUT_ONCE);
    while (prefix) {
        count = read(pipefd[0], fill, MIN(prefix, sizeof(fill)));
        g_assert_cmpint(count, >, 0); prefix -= count;
    }
    g_assert_true(terminal_output_flush());
    if (session) g_assert_true(terminal_output_end_session());
    count = read(pipefd[0], fill, sizeof(fill));
    gchar *expected = g_strconcat(chunk, "\033\\DELETE", NULL);
    g_assert_cmpmem(fill, count, expected, strlen(expected));
    g_assert_cmpint(fcntl(fileno(stream), F_GETFL), ==, flags);
    g_free(expected); stdout = saved; fclose(stream); close(pipefd[0]);
}

static void test_graphics_chunk_partial_progress_then_cleanup(void) {
#ifdef __linux__
    int fds[2];
    g_assert_cmpint(pipe(fds), ==, 0);
    int flags = fcntl(fds[1], F_GETFL);
    g_assert_cmpint(fcntl(fds[1], F_SETFL, flags | O_NONBLOCK), ==, 0);
    g_assert_cmpint(fcntl(fds[0], F_SETFL, O_NONBLOCK), ==, 0);
    gchar bytes[4096]; memset(bytes, 'F', sizeof(bytes));
    gsize fill = 0; ssize_t n;
    while ((n = write(fds[1], bytes, sizeof(bytes))) > 0) fill += n;
    g_assert_cmpint(read(fds[0], bytes, sizeof(bytes)), ==, sizeof(bytes));
    fill -= sizeof(bytes);
    g_assert_cmpint(fcntl(fds[1], F_SETFL, flags), ==, 0);
    FILE *saved = stdout, *stream = fdopen(fds[1], "w"); stdout = stream;
    gchar payload[8192];
    for (guint i = 0; i < sizeof(payload); i++) payload[i] = (gchar)(i % 127);
    g_assert_true(terminal_output_write_chunk(payload, sizeof(payload)));
    gint64 start = g_get_monotonic_time();
    g_assert_false(terminal_output_flush());
    g_assert_cmpint(g_get_monotonic_time() - start, <, 100000);
    terminal_output_defer("DELETE", TERMINAL_OUTPUT_ONCE);
    GByteArray *received = g_byte_array_new();
    while ((n = read(fds[0], bytes, sizeof(bytes))) > 0)
        g_byte_array_append(received, (guint8 *)bytes, n);
    g_assert_cmpuint(received->len, >, fill);
    g_assert_cmpuint(received->len - fill, <, sizeof(payload));
    g_assert_true(terminal_output_flush());
    while ((n = read(fds[0], bytes, sizeof(bytes))) > 0)
        g_byte_array_append(received, (guint8 *)bytes, n);
    g_assert_cmpuint(received->len, ==, fill + sizeof(payload) + 8);
    g_assert_cmpmem(received->data + fill, sizeof(payload), payload, sizeof(payload));
    g_assert_cmpmem(received->data + fill + sizeof(payload), 8, "\033\\DELETE", 8);
    g_assert_cmpint(fcntl(fileno(stream), F_GETFL), ==, flags);
    stdout = saved; fclose(stream); close(fds[0]); g_byte_array_unref(received);
#else
    g_test_skip("Linux pipe partial-write semantics regression");
#endif
}

static void test_graphics_chunk_idle_timeout_reports_failure(void) {
    int fds[2]; g_assert_cmpint(pipe(fds), ==, 0);
    int flags = fcntl(fds[1], F_GETFL);
    g_assert_cmpint(fcntl(fds[1], F_SETFL, flags | O_NONBLOCK), ==, 0);
    g_assert_cmpint(fcntl(fds[0], F_SETFL, O_NONBLOCK), ==, 0);
    gchar bytes[4096]; memset(bytes, 'F', sizeof(bytes));
    while (write(fds[1], bytes, sizeof(bytes)) > 0) {}
    g_assert_cmpint(fcntl(fds[1], F_SETFL, flags), ==, 0);
    FILE *saved = stdout, *stream = fdopen(fds[1], "w"); stdout = stream;
    g_assert_true(terminal_output_write_chunk("DATA", 4));
    gint64 start = g_get_monotonic_time();
    gboolean failed = FALSE;
    while (g_get_monotonic_time() - start < 5 * G_TIME_SPAN_SECOND) {
        g_assert_false(terminal_output_flush());
        if (terminal_output_take_chunk_error()) { failed = TRUE; break; }
        g_usleep(10000);
    }
    g_assert_true(failed);
    g_assert_cmpint(g_get_monotonic_time() - start, >=, 2 * G_TIME_SPAN_SECOND);
    g_assert_cmpint(g_get_monotonic_time() - start, <, 10 * G_TIME_SPAN_SECOND);
    while (read(fds[0], bytes, sizeof(bytes)) > 0) {}
    g_assert_true(terminal_output_flush());
    g_assert_cmpint(read(fds[0], bytes, sizeof(bytes)), ==, 2);
    g_assert_cmpmem(bytes, 2, "\033\\", 2);
    g_assert_false(terminal_output_take_chunk_error());
    stdout = saved; fclose(stream); close(fds[0]);
}

static void test_session_restore_keeps_original_endpoint(gconstpointer data) {
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    g_assert_cmpint(master, >=, 0);
    g_assert_cmpint(grantpt(master), ==, 0);
    g_assert_cmpint(unlockpt(master), ==, 0);
    int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
    g_assert_cmpint(slave, >=, 0);
    /* Keep the PTY readable after the session releases its last writer. */
    int witness = dup(slave);
    g_assert_cmpint(witness, >=, 0);
    FILE *stream = fdopen(slave, "w");
    FILE *redirected = tmpfile();
    g_assert_nonnull(stream);
    g_assert_nonnull(redirected);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    g_assert_true(terminal_output_begin_session());
    if (GPOINTER_TO_INT(data) == 2) {
        g_assert_true(terminal_output_write_chunk("OLD-CHUNK", 9));
    }
    terminal_output_defer("RESTORE", TERMINAL_OUTPUT_RESTORE);
    g_assert_cmpint(dup2(fileno(redirected), slave), ==, slave);

    get_calls = restore_calls = enable_calls = fail_restore = fail_enable = 0;
    fail_get = 1;
    get_failure_errno = GPOINTER_TO_INT(data) == 2 ? EAGAIN : 0;
    terminal_output_set_fcntl_for_test(failing_fcntl);
    g_assert_false(terminal_output_end_session());
    get_failure_errno = 0;
    if (GPOINTER_TO_INT(data) == 2) g_assert_true(terminal_output_take_chunk_error());
    g_assert_true(stdout == stream);
    int retained = failed_get_fd;
    g_assert_cmpint(retained, !=, slave);
    g_assert_cmpint(fcntl(retained, F_GETFD), ==, FD_CLOEXEC);
    terminal_output_set_fcntl_for_test(NULL);
    if (GPOINTER_TO_INT(data) == 4) {
        terminal_output_set_fcntl_for_test(failing_fcntl);
        fail_get_fd = retained;
        g_assert_true(terminal_output_write("normal output", 13));
        g_assert_cmpint(fcntl(retained, F_GETFD), ==, FD_CLOEXEC);
        fail_get_fd = -1;
        terminal_output_set_fcntl_for_test(NULL);
        g_usleep(110000); /* Detached cleanup has independent retry backoff. */
    } else if (GPOINTER_TO_INT(data) == 3) {
        terminal_output_set_fcntl_for_test(failing_fcntl);
        fail_get_fd = slave;
        g_assert_false(terminal_output_flush());
        fail_get_fd = -1;
        g_assert_cmpint(failed_get_fd, ==, slave);
        g_assert_cmpint(fcntl(retained, F_GETFD), ==, -1);
        terminal_output_set_fcntl_for_test(NULL);
        g_assert_true(terminal_output_flush());
    } else if (GPOINTER_TO_INT(data)) {
        g_assert_cmpint(fputs("normal output", stdout), >=, 0);
    }
    g_assert_true(terminal_output_flush());
    g_assert_cmpint(fcntl(retained, F_GETFD), ==, -1);
    g_assert_cmpint(errno, ==, EBADF);
    if (GPOINTER_TO_INT(data) == 3) {
        g_assert_cmpint(ftell(redirected), ==, 2); /* ST for its own failed preflight. */
    } else if (GPOINTER_TO_INT(data)) {
        rewind(redirected);
        char redirected_bytes[32];
        gsize length = fread(redirected_bytes, 1, sizeof(redirected_bytes), redirected);
        g_assert_cmpmem(redirected_bytes, length, "normal output", 13);
    } else {
        g_assert_cmpint(ftell(redirected), ==, 0);
    }
    char bytes[32];
    ssize_t count = read(master, bytes, sizeof(bytes));
    if (GPOINTER_TO_INT(data) == 2) {
        g_assert_cmpmem(bytes, (gsize)count, "\033\\RESTORE\033\\", 11);
    } else {
        g_assert_cmpmem(bytes, (gsize)count, "\033\\RESTORE", 9);
    }
    g_assert_cmpint(close(master), ==, 0);
    g_assert_cmpint(close(witness), ==, 0);
    stdout = saved;
    fclose(stream);
    fclose(redirected);
}

void register_terminal_output_blocking_tests(void) {
    g_test_add_func("/terminal_output/chunk/idle_timeout", test_graphics_chunk_idle_timeout_reports_failure);
    g_test_add_func("/terminal_output/chunk/partial_packet_cleanup", test_graphics_chunk_partial_progress_then_cleanup);
    g_test_add_data_func("/terminal_output/chunk/full_pipe", NULL, test_graphics_chunk_resumes_without_blocking);
    g_test_add_data_func("/terminal_output/chunk/full_pipe_session", GINT_TO_POINTER(1), test_graphics_chunk_resumes_without_blocking);
    g_test_add_data_func("/terminal_output/session_restore_keeps_original_endpoint", NULL,
                         test_session_restore_keeps_original_endpoint);
    g_test_add_data_func("/terminal_output/session_restore_preserves_redirected_output", GINT_TO_POINTER(1),
                         test_session_restore_keeps_original_endpoint);
    g_test_add_data_func("/terminal_output/session_restore_drops_failed_chunk", GINT_TO_POINTER(2),
                         test_session_restore_keeps_original_endpoint);
    g_test_add_data_func("/terminal_output/session_restore_survives_stdout_failure", GINT_TO_POINTER(3),
                         test_session_restore_keeps_original_endpoint);
    g_test_add_data_func("/terminal_output/session_restore_does_not_block_stdout", GINT_TO_POINTER(4),
                         test_session_restore_keeps_original_endpoint);
    g_test_add_data_func("/terminal_output/preflight/outer", GUINT_TO_POINTER(3), test_once_preflight_retained);
    g_test_add_data_func("/terminal_output/preflight/descriptor", GUINT_TO_POINTER(4), test_once_preflight_retained);
    g_test_add_data_func("/terminal_output/preflight/session", GUINT_TO_POINTER(1), test_once_preflight_retained);
    g_test_add_data_func("/terminal_output/preflight/enable_nonblock", GUINT_TO_POINTER(5), test_once_preflight_retained);
    g_test_add_data_func("/terminal_output/restore_flags/direct", NULL, test_restore_flags_retry);
    g_test_add_data_func("/terminal_output/restore_flags/session", GINT_TO_POINTER(1), test_restore_flags_retry);
    g_test_add_data_func("/terminal_output/transient_pipe_preserves_payload", NULL, test_transient_pipe_preserves_payload);
    g_test_add_data_func("/terminal_output/slow_pipe/direct", GUINT_TO_POINTER(1), test_transient_pipe_preserves_payload);
    g_test_add_data_func("/terminal_output/slow_pipe/session", GUINT_TO_POINTER(2), test_transient_pipe_preserves_payload);
    g_test_add_func("/terminal_output/partial_session_preserves_progress", test_partial_session_preserves_progress);
    g_test_add_data_func("/terminal_output/full_pipe/payload", GINT_TO_POINTER(0), test_full_pipe_returns);
    g_test_add_data_func("/terminal_output/full_pipe/buffered", GINT_TO_POINTER(1), test_full_pipe_returns);
    g_test_add_data_func("/terminal_output/full_pipe/legacy_session", GINT_TO_POINTER(3), test_full_pipe_returns);
    g_test_add_data_func("/terminal_output/full_pipe/legacy_stops_after_failure", GINT_TO_POINTER(4), test_full_pipe_returns);
    g_test_add_data_func("/terminal_output/stopped_terminal", GINT_TO_POINTER(2), test_full_pipe_returns);
}
