#ifndef TERMINAL_OUTPUT_H
#define TERMINAL_OUTPUT_H

#include <glib.h>

typedef enum {
    TERMINAL_OUTPUT_ONCE,
    TERMINAL_OUTPUT_RESTORE
} TerminalOutputPolicy;

/* Main-context stdout owner. FALSE means output is pending; callers must not
 * emit UI bytes until ready/flush succeeds. Background failures wait 100 ms. */
gboolean terminal_output_ready(gint64 now_us);
gboolean terminal_output_flush(void);
/* Checked payload submission. Payloads are never replayed after failure. */
gboolean terminal_output_write(const char *bytes, gsize length);
/* Copies bytes and prefixes ST. ONCE becomes ST-only after uncertain delivery;
 * RESTORE is retained until success or process exit (one final attempt).
 * Identical restores coalesce. */
void terminal_output_defer(const char *bytes, TerminalOutputPolicy policy);
/* Discard buffered failed bytes, recover ST, and retain a failed retry deadline. */
gboolean terminal_output_recover(void);
/* Purge all unsent stdout bytes and clear its error state; do not queue or drain ST. */
void terminal_output_discard(void);
void terminal_output_defer_recovery(void);

#endif
