#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "terminal_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#ifdef __linux__
#include <stdio_ext.h>
#endif

/* The main input/output owner services this queue; render workers never use it. */
static GQueue pending_output = G_QUEUE_INIT;
typedef struct {
    GString *bytes;
    gboolean replay_safe;
} PendingOutput;
static gboolean cleanup_registered = FALSE;
static gint64 next_cleanup_retry_us = 0;
static FILE *session_stdout;
static FILE *session_stream;
static int session_fd;
static gboolean session_write_failed;
static gboolean session_write_attempted;
static gboolean recovery_pending;
static GString *pending_chunk;
static gboolean chunk_failed;
static gsize pending_chunk_offset;
static gint64 pending_chunk_progress_us;
static int restore_fd = -1;
static int restore_flags;
static int (*fcntl_hook)(int, int, int);

void terminal_output_set_fcntl_for_test(int (*hook)(int, int, int)) {
    fcntl_hook = hook;
}

static int output_fcntl(int fd, int operation, int flags) {
    return fcntl_hook ? fcntl_hook(fd, operation, flags) : fcntl(fd, operation, flags);
}

static gboolean restore_descriptor_flags(void) {
    if (restore_fd < 0) return TRUE;
    if (output_fcntl(restore_fd, F_SETFL, restore_flags) < 0) return FALSE;
    restore_fd = -1;
    return TRUE;
}

static gboolean restore_descriptor(int fd, int flags) {
    restore_fd = fd;
    restore_flags = flags;
    return restore_descriptor_flags();
}

gboolean terminal_output_retry_descriptor_restore(void) {
    return restore_descriptor_flags();
}

ssize_t terminal_output_descriptor_write(int fd, const char *bytes, gsize length, gboolean *attempted) {
    return terminal_output_descriptor_write_until(fd, bytes, length, attempted, 0);
}

ssize_t terminal_output_descriptor_write_until(int fd, const char *bytes, gsize length,
                                               gboolean *attempted, gint64 deadline_us) {
    if (!restore_descriptor_flags()) return -1;
    int flags = output_fcntl(fd, F_GETFL, 0);
    gboolean changed = flags >= 0 && !(flags & O_NONBLOCK);
    if (flags < 0 || (changed && output_fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)) return -1;
    gsize written = 0;
    /* SSH and terminal graphics consumers can pause longer than one render tick. */
    const gint64 idle_timeout_us = 2 * G_TIME_SPAN_SECOND;
    gint64 deadline = g_get_monotonic_time() + idle_timeout_us;
    if (deadline_us > 0) deadline = MIN(deadline, deadline_us);
    while (written < length) {
        if (g_get_monotonic_time() >= deadline) {
            errno = EAGAIN;
            break;
        }
        if (attempted) *attempted = TRUE;
        ssize_t count = write(fd, bytes + written, length - written);
        if (count > 0) {
            written += (gsize)count;
            deadline = g_get_monotonic_time() + idle_timeout_us;
            if (deadline_us > 0) deadline = MIN(deadline, deadline_us);
        } else if (count < 0 && errno == EINTR) {
            /* Interrupted writes still share the no-progress deadline. */
        } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            gint64 remaining = deadline - g_get_monotonic_time();
            if (remaining <= 0) break;
            struct pollfd output = {.fd = fd, .events = POLLOUT};
            if (poll(&output, 1, (int)((remaining + 999) / 1000)) < 0 && errno != EINTR) break;
        } else break;
        if (g_get_monotonic_time() >= deadline) break;
    }
    /* Preserve exact progress even when restoring flags needs a later retry. */
    int write_errno = errno;
    if (changed) restore_descriptor(fd, flags);
    errno = write_errno;
    return written || !length ? (ssize_t)written : -1;
}

#ifdef __APPLE__
static int terminal_output_session_write(void *data, const char *bytes, int length) {
    (void)data;
    if (session_write_failed) return -1;
    ssize_t written = terminal_output_descriptor_write(session_fd, bytes, (gsize)length, &session_write_attempted);
    if (written != length || restore_fd >= 0) session_write_failed = TRUE;
    return (int)written;
}
#else
static ssize_t terminal_output_session_write(void *data, const char *bytes, size_t length) {
    (void)data;
    if (session_write_failed) return 0;
    ssize_t written = terminal_output_descriptor_write(session_fd, bytes, length, &session_write_attempted);
    if (written < 0 || (gsize)written != length || restore_fd >= 0) session_write_failed = TRUE;
    return written < 0 ? 0 : written;
}
#endif

gboolean terminal_output_begin_session(void) {
    if (session_stream) return FALSE;
    if (fileno(stdout) < 0) return TRUE;
    if (!terminal_output_flush()) return FALSE;
    session_fd = fileno(stdout);
#ifdef __APPLE__
    FILE *stream = funopen(NULL, NULL, terminal_output_session_write, NULL, NULL);
#else
    cookie_io_functions_t hooks = {.write = terminal_output_session_write};
    FILE *stream = fopencookie(NULL, "w", hooks);
#endif
    if (!stream) return FALSE;
    session_stdout = stdout;
    session_stream = stream;
    session_write_failed = FALSE;
    stdout = stream;
    return TRUE;
}

gboolean terminal_output_end_session(void) {
    if (!session_stream) return TRUE;
    gboolean success = terminal_output_flush();
    terminal_output_discard();
    stdout = session_stdout;
    if (fclose(session_stream) != 0) success = FALSE;
    session_stdout = session_stream = NULL;
    return success;
}

/* Bound kernel writes without changing descriptor flags between submissions. */
static gboolean terminal_output_submit(const char *bytes, gsize length, gboolean *attempted) {
    if (attempted) *attempted = FALSE;
    if (!restore_descriptor_flags()) return FALSE;
    int fd = fileno(stdout);
    int flags = fd >= 0 ? output_fcntl(fd, F_GETFL, 0) : -1;
    gboolean changed = flags >= 0 && !(flags & O_NONBLOCK);
    if (fd >= 0 && (flags < 0 || (changed && output_fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)))
        return FALSE;
    gsize written = 0;
    int flushed = fflush(stdout);
    if (flushed == 0 && !ferror(stdout)) {
        if (fd < 0) {
            session_write_attempted = FALSE;
            if (attempted) *attempted = length > 0;
            written = length ? fwrite(bytes, 1, length, stdout) : 0;
            flushed = fflush(stdout);
            if (attempted && stdout == session_stream) *attempted = session_write_attempted;
        } else {
            ssize_t count = terminal_output_descriptor_write(fd, bytes, length, attempted);
            if (count > 0) written = (gsize)count;
            if (count < 0) flushed = -1;
        }
    }
    gboolean success = written == length && flushed == 0 && !ferror(stdout) &&
        !(stdout == session_stream && session_write_failed);
    if (changed && !restore_descriptor(fd, flags)) success = FALSE;
    return success;
}

void terminal_output_discard(void) {
    if (stdout == session_stream) session_write_failed = FALSE;
#ifdef __linux__
    __fpurge(stdout);
#else
    fpurge(stdout);
#endif
    clearerr(stdout);
}

gboolean terminal_output_recover(void) {
    terminal_output_discard();
    terminal_output_defer_recovery();
    gboolean recovered = terminal_output_flush();
    return recovered;
}

gboolean terminal_output_ready(gint64 now_us) {
    if (now_us < next_cleanup_retry_us) return FALSE;
    return terminal_output_flush();
}

gboolean terminal_output_take_chunk_error(void) {
    gboolean failed = chunk_failed;
    chunk_failed = FALSE;
    return failed;
}

gboolean terminal_output_write_chunk(const char *bytes, gsize length) {
    if (!bytes || length > 8192 || !terminal_output_flush()) return FALSE;
    pending_chunk = g_string_new_len(bytes, length);
    pending_chunk_offset = 0;
    pending_chunk_progress_us = g_get_monotonic_time();
    /* The next ready/flush owns transmission, never the caller's buffer. */
    return TRUE;
}

static gboolean terminal_output_drain_chunk(void) {
    if (!pending_chunk) return TRUE;
    int fd = stdout == session_stream ? session_fd : fileno(stdout);
    if (fd < 0) {
        if (!terminal_output_submit(pending_chunk->str + pending_chunk_offset,
                                   pending_chunk->len - pending_chunk_offset, NULL))
            goto failed;
        pending_chunk_offset = pending_chunk->len;
    } else {
        errno = 0;
        ssize_t count = terminal_output_descriptor_write_until(fd,
            pending_chunk->str + pending_chunk_offset,
            pending_chunk->len - pending_chunk_offset, NULL,
            g_get_monotonic_time() + 2000);
        if (count > 0) {
            pending_chunk_offset += (gsize)count;
            pending_chunk_progress_us = g_get_monotonic_time();
        } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            goto failed;
        }
    }
    if (pending_chunk_offset < pending_chunk->len) {
        if (g_get_monotonic_time() - pending_chunk_progress_us >= 2 * G_TIME_SPAN_SECOND)
            goto failed;
        return FALSE;
    }
    g_string_free(pending_chunk, TRUE);
    pending_chunk = NULL;
    return TRUE;
failed:
    chunk_failed = TRUE;
    g_string_free(pending_chunk, TRUE);
    pending_chunk = NULL;
    terminal_output_defer_recovery();
    next_cleanup_retry_us = g_get_monotonic_time() + 100000;
    return FALSE;
}

gboolean terminal_output_flush(void) {
    if (!terminal_output_drain_chunk()) return FALSE;
    if (recovery_pending) terminal_output_discard();
    /* Check buffered UI bytes before clearing errors or submitting queued controls. */
    if (ferror(stdout) || !terminal_output_submit(NULL, 0, NULL)) {
        terminal_output_discard();
        terminal_output_defer_recovery();
        next_cleanup_retry_us = g_get_monotonic_time() + 100000;
        return FALSE;
    }
    while (!g_queue_is_empty(&pending_output)) {
        PendingOutput *pending = g_queue_peek_head(&pending_output);
        GString *command = pending->bytes;
        clearerr(stdout);
        gboolean attempted;
        if (!terminal_output_submit(command->str, command->len, &attempted)) {
            terminal_output_discard();
            recovery_pending = TRUE;
            /* Delivery may have succeeded: repeating d=I could delete a reused ID. */
            if (attempted && !pending->replay_safe) {
                g_string_assign(command, "\033\\");
                pending->replay_safe = TRUE;
            }
            next_cleanup_retry_us = g_get_monotonic_time() + 100000;
            return FALSE;
        }
        g_queue_pop_head(&pending_output);
        g_string_free(command, TRUE);
        g_free(pending);
    }
    next_cleanup_retry_us = 0;
    recovery_pending = FALSE;
    return TRUE;
}

static void terminal_output_cleanup_at_exit(void) {
    if (pending_chunk) {
        g_string_free(pending_chunk, TRUE);
        pending_chunk = NULL;
        terminal_output_defer_recovery();
    }
    if (!terminal_output_flush()) {
        /* A sticky/buffered frame error must not consume the final restore attempt. */
        terminal_output_flush();
    }
    /* Process exit is the last retry opportunity, even if the terminal is gone. */
    while (!g_queue_is_empty(&pending_output)) {
        PendingOutput *pending = g_queue_pop_head(&pending_output);
        /* A blocked head must not starve later independently ST-framed restores.
         * Already-attempted ambiguous releases have become ST-only above. */
        terminal_output_discard();
        if (!terminal_output_submit(pending->bytes->str, pending->bytes->len, NULL))
            terminal_output_discard();
        g_string_free(pending->bytes, TRUE);
        g_free(pending);
    }
}

static void terminal_output_queue(GString *command, gboolean replay_safe) {
    if (!cleanup_registered) {
        cleanup_registered = atexit(terminal_output_cleanup_at_exit) == 0;
    }
    PendingOutput *pending = g_new0(PendingOutput, 1);
    pending->bytes = command;
    pending->replay_safe = replay_safe;
    g_queue_push_tail(&pending_output, pending);
}

void terminal_output_defer(const char *controls, TerminalOutputPolicy policy) {
    GString *command = g_string_new("\033\\");
    g_string_append(command, controls);
    for (GList *link = pending_output.head; link; link = link->next) {
        PendingOutput *pending = link->data;
        if (policy == TERMINAL_OUTPUT_RESTORE && pending->replay_safe && g_strcmp0(pending->bytes->str, command->str) == 0) {
            g_string_free(command, TRUE);
            return;
        }
    }
    terminal_output_queue(command, policy == TERMINAL_OUTPUT_RESTORE);
}

void terminal_output_defer_recovery(void) {
    recovery_pending = TRUE;
    PendingOutput *tail = g_queue_peek_tail(&pending_output);
    if (tail && tail->bytes->len == 2 && memcmp(tail->bytes->str, "\033\\", 2) == 0) return;
    terminal_output_queue(g_string_new("\033\\"), TRUE);
}


gboolean terminal_output_write(const char *bytes, gsize length) {
    if (!terminal_output_flush()) return FALSE;
    clearerr(stdout);
    if (terminal_output_submit(bytes, length, NULL)) return TRUE;
    terminal_output_recover();
    return FALSE;
}
