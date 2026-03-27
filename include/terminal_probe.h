#ifndef TERMINAL_PROBE_H
#define TERMINAL_PROBE_H

#include <glib.h>
#include <stddef.h>
#include <sys/types.h>
#include <termios.h>

enum {
    TERMINAL_PROBE_DEFAULT_TIMEOUT_MS = 120,
    TERMINAL_PROBE_POLL_TIMEOUT_MS = 20
};

typedef struct {
    gboolean (*stdin_is_tty)(gpointer user_data);
    gboolean (*stdout_is_tty)(gpointer user_data);
    int (*tcgetattr_fn)(struct termios *termios_state, gpointer user_data);
    int (*tcsetattr_fn)(gint optional_actions,
                        const struct termios *termios_state,
                        gpointer user_data);
    int (*tcflush_fn)(gint queue_selector, gpointer user_data);
    ssize_t (*write_fn)(const char *buf, size_t len, gpointer user_data);
    gint (*read_char_with_timeout_fn)(gint timeout_ms, gpointer user_data);
    gint64 (*monotonic_time_us_fn)(gpointer user_data);
} TerminalProbeTransportHooks;

gssize terminal_probe_query_response(const char *query,
                                     gint timeout_ms,
                                     gint poll_timeout_ms,
                                     gchar terminator,
                                     char *buffer,
                                     gsize buffer_size);

void terminal_probe_set_transport_hooks_for_test(const TerminalProbeTransportHooks *hooks,
                                                 gpointer user_data);
void terminal_probe_reset_transport_hooks_for_test(void);

#endif
