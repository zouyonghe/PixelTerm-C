#include <glib.h>

#include "terminal_protocols.h"

static const gchar * const k_terminal_env_keys[] = {
    "TERM",
    "TERM_PROGRAM",
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

static void assert_env_match_name(const gchar *expected_name) {
    const TerminalProtocolHint *hint = terminal_protocol_env_match();

    g_assert_nonnull(hint);
    g_assert_cmpstr(hint->name, ==, expected_name);
}

static void test_terminal_protocol_kitty_match_from_term(TerminalProtocolEnvFixture *fixture,
                                                         gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERM", "xterm-kitty", TRUE));

    assert_env_match_name("kitty");
    g_assert_true(terminal_env_supports_kitty());
    g_assert_false(terminal_env_supports_iterm2());
    g_assert_false(terminal_env_supports_sixel());
}

static void test_terminal_protocol_iterm2_match_from_term_program(TerminalProtocolEnvFixture *fixture,
                                                                  gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERM_PROGRAM", "iterm.app", TRUE));

    assert_env_match_name("iterm2");
    g_assert_false(terminal_env_supports_kitty());
    g_assert_true(terminal_env_supports_iterm2());
    g_assert_true(terminal_env_supports_sixel());
}

static void test_terminal_protocol_sixel_match_without_kitty_or_iterm2(TerminalProtocolEnvFixture *fixture,
                                                                       gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERMINAL_NAME", "contour", TRUE));

    assert_env_match_name("contour");
    g_assert_false(terminal_env_supports_kitty());
    g_assert_false(terminal_env_supports_iterm2());
    g_assert_true(terminal_env_supports_sixel());
}

static void test_terminal_protocol_unknown_terminal_has_no_known_support(TerminalProtocolEnvFixture *fixture,
                                                                         gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERM", "unknown-256color", TRUE));
    g_assert_true(g_setenv("TERM_PROGRAM", "mystery-terminal", TRUE));

    g_assert_null(terminal_protocol_env_match());
    g_assert_false(terminal_env_supports_kitty());
    g_assert_false(terminal_env_supports_iterm2());
    g_assert_false(terminal_env_supports_sixel());
}

static void test_terminal_protocol_env_match_prefers_first_matching_hint(TerminalProtocolEnvFixture *fixture,
                                                                         gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    clear_terminal_protocol_env();
    g_assert_true(g_setenv("TERM", "xterm-kitty", TRUE));
    g_assert_true(g_setenv("TERM_PROGRAM", "iTerm2", TRUE));

    assert_env_match_name("kitty");
    g_assert_true(terminal_env_supports_kitty());
    g_assert_true(terminal_env_supports_iterm2());
    g_assert_true(terminal_env_supports_sixel());
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

    add_terminal_protocol_test("/terminal_protocols/kitty/term_match_enables_kitty_only",
                               test_terminal_protocol_kitty_match_from_term);
    add_terminal_protocol_test("/terminal_protocols/iterm2/term_program_match_is_case_insensitive",
                               test_terminal_protocol_iterm2_match_from_term_program);
    add_terminal_protocol_test("/terminal_protocols/sixel/contour_env_match_enables_sixel_only",
                               test_terminal_protocol_sixel_match_without_kitty_or_iterm2);
    add_terminal_protocol_test("/terminal_protocols/unknown/no_match_disables_all_protocols",
                               test_terminal_protocol_unknown_terminal_has_no_known_support);
    add_terminal_protocol_test("/terminal_protocols/precedence/env_match_uses_first_matching_hint",
                               test_terminal_protocol_env_match_prefers_first_matching_hint);
    add_terminal_protocol_test("/terminal_protocols/restoration/mutation_canary",
                               test_terminal_protocol_env_restoration_mutation_canary);
    add_terminal_protocol_test("/terminal_protocols/restoration/preserves_original_values",
                               test_terminal_protocol_env_restoration_preserves_original_values);
}
