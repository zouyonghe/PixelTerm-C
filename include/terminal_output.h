#ifndef TERMINAL_OUTPUT_H
#define TERMINAL_OUTPUT_H

#include <glib.h>
#include <sys/types.h>

typedef enum {
    TERMINAL_OUTPUT_ONCE,
    TERMINAL_OUTPUT_RESTORE
} TerminalOutputPolicy;

/* Main-context stdout owner. FALSE means current output is pending; callers
 * must not emit UI bytes until ready/flush succeeds. Detached cleanup retries
 * independently. Background failures wait 100 ms. */
gboolean terminal_output_ready(gint64 now_us);
gboolean terminal_output_flush(void);
/* Scope legacy stdio rendering through the same bounded descriptor writer.
 * The session owns its original endpoint through deferred cleanup. Memory/test
 * streams are left untouched. End restores the caller's stdout. */
gboolean terminal_output_begin_session(void);
gboolean terminal_output_end_session(void);
/* Checked payload submission. Descriptor writes temporarily use O_NONBLOCK so
 * a full pipe or flow-controlled terminal cannot stall cleanup. Poll for up to
 * 2 seconds without progress, then restore flags (retry before later submissions if
 * restoration fails). Failed payloads are never replayed. */
gboolean terminal_output_write(const char *bytes, gsize length);
/* Accept one <= 8 KiB graphics chunk. flush/ready resume exact partial progress
 * in <= 2 ms slices before any other output; TRUE means ownership was accepted. */
gboolean terminal_output_write_chunk(const char *bytes, gsize length);
/* Consume a failed asynchronous chunk; the owner must abort the incomplete image. */
gboolean terminal_output_take_chunk_error(void);
/* Monotonic timestamp of latest accepted chunk/progress; zero before any chunk. */
gint64 terminal_output_chunk_progress_us(void);
/* Internal main-owner transport: bounded write to an explicit descriptor,
 * independent of stdout sessions/queues. Returns exact progress or -1/errno.
 * Main owner only: callers must serialize all output on shared descriptors.
 * attempted may be NULL; otherwise becomes TRUE when write is called. */
ssize_t terminal_output_descriptor_write(int fd, const char *bytes, gsize length,
                                        gboolean *attempted);
/* Same progress contract, additionally capped by an absolute monotonic deadline.
 * A zero deadline retains the ordinary 2 second no-progress bound. */
ssize_t terminal_output_descriptor_write_until(int fd, const char *bytes, gsize length,
                                               gboolean *attempted, gint64 deadline_us);
/* Retry pending descriptor flag restoration independently of stdout/queued output. */
gboolean terminal_output_retry_descriptor_restore(void);
/* Copies bytes and prefixes ST. ONCE stays queued if no write was attempted,
 * and becomes ST-only after uncertain delivery;
 * RESTORE is retained until success or process exit (one final attempt).
 * Identical restores coalesce. */
void terminal_output_defer(const char *bytes, TerminalOutputPolicy policy);
/* Discard buffered failed bytes, recover ST, and retain a failed retry deadline. */
gboolean terminal_output_recover(void);
/* Purge all unsent stdout bytes and clear its error state; do not queue or drain ST. */
void terminal_output_discard(void);
void terminal_output_defer_recovery(void);
/* Main-owner test seam; NULL restores the system fcntl implementation. */
void terminal_output_set_fcntl_for_test(int (*hook)(int fd, int operation, int flags));

#endif
