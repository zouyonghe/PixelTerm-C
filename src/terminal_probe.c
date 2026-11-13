#include "terminal_probe.h"

#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static gboolean terminal_probe_default_stdin_is_tty(gpointer user_data) {
    (void)user_data;
    return isatty(STDIN_FILENO) != 0;
}

static gboolean terminal_probe_default_stdout_is_tty(gpointer user_data) {
    (void)user_data;
    return isatty(STDOUT_FILENO) != 0;
}

static int terminal_probe_default_tcgetattr(struct termios *termios_state, gpointer user_data) {
    (void)user_data;
    return tcgetattr(STDIN_FILENO, termios_state);
}

static int terminal_probe_default_tcsetattr(gint optional_actions,
                                            const struct termios *termios_state,
                                            gpointer user_data) {
    (void)user_data;
    return tcsetattr(STDIN_FILENO, optional_actions, termios_state);
}

static int terminal_probe_default_tcflush(gint queue_selector, gpointer user_data) {
    (void)user_data;
    return tcflush(STDIN_FILENO, queue_selector);
}

static ssize_t terminal_probe_default_write(const char *buf, size_t len, gpointer user_data) {
    (void)user_data;
    return write(STDOUT_FILENO, buf, len);
}

static gint terminal_probe_default_read_char_with_timeout(gint timeout_ms, gpointer user_data) {
    (void)user_data;

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    int ready = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (ready <= 0 || !FD_ISSET(STDIN_FILENO, &fds)) {
        return 0;
    }

    unsigned char ch = 0;
    ssize_t bytes_read = read(STDIN_FILENO, &ch, 1);
    return (bytes_read == 1) ? (gint)ch : 0;
}

static gint64 terminal_probe_default_monotonic_time_us(gpointer user_data) {
    (void)user_data;
    return g_get_monotonic_time();
}

static const TerminalProbeTransportHooks k_default_hooks = {
    .stdin_is_tty = terminal_probe_default_stdin_is_tty,
    .stdout_is_tty = terminal_probe_default_stdout_is_tty,
    .tcgetattr_fn = terminal_probe_default_tcgetattr,
    .tcsetattr_fn = terminal_probe_default_tcsetattr,
    .tcflush_fn = terminal_probe_default_tcflush,
    .write_fn = terminal_probe_default_write,
    .read_char_with_timeout_fn = terminal_probe_default_read_char_with_timeout,
    .monotonic_time_us_fn = terminal_probe_default_monotonic_time_us,
};

static const TerminalProbeTransportHooks *g_terminal_probe_hooks = &k_default_hooks;
static gpointer g_terminal_probe_user_data = NULL;

/* Startup probes and event handling run on the main thread. Stop reading when
 * this queue fills so unread keystrokes stay in the terminal's input buffer. */
static GQueue g_terminal_probe_pending_input = G_QUEUE_INIT;
#define TERMINAL_PROBE_PENDING_LIMIT 4096

gboolean terminal_probe_has_pending_input(void) {
    return !g_queue_is_empty(&g_terminal_probe_pending_input);
}

gint terminal_probe_take_pending_byte(void) {
    if (!terminal_probe_has_pending_input()) {
        return -1;
    }
    return GPOINTER_TO_INT(g_queue_pop_head(&g_terminal_probe_pending_input)) - 1;
}

static void terminal_probe_keep_input(const char *bytes, gsize length) {
    for (gsize i = 0; i < length; i++) {
        g_queue_push_tail(&g_terminal_probe_pending_input,
                          GINT_TO_POINTER((guchar)bytes[i] + 1));
    }
}

static gboolean terminal_probe_is_reply_prefix(const char *sequence) {
    return g_str_has_prefix(sequence, "\033[?") ||
           g_str_has_prefix(sequence, "\033P>|") ||
           g_str_has_prefix(sequence, "\033_G");
}

static gboolean terminal_probe_sequence_complete(const char *sequence, gsize length) {
    if (length < 2) {
        return FALSE;
    }
    if (sequence[1] == '[') {
        return length > 2 && sequence[length - 1] >= '@' && sequence[length - 1] <= '~';
    }
    if (sequence[1] == 'P' || sequence[1] == '_') {
        return length >= 4 && sequence[length - 2] == '\033' && sequence[length - 1] == '\\';
    }
    return TRUE;
}

static gboolean terminal_probe_configure_raw_input(const TerminalProbeTransportHooks *hooks,
                                                   gpointer user_data,
                                                   struct termios *saved_termios) {
    if (hooks->tcgetattr_fn(saved_termios, user_data) != 0) {
        return FALSE;
    }

    struct termios raw = *saved_termios;
    raw.c_lflag &= ~(ICANON | ECHO | IEXTEN);
    raw.c_lflag |= ISIG;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    return hooks->tcsetattr_fn(TCSANOW, &raw, user_data) == 0;
}

gssize terminal_probe_query_response(const char *query,
                                     gint timeout_ms,
                                     gint poll_timeout_ms,
                                     gchar terminator,
                                     char *buffer,
                                     gsize buffer_size) {
    if (!query || query[0] == '\0' || !buffer || buffer_size == 0 ||
        timeout_ms <= 0 || poll_timeout_ms <= 0) {
        return -1;
    }

    buffer[0] = '\0';

    const TerminalProbeTransportHooks *hooks = g_terminal_probe_hooks ? g_terminal_probe_hooks : &k_default_hooks;
    gpointer user_data = g_terminal_probe_user_data;
    if (!hooks->stdin_is_tty || !hooks->stdout_is_tty || !hooks->tcgetattr_fn ||
        !hooks->tcsetattr_fn || !hooks->tcflush_fn || !hooks->write_fn ||
        !hooks->read_char_with_timeout_fn || !hooks->monotonic_time_us_fn) {
        return -1;
    }

    if (!hooks->stdin_is_tty(user_data) || !hooks->stdout_is_tty(user_data)) {
        return -1;
    }

    struct termios saved_termios;
    if (!terminal_probe_configure_raw_input(hooks, user_data, &saved_termios)) {
        return -1;
    }

    gssize length = -1;
    do {
        size_t query_len = strlen(query);
        gint64 deadline_us = hooks->monotonic_time_us_fn(user_data) + (gint64)timeout_ms * 1000;
        gsize offset = 0;
        char sequence[512] = {0};
        gsize sequence_length = 0;
        gboolean query_sent = FALSE;
        gboolean sequence_before_query = FALSE;

        while (offset + 1 < buffer_size && sequence_length + 1 < sizeof(sequence) &&
               g_terminal_probe_pending_input.length + sequence_length < TERMINAL_PROBE_PENDING_LIMIT) {
            gint64 now_us = hooks->monotonic_time_us_fn(user_data);
            gint64 remaining_us = deadline_us - now_us;
            if (remaining_us <= 0) {
                break;
            }

            gint remaining_ms = (gint)MAX((remaining_us + 999) / 1000, (gint64)1);
            gint step_timeout_ms = query_sent ? MIN(poll_timeout_ms, remaining_ms) : 0;
            gint ch = hooks->read_char_with_timeout_fn(step_timeout_ms, user_data);
            if (ch == 0) {
                if (!query_sent) {
                    /* Establish a non-flushing boundary; preserve queued keys but
                     * never use replies that started before this query. */
                    if (hooks->write_fn(query, query_len, user_data) != (ssize_t)query_len) {
                        break;
                    }
                    query_sent = TRUE;
                    deadline_us = hooks->monotonic_time_us_fn(user_data) + (gint64)timeout_ms * 1000;
                }
                continue;
            }

            if (sequence_length == 0 && ch != '\033') {
                char byte = (char)ch;
                terminal_probe_keep_input(&byte, 1);
                continue;
            }
            if (sequence_length == 1 && ch == '\033') {
                terminal_probe_keep_input(sequence, 1);
                sequence_before_query = !query_sent;
                continue;
            }
            if (sequence_length == 0) {
                sequence_before_query = !query_sent;
            }
            sequence[sequence_length++] = (char)ch;
            sequence[sequence_length] = '\0';
            if (!terminal_probe_sequence_complete(sequence, sequence_length)) {
                continue;
            }

            gboolean is_reply = terminal_probe_is_reply_prefix(sequence) ||
                                strcmp(sequence, "\033[0n") == 0 ||
                                strcmp(sequence, "\033[3n") == 0;
            gboolean current_reply = is_reply && query_sent && !sequence_before_query;
            if (current_reply) {
                if (sequence_length >= buffer_size - offset) {
                    sequence_length = 0;
                    break;
                }
                memcpy(buffer + offset, sequence, sequence_length);
                offset += sequence_length;
            } else if (!is_reply) {
                terminal_probe_keep_input(sequence, sequence_length);
            }
            sequence_length = 0;
            sequence[0] = '\0';
            if (current_reply && terminator != '\0' && ch == (guchar)terminator) {
                break;
            }
        }
        /* Preserve framing so the input parser can finish discarding late replies. */
        if (sequence_length > 0) {
            terminal_probe_keep_input(sequence, sequence_length);
        }

        buffer[offset] = '\0';
        length = query_sent ? (gssize)offset : -1;
    } while (0);

    if (hooks->tcsetattr_fn(TCSANOW, &saved_termios, user_data) != 0) {
        return -1;
    }

    return length;
}

void terminal_probe_set_transport_hooks_for_test(const TerminalProbeTransportHooks *hooks,
                                                 gpointer user_data) {
    if (!hooks) {
        terminal_probe_reset_transport_hooks_for_test();
        return;
    }

    g_terminal_probe_hooks = hooks;
    g_terminal_probe_user_data = user_data;
}

void terminal_probe_reset_transport_hooks_for_test(void) {
    g_terminal_probe_hooks = &k_default_hooks;
    g_terminal_probe_user_data = NULL;
    g_queue_clear(&g_terminal_probe_pending_input);
}
