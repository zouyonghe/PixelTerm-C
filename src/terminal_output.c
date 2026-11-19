#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "terminal_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
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

/* Bound kernel writes without changing descriptor flags between submissions. */
static gboolean terminal_output_submit(const char *bytes, gsize length) {
    int fd = fileno(stdout);
    int flags = fd >= 0 ? fcntl(fd, F_GETFL) : -1;
    gboolean changed = flags >= 0 && !(flags & O_NONBLOCK);
    if (fd >= 0 && (flags < 0 || (changed && fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)))
        return FALSE;
    gsize written = length ? fwrite(bytes, 1, length, stdout) : 0;
    int flushed = fflush(stdout);
    gboolean success = written == length && flushed == 0 && !ferror(stdout);
    if (changed && fcntl(fd, F_SETFL, flags) < 0) success = FALSE;
    return success;
}

void terminal_output_discard(void) {
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

gboolean terminal_output_flush(void) {
    /* Check buffered UI bytes before clearing errors or submitting queued controls. */
    if (ferror(stdout) || !terminal_output_submit(NULL, 0)) {
        terminal_output_discard();
        terminal_output_defer_recovery();
        next_cleanup_retry_us = g_get_monotonic_time() + 100000;
        return FALSE;
    }
    while (!g_queue_is_empty(&pending_output)) {
        PendingOutput *pending = g_queue_peek_head(&pending_output);
        GString *command = pending->bytes;
        clearerr(stdout);
        if (!terminal_output_submit(command->str, command->len)) {
            terminal_output_discard();
            /* Delivery may have succeeded: repeating d=I could delete a reused ID. */
            if (!pending->replay_safe) {
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
    return TRUE;
}

static void terminal_output_cleanup_at_exit(void) {
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
        if (!terminal_output_submit(pending->bytes->str, pending->bytes->len))
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
    PendingOutput *tail = g_queue_peek_tail(&pending_output);
    if (tail && tail->bytes->len == 2 && memcmp(tail->bytes->str, "\033\\", 2) == 0) return;
    terminal_output_queue(g_string_new("\033\\"), TRUE);
}


gboolean terminal_output_write(const char *bytes, gsize length) {
    if (!terminal_output_flush()) return FALSE;
    clearerr(stdout);
    if (terminal_output_submit(bytes, length)) return TRUE;
    terminal_output_recover();
    return FALSE;
}
