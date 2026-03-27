#include <glib.h>

#include "terminal_probe.h"
#include "terminal_protocol_resolver.h"
#include "terminal_protocols.h"

static const gchar * const k_terminal_env_keys[] = {
    "TERM",
    "TERM_PROGRAM",
    "SSH_CONNECTION",
    "SSH_CLIENT",
    "SSH_TTY",
    "TMUX",
    "STY",
    "WEZTERM_EXECUTABLE",
    "WEZTERM_EXECUTABLE_DIR",
    "WEZTERM_PANE",
    "WEZTERM_UNIX_SOCKET",
    "KITTY_WINDOW_ID",
    "KITTY_PID",
    "KITTY_INSTALLATION_DIR",
    "ITERM_SESSION_ID",
    "LC_TERMINAL",
    "GHOSTTY_RESOURCES_DIR",
    "GHOSTTY_BIN_DIR",
    "TERMINAL_NAME",
    "EAT_SHELL_INTEGRATION_DIR",
    "MLTERM",
    "KONSOLE_VERSION",
    "CTX_BACKEND",
    "VTE_VERSION",
    "ComSpec",
    "COMSPEC",
    "XTERM_VERSION",
    NULL,
};

#define TERMINAL_PROTOCOL_ENV_KEY_COUNT (G_N_ELEMENTS(k_terminal_env_keys) - 1)

typedef struct {
    gchar *saved_values[TERMINAL_PROTOCOL_ENV_KEY_COUNT];
} TerminalProtocolEnvFixture;

static gchar *g_terminal_protocol_baseline_term = NULL;
static gchar *g_terminal_protocol_baseline_term_program = NULL;

static void clear_terminal_protocol_env(void) {
    for (gsize i = 0; k_terminal_env_keys[i]; i++) {
        g_unsetenv(k_terminal_env_keys[i]);
    }
}

static void terminal_protocol_env_fixture_set_up(TerminalProtocolEnvFixture *fixture,
                                                 gconstpointer user_data) {
    (void)user_data;

    for (gsize i = 0; i < TERMINAL_PROTOCOL_ENV_KEY_COUNT; i++) {
        fixture->saved_values[i] = g_strdup(g_getenv(k_terminal_env_keys[i]));
    }
}

static void terminal_protocol_env_fixture_tear_down(TerminalProtocolEnvFixture *fixture,
                                                    gconstpointer user_data) {
    (void)user_data;

    for (gsize i = 0; i < TERMINAL_PROTOCOL_ENV_KEY_COUNT; i++) {
        if (fixture->saved_values[i]) {
            g_setenv(k_terminal_env_keys[i], fixture->saved_values[i], TRUE);
        } else {
            g_unsetenv(k_terminal_env_keys[i]);
        }

        g_clear_pointer(&fixture->saved_values[i], g_free);
    }
}

static void add_terminal_protocol_test(
    const gchar *path,
    void (*test_func)(TerminalProtocolEnvFixture *fixture, gconstpointer user_data)) {
    g_test_add(path, TerminalProtocolEnvFixture, NULL,
               terminal_protocol_env_fixture_set_up,
               test_func,
               terminal_protocol_env_fixture_tear_down);
}

static void assert_weak_candidate_name(const gchar *expected_name) {
    const TerminalProtocolHint *hint = terminal_protocol_env_weak_candidate();

    g_assert_nonnull(hint);
    g_assert_cmpstr(hint->name, ==, expected_name);
}

static void assert_live_support(gboolean expected_kitty,
                                gboolean expected_iterm2,
                                gboolean expected_sixel) {
    g_assert_cmpint(terminal_env_supports_kitty(), ==, expected_kitty);
    g_assert_cmpint(terminal_env_supports_iterm2(), ==, expected_iterm2);
    g_assert_cmpint(terminal_env_supports_sixel(), ==, expected_sixel);
}

static void assert_resolver_decision(TerminalProtocolDecision decision,
                                     TerminalResolvedProtocol expected_protocol,
                                     TerminalProtocolDecisionSource expected_source,
                                     const char *expected_reason) {
    g_assert_cmpint(decision.protocol, ==, expected_protocol);
    g_assert_cmpint(decision.source, ==, expected_source);
    g_assert_cmpstr(decision.reason, ==, expected_reason);
}

typedef struct {
    gboolean stdin_is_tty;
    gboolean stdout_is_tty;
    struct termios saved_termios;
    gboolean has_saved_termios;
    gint tcgetattr_calls;
    gint raw_mode_calls;
    gint restore_calls;
    gint raw_mode_optional_actions;
    gint restore_optional_actions;
    gint tcflush_calls;
    GString *writes;
    GArray *read_timeouts_ms;
    const gchar *response;
    gsize response_length;
    gsize response_offset;
    gint64 now_us;
} TerminalProbeFixture;

static gboolean terminal_probe_fixture_stdin_is_tty(gpointer user_data) {
    TerminalProbeFixture *fixture = user_data;
    return fixture->stdin_is_tty;
}

static gboolean terminal_probe_fixture_stdout_is_tty(gpointer user_data) {
    TerminalProbeFixture *fixture = user_data;
    return fixture->stdout_is_tty;
}

static int terminal_probe_fixture_tcgetattr(struct termios *state, gpointer user_data) {
    TerminalProbeFixture *fixture = user_data;

    memset(state, 0, sizeof(*state));
    state->c_lflag = ICANON | ECHO | IEXTEN;
    state->c_iflag = BRKINT | ICRNL | INPCK | ISTRIP | IXON;
    state->c_cflag = 0;
    state->c_cc[VMIN] = 1;
    state->c_cc[VTIME] = 0;

    fixture->saved_termios = *state;
    fixture->has_saved_termios = TRUE;
    fixture->tcgetattr_calls++;
    return 0;
}

static int terminal_probe_fixture_tcsetattr(gint optional_actions,
                                            const struct termios *state,
                                            gpointer user_data) {
    TerminalProbeFixture *fixture = user_data;

    g_assert_nonnull(state);

    if (fixture->raw_mode_calls == 0) {
        fixture->raw_mode_calls++;
        fixture->raw_mode_optional_actions = optional_actions;
        g_assert_cmpint(optional_actions, ==, TCSANOW);
        g_assert_cmpint(state->c_cc[VMIN], ==, 0);
        g_assert_cmpint(state->c_cc[VTIME], ==, 0);
        g_assert_false((state->c_lflag & ICANON) != 0);
        g_assert_false((state->c_lflag & ECHO) != 0);
        g_assert_false((state->c_lflag & IEXTEN) != 0);
        g_assert_true((state->c_lflag & ISIG) != 0);
    } else {
        fixture->restore_calls++;
        fixture->restore_optional_actions = optional_actions;
        g_assert_cmpint(optional_actions, ==, TCSAFLUSH);
        g_assert_true(fixture->has_saved_termios);
        g_assert_cmpmem(state, sizeof(*state),
                        &fixture->saved_termios, sizeof(fixture->saved_termios));
    }

    return 0;
}

static int terminal_probe_fixture_tcflush(gint queue_selector, gpointer user_data) {
    TerminalProbeFixture *fixture = user_data;

    g_assert_cmpint(queue_selector, ==, TCIFLUSH);
    fixture->tcflush_calls++;
    return 0;
}

static ssize_t terminal_probe_fixture_write(const char *buf, size_t len, gpointer user_data) {
    TerminalProbeFixture *fixture = user_data;

    g_assert_nonnull(buf);
    g_string_append_len(fixture->writes, buf, len);
    return (ssize_t)len;
}

static gint terminal_probe_fixture_read_char_with_timeout(gint timeout_ms, gpointer user_data) {
    TerminalProbeFixture *fixture = user_data;

    g_array_append_val(fixture->read_timeouts_ms, timeout_ms);

    if (!fixture->response || fixture->response_offset >= fixture->response_length) {
        fixture->now_us += (gint64)timeout_ms * 1000;
        return 0;
    }

    fixture->now_us += 1000;
    return (guchar)fixture->response[fixture->response_offset++];
}

static gint64 terminal_probe_fixture_monotonic_time_us(gpointer user_data) {
    TerminalProbeFixture *fixture = user_data;
    return fixture->now_us;
}

static void reset_terminal_probe_hooks(gpointer user_data) {
    (void)user_data;
    terminal_probe_reset_transport_hooks_for_test();
}

static void test_terminal_probe_transport_reads_until_terminator_with_short_timeouts(void) {
    static const gchar k_response[] = "\033[?4;1;2cTAIL";
    static const gchar k_expected_response[] = "\033[?4;1;2c";

    TerminalProbeFixture fixture = {
        .stdin_is_tty = TRUE,
        .stdout_is_tty = TRUE,
        .writes = g_string_new(NULL),
        .read_timeouts_ms = g_array_new(FALSE, FALSE, sizeof(gint)),
        .response = k_response,
        .response_length = sizeof(k_response) - 1,
    };
    static const TerminalProbeTransportHooks hooks = {
        .stdin_is_tty = terminal_probe_fixture_stdin_is_tty,
        .stdout_is_tty = terminal_probe_fixture_stdout_is_tty,
        .tcgetattr_fn = terminal_probe_fixture_tcgetattr,
        .tcsetattr_fn = terminal_probe_fixture_tcsetattr,
        .tcflush_fn = terminal_probe_fixture_tcflush,
        .write_fn = terminal_probe_fixture_write,
        .read_char_with_timeout_fn = terminal_probe_fixture_read_char_with_timeout,
        .monotonic_time_us_fn = terminal_probe_fixture_monotonic_time_us,
    };
    char buffer[128];

    terminal_probe_set_transport_hooks_for_test(&hooks, &fixture);
    g_test_queue_destroy(reset_terminal_probe_hooks, NULL);

    gssize length = terminal_probe_query_response("\033[c",
                                                  TERMINAL_PROBE_DEFAULT_TIMEOUT_MS,
                                                  TERMINAL_PROBE_POLL_TIMEOUT_MS,
                                                  'c',
                                                  buffer,
                                                  sizeof(buffer));

    g_assert_cmpint(length, ==, (gssize)(sizeof(k_expected_response) - 1));
    g_assert_cmpstr(buffer, ==, k_expected_response);
    g_assert_cmpint(fixture.tcgetattr_calls, ==, 1);
    g_assert_cmpint(fixture.raw_mode_calls, ==, 1);
    g_assert_cmpint(fixture.restore_calls, ==, 1);
    g_assert_cmpint(fixture.raw_mode_optional_actions, ==, TCSANOW);
    g_assert_cmpint(fixture.restore_optional_actions, ==, TCSAFLUSH);
    g_assert_cmpint(fixture.tcflush_calls, ==, 1);
    g_assert_cmpuint(fixture.read_timeouts_ms->len, ==, sizeof(k_expected_response) - 1);
    g_assert_cmpmem(fixture.writes->str, fixture.writes->len, "\033[c", sizeof("\033[c") - 1);

    for (guint i = 0; i < fixture.read_timeouts_ms->len; i++) {
        gint timeout_ms = g_array_index(fixture.read_timeouts_ms, gint, i);
        g_assert_cmpint(timeout_ms, ==, TERMINAL_PROBE_POLL_TIMEOUT_MS);
    }

    g_string_free(fixture.writes, TRUE);
    g_array_free(fixture.read_timeouts_ms, TRUE);
}

static void test_terminal_probe_transport_bounds_timeout_and_restores_input_state(void) {
    TerminalProbeFixture fixture = {
        .stdin_is_tty = TRUE,
        .stdout_is_tty = TRUE,
        .writes = g_string_new(NULL),
        .read_timeouts_ms = g_array_new(FALSE, FALSE, sizeof(gint)),
    };
    static const TerminalProbeTransportHooks hooks = {
        .stdin_is_tty = terminal_probe_fixture_stdin_is_tty,
        .stdout_is_tty = terminal_probe_fixture_stdout_is_tty,
        .tcgetattr_fn = terminal_probe_fixture_tcgetattr,
        .tcsetattr_fn = terminal_probe_fixture_tcsetattr,
        .tcflush_fn = terminal_probe_fixture_tcflush,
        .write_fn = terminal_probe_fixture_write,
        .read_char_with_timeout_fn = terminal_probe_fixture_read_char_with_timeout,
        .monotonic_time_us_fn = terminal_probe_fixture_monotonic_time_us,
    };
    char buffer[64];

    terminal_probe_set_transport_hooks_for_test(&hooks, &fixture);
    g_test_queue_destroy(reset_terminal_probe_hooks, NULL);

    gssize length = terminal_probe_query_response("\033[5n",
                                                  15,
                                                  TERMINAL_PROBE_POLL_TIMEOUT_MS,
                                                  '\0',
                                                  buffer,
                                                  sizeof(buffer));

    g_assert_cmpint(length, ==, 0);
    g_assert_cmpstr(buffer, ==, "");
    g_assert_cmpint(fixture.tcgetattr_calls, ==, 1);
    g_assert_cmpint(fixture.raw_mode_calls, ==, 1);
    g_assert_cmpint(fixture.restore_calls, ==, 1);
    g_assert_cmpint(fixture.raw_mode_optional_actions, ==, TCSANOW);
    g_assert_cmpint(fixture.restore_optional_actions, ==, TCSAFLUSH);
    g_assert_cmpint(fixture.tcflush_calls, ==, 1);
    g_assert_cmpuint(fixture.read_timeouts_ms->len, ==, 1);
    g_assert_cmpint(g_array_index(fixture.read_timeouts_ms, gint, 0), ==, 15);
    g_assert_cmpmem(fixture.writes->str, fixture.writes->len, "\033[5n", sizeof("\033[5n") - 1);

    g_string_free(fixture.writes, TRUE);
    g_array_free(fixture.read_timeouts_ms, TRUE);
}

static void test_terminal_protocol_resolver_override_beats_signal_and_probe(void) {
    TerminalProtocolResolverInput input = {
        .has_override = TRUE,
        .override_protocol = TERMINAL_RESOLVED_PROTOCOL_ITERM2,
        .has_signal = TRUE,
        .signal_protocol = TERMINAL_RESOLVED_PROTOCOL_SIXEL,
        .has_probe = TRUE,
        .probe_protocol = TERMINAL_RESOLVED_PROTOCOL_KITTY,
    };

    TerminalProtocolDecision decision = terminal_protocol_resolve(&input);

    assert_resolver_decision(decision,
                             TERMINAL_RESOLVED_PROTOCOL_ITERM2,
                             TERMINAL_PROTOCOL_DECISION_SOURCE_OVERRIDE,
                             "override");
}

static void test_terminal_protocol_resolver_signal_beats_probe(void) {
    TerminalProtocolResolverInput input = {
        .has_signal = TRUE,
        .signal_protocol = TERMINAL_RESOLVED_PROTOCOL_KITTY,
        .has_probe = TRUE,
        .probe_protocol = TERMINAL_RESOLVED_PROTOCOL_SIXEL,
    };

    TerminalProtocolDecision decision = terminal_protocol_resolve(&input);

    assert_resolver_decision(decision,
                             TERMINAL_RESOLVED_PROTOCOL_KITTY,
                             TERMINAL_PROTOCOL_DECISION_SOURCE_SIGNAL,
                             "signal");
}

static void test_terminal_protocol_resolver_signal_used_without_override(void) {
    TerminalProtocolResolverInput input = {
        .has_signal = TRUE,
        .signal_protocol = TERMINAL_RESOLVED_PROTOCOL_SIXEL,
    };

    TerminalProtocolDecision decision = terminal_protocol_resolve(&input);

    assert_resolver_decision(decision,
                             TERMINAL_RESOLVED_PROTOCOL_SIXEL,
                             TERMINAL_PROTOCOL_DECISION_SOURCE_SIGNAL,
                             "signal");
}

static void test_terminal_protocol_resolver_probe_used_without_override_or_signal(void) {
    TerminalProtocolResolverInput input = {
        .has_probe = TRUE,
        .probe_protocol = TERMINAL_RESOLVED_PROTOCOL_ITERM2,
    };

    TerminalProtocolDecision decision = terminal_protocol_resolve(&input);

    assert_resolver_decision(decision,
                             TERMINAL_RESOLVED_PROTOCOL_ITERM2,
                             TERMINAL_PROTOCOL_DECISION_SOURCE_PROBE,
                             "probe");
}

static void test_terminal_protocol_resolver_falls_back_to_text(void) {
    TerminalProtocolResolverInput input = {0};

    TerminalProtocolDecision decision = terminal_protocol_resolve(&input);

    assert_resolver_decision(decision,
                             TERMINAL_RESOLVED_PROTOCOL_TEXT,
                             TERMINAL_PROTOCOL_DECISION_SOURCE_TEXT_FALLBACK,
                             "text-fallback");
}

static void test_terminal_protocol_resolver_direct_ssh_requires_affirmative_signal(
    TerminalProtocolEnvFixture *fixture,
    gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("SSH_CONNECTION", "client 22 server 22", TRUE));

    TerminalProtocolResolverInput input = {
        .has_probe = TRUE,
        .probe_protocol = TERMINAL_RESOLVED_PROTOCOL_SIXEL,
    };

    TerminalProtocolDecision decision = terminal_protocol_resolve(&input);

    assert_resolver_decision(decision,
                             TERMINAL_RESOLVED_PROTOCOL_TEXT,
                             TERMINAL_PROTOCOL_DECISION_SOURCE_TEXT_FALLBACK,
                             "ssh-no-affirmative-signal");
}

static void test_terminal_protocol_kitty_term_match_is_weak_candidate(TerminalProtocolEnvFixture *fixture,
                                                                      gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERM", "xterm-kitty", TRUE));

    assert_weak_candidate_name("kitty");
}

static void test_terminal_protocol_iterm2_term_program_match_is_weak_candidate(TerminalProtocolEnvFixture *fixture,
                                                                               gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERM_PROGRAM", "iterm.app", TRUE));

    assert_weak_candidate_name("iterm2");
}

static void test_terminal_protocol_contour_env_match_is_weak_candidate(TerminalProtocolEnvFixture *fixture,
                                                                        gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERMINAL_NAME", "contour", TRUE));

    assert_weak_candidate_name("contour");
}

static void test_terminal_protocol_kitty_live_helper_keeps_current_behavior(TerminalProtocolEnvFixture *fixture,
                                                                            gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERM", "xterm-kitty", TRUE));

    assert_live_support(TRUE, FALSE, FALSE);
}

static void test_terminal_protocol_iterm2_live_helper_keeps_current_behavior(TerminalProtocolEnvFixture *fixture,
                                                                             gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERM_PROGRAM", "iterm.app", TRUE));

    assert_live_support(FALSE, TRUE, TRUE);
}

static void test_terminal_protocol_contour_live_helper_keeps_current_behavior(TerminalProtocolEnvFixture *fixture,
                                                                              gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERMINAL_NAME", "contour", TRUE));

    assert_live_support(FALSE, FALSE, TRUE);
}

static void test_terminal_protocol_unknown_terminal_has_no_known_support(TerminalProtocolEnvFixture *fixture,
                                                                         gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERM", "unknown-256color", TRUE));
    g_assert_true(g_setenv("TERM_PROGRAM", "mystery-terminal", TRUE));

    g_assert_null(terminal_protocol_env_weak_candidate());
    assert_live_support(FALSE, FALSE, FALSE);
}

static void test_terminal_protocol_env_match_prefers_first_weak_candidate(TerminalProtocolEnvFixture *fixture,
                                                                          gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERM", "xterm-kitty", TRUE));
    g_assert_true(g_setenv("TERM_PROGRAM", "iTerm2", TRUE));

    assert_weak_candidate_name("kitty");
}

static void test_terminal_protocol_live_helper_precedence_keeps_current_behavior(TerminalProtocolEnvFixture *fixture,
                                                                                 gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERM", "xterm-kitty", TRUE));
    g_assert_true(g_setenv("TERM_PROGRAM", "iTerm2", TRUE));

    assert_live_support(TRUE, TRUE, TRUE);
}

static void test_terminal_protocol_env_restoration_mutation_canary(TerminalProtocolEnvFixture *fixture,
                                                                   gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERM", "pixelterm-test-term", TRUE));
    g_assert_true(g_setenv("TERM_PROGRAM", "pixelterm-test-program", TRUE));
}

static void test_terminal_protocol_env_restoration_preserves_original_values(TerminalProtocolEnvFixture *fixture,
                                                                             gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    g_assert_cmpstr(g_getenv("TERM"), ==, g_terminal_protocol_baseline_term);
    g_assert_cmpstr(g_getenv("TERM_PROGRAM"), ==, g_terminal_protocol_baseline_term_program);
}

void register_terminal_protocols_tests(void) {
    g_free(g_terminal_protocol_baseline_term);
    g_free(g_terminal_protocol_baseline_term_program);
    g_terminal_protocol_baseline_term = g_strdup(g_getenv("TERM"));
    g_terminal_protocol_baseline_term_program = g_strdup(g_getenv("TERM_PROGRAM"));

    g_test_add_func("/terminal_protocols/resolver/override_beats_signal_and_probe",
                    test_terminal_protocol_resolver_override_beats_signal_and_probe);
    g_test_add_func("/terminal_protocols/resolver/signal_beats_probe",
                    test_terminal_protocol_resolver_signal_beats_probe);
    g_test_add_func("/terminal_protocols/resolver/signal_used_without_override",
                    test_terminal_protocol_resolver_signal_used_without_override);
    g_test_add_func("/terminal_protocols/resolver/probe_used_without_override_or_signal",
                    test_terminal_protocol_resolver_probe_used_without_override_or_signal);
    g_test_add_func("/terminal_protocols/resolver/falls_back_to_text",
                    test_terminal_protocol_resolver_falls_back_to_text);
    add_terminal_protocol_test("/terminal_protocols/resolver/direct_ssh_requires_affirmative_signal",
                               test_terminal_protocol_resolver_direct_ssh_requires_affirmative_signal);

    add_terminal_protocol_test("/terminal_protocols/kitty/term_match_keeps_family_without_support_claim",
                               test_terminal_protocol_kitty_term_match_is_weak_candidate);
    add_terminal_protocol_test("/terminal_protocols/kitty/live_helper_keeps_current_behavior",
                               test_terminal_protocol_kitty_live_helper_keeps_current_behavior);
    add_terminal_protocol_test("/terminal_protocols/iterm2/term_program_match_keeps_family_without_support_claim",
                               test_terminal_protocol_iterm2_term_program_match_is_weak_candidate);
    add_terminal_protocol_test("/terminal_protocols/iterm2/live_helper_keeps_current_behavior",
                               test_terminal_protocol_iterm2_live_helper_keeps_current_behavior);
    add_terminal_protocol_test("/terminal_protocols/sixel/contour_env_match_keeps_family_without_support_claim",
                               test_terminal_protocol_contour_env_match_is_weak_candidate);
    add_terminal_protocol_test("/terminal_protocols/sixel/live_helper_keeps_current_behavior",
                               test_terminal_protocol_contour_live_helper_keeps_current_behavior);
    add_terminal_protocol_test("/terminal_protocols/unknown/no_match_disables_all_protocols",
                               test_terminal_protocol_unknown_terminal_has_no_known_support);
    add_terminal_protocol_test("/terminal_protocols/precedence/env_match_uses_first_weak_candidate",
                               test_terminal_protocol_env_match_prefers_first_weak_candidate);
    add_terminal_protocol_test("/terminal_protocols/precedence/live_helpers_keep_current_behavior",
                               test_terminal_protocol_live_helper_precedence_keeps_current_behavior);
    add_terminal_protocol_test("/terminal_protocols/restoration/mutation_canary",
                               test_terminal_protocol_env_restoration_mutation_canary);
    add_terminal_protocol_test("/terminal_protocols/restoration/preserves_original_values",
                               test_terminal_protocol_env_restoration_preserves_original_values);
    g_test_add_func("/terminal_protocols/probe/transport_reads_until_terminator_with_short_timeouts",
                    test_terminal_probe_transport_reads_until_terminator_with_short_timeouts);
    g_test_add_func("/terminal_protocols/probe/transport_bounds_timeout_and_restores_input_state",
                    test_terminal_probe_transport_bounds_timeout_and_restores_input_state);
}
