#include <glib.h>
#include <glib/gstdio.h>

#include "app_cli.h"
#include "app_config_runtime.h"
#include "input.h"
#include "process_env.h"
#include "terminal_probe.h"

#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static const gchar * const k_app_cli_env_keys[] = {
    "TERM_PROGRAM",
    "LC_TERMINAL",
    "TERMINAL_NAME",
    "TERM",
    "SSH_CONNECTION",
    "SSH_CLIENT",
    "SSH_TTY",
    "TMUX",
    "STY",
    "XDG_CONFIG_HOME",
    NULL,
};

typedef struct {
    gint unused;
} AppCliFixture;

typedef struct {
    const gchar *value;
    AppProtocolMode expected_mode;
} ProtocolCase;

typedef struct {
    const gchar *value;
    TextSymbolMode expected_mode;
} TextSymbolCase;

typedef struct {
    const gchar *value;
    ColorEnhanceMode expected_mode;
} ColorEnhanceCase;

typedef struct {
    char **argv;
    gchar **path_out;
    AppConfig *config;
    ErrorCode error;
} AppCliParseInvocation;

typedef struct {
    const gchar *option;
    const gchar *value;
    gboolean expected_value;
} BooleanAliasCase;

typedef struct {
    const gchar *response;
    gsize response_length;
    gsize response_offset;
    gint64 now_us;
    GString *writes;
    gint enable_raw_mode_calls;
    gint disable_raw_mode_calls;
} AppCliProbeFixture;

static gchar *g_app_cli_shared_config_root = NULL;

static void remove_path(gpointer data) {
    if (!data) {
        return;
    }

    g_remove((const gchar *)data);
    g_free(data);
}

static void remove_empty_dir(gpointer data) {
    if (!data) {
        return;
    }

    g_rmdir((const gchar *)data);
    g_free(data);
}

static gboolean remove_tree_recursive(const gchar *path) {
    GError *error = NULL;
    GDir *dir = g_dir_open(path, 0, &error);
    if (!dir) {
        if (error) {
            g_error_free(error);
        }
        return g_remove(path) == 0;
    }

    const gchar *name = NULL;
    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *child = g_build_filename(path, name, NULL);
        remove_tree_recursive(child);
        g_free(child);
    }

    g_dir_close(dir);
    return g_rmdir(path) == 0;
}

static void remove_tree(gpointer data) {
    if (!data) {
        return;
    }

    remove_tree_recursive((const gchar *)data);
    g_free(data);
}

static void app_cli_cleanup_shared_config_root(void) {
    if (!g_app_cli_shared_config_root) {
        return;
    }

    gchar *root = g_steal_pointer(&g_app_cli_shared_config_root);
    remove_tree(root);
}

static gboolean app_cli_probe_fixture_stdin_is_tty(gpointer user_data) {
    (void)user_data;
    return TRUE;
}

static gboolean app_cli_probe_fixture_stdout_is_tty(gpointer user_data) {
    (void)user_data;
    return TRUE;
}

static int app_cli_probe_fixture_tcgetattr(struct termios *termios_state, gpointer user_data) {
    (void)user_data;

    memset(termios_state, 0, sizeof(*termios_state));
    termios_state->c_lflag = ICANON | ECHO | IEXTEN;
    termios_state->c_iflag = BRKINT | ICRNL | INPCK | ISTRIP | IXON;
    termios_state->c_cc[VMIN] = 1;
    termios_state->c_cc[VTIME] = 0;
    return 0;
}

static int app_cli_probe_fixture_tcsetattr(gint optional_actions,
                                           const struct termios *termios_state,
                                           gpointer user_data) {
    (void)optional_actions;
    (void)termios_state;
    (void)user_data;
    return 0;
}

static int app_cli_probe_fixture_tcflush(gint queue_selector, gpointer user_data) {
    (void)queue_selector;
    (void)user_data;
    return 0;
}

static ssize_t app_cli_probe_fixture_write(const char *buf, size_t len, gpointer user_data) {
    AppCliProbeFixture *fixture = user_data;

    g_assert_nonnull(buf);
    g_string_append_len(fixture->writes, buf, len);
    return (ssize_t)len;
}

static gint app_cli_probe_fixture_read_char_with_timeout(gint timeout_ms, gpointer user_data) {
    AppCliProbeFixture *fixture = user_data;

    if (!fixture->response || fixture->response_offset >= fixture->response_length) {
        fixture->now_us += (gint64)timeout_ms * 1000;
        return 0;
    }

    fixture->now_us += 1000;
    return (guchar)fixture->response[fixture->response_offset++];
}

static gint64 app_cli_probe_fixture_monotonic_time_us(gpointer user_data) {
    AppCliProbeFixture *fixture = user_data;
    return fixture->now_us;
}

static void reset_app_cli_probe_hooks(gpointer user_data) {
    (void)user_data;
    terminal_probe_reset_transport_hooks_for_test();
    input_reset_raw_mode_observers_for_test();
}

static void queue_terminal_protocol_env_restore_and_clear(void) {
    static const gchar * const keys[] = {
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

    for (gsize i = 0; keys[i] != NULL; i++) {
        pixelterm_env_unset_for_test(keys[i]);
    }

    pixelterm_env_set_for_test("TERM", "pixelterm-test-unknown");
}

static void app_cli_probe_fixture_enable_raw_mode(InputHandler *handler, gpointer user_data) {
    (void)handler;
    AppCliProbeFixture *fixture = user_data;
    fixture->enable_raw_mode_calls++;
}

static void app_cli_probe_fixture_disable_raw_mode(InputHandler *handler, gpointer user_data) {
    (void)handler;
    AppCliProbeFixture *fixture = user_data;
    fixture->disable_raw_mode_calls++;
}

static void assert_app_cli_probe_resolves_to_kitty(const gchar *response,
                                                   const gchar *env_key,
                                                   const gchar *env_value) {
    static const TerminalProbeTransportHooks hooks = {
        .stdin_is_tty = app_cli_probe_fixture_stdin_is_tty,
        .stdout_is_tty = app_cli_probe_fixture_stdout_is_tty,
        .tcgetattr_fn = app_cli_probe_fixture_tcgetattr,
        .tcsetattr_fn = app_cli_probe_fixture_tcsetattr,
        .tcflush_fn = app_cli_probe_fixture_tcflush,
        .write_fn = app_cli_probe_fixture_write,
        .read_char_with_timeout_fn = app_cli_probe_fixture_read_char_with_timeout,
        .monotonic_time_us_fn = app_cli_probe_fixture_monotonic_time_us,
    };
    AppCliProbeFixture probe_fixture = {
        .response = response,
        .response_length = strlen(response),
        .writes = g_string_new(NULL),
    };
    AppConfig config;

    queue_terminal_protocol_env_restore_and_clear();
    if (env_key && env_value) {
        pixelterm_env_set_for_test(env_key, env_value);
    }

    terminal_probe_set_transport_hooks_for_test(&hooks, &probe_fixture);
    g_test_queue_destroy(reset_app_cli_probe_hooks, NULL);
    input_set_enable_raw_mode_observer_for_test(app_cli_probe_fixture_enable_raw_mode,
                                                &probe_fixture);
    input_set_disable_raw_mode_observer_for_test(app_cli_probe_fixture_disable_raw_mode,
                                                 &probe_fixture);

    app_config_init(&config);
    app_config_resolve_protocol(&config);

    g_assert_false(config.force_text);
    g_assert_false(config.force_sixel);
    g_assert_true(config.force_kitty);
    g_assert_false(config.force_iterm2);
    g_assert_cmpstr(probe_fixture.writes->str, ==, "\033[>q\033[5n");
    g_assert_cmpint(probe_fixture.enable_raw_mode_calls, ==, 0);
    g_assert_cmpint(probe_fixture.disable_raw_mode_calls, ==, 0);

    g_string_free(probe_fixture.writes, TRUE);
}

static const gchar *app_cli_shared_config_root(void) {
    if (g_app_cli_shared_config_root) {
        return g_app_cli_shared_config_root;
    }

    GError *error = NULL;
    g_app_cli_shared_config_root = g_dir_make_tmp("pixelterm-app-cli-XXXXXX", &error);
    if (!g_app_cli_shared_config_root) {
        g_error("Failed to create shared app_cli config root: %s",
                error ? error->message : "unknown error");
    }

    if (error) {
        g_error_free(error);
    }

    atexit(app_cli_cleanup_shared_config_root);
    return g_app_cli_shared_config_root;
}

static void clear_app_cli_env(void) {
    for (gsize i = 0; k_app_cli_env_keys[i]; i++) {
        pixelterm_env_unset_for_test(k_app_cli_env_keys[i]);
    }
}

static void app_cli_fixture_set_up(AppCliFixture *fixture, gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    pixelterm_env_reset_for_test();
    clear_app_cli_env();
    pixelterm_env_set_for_test("XDG_CONFIG_HOME", app_cli_shared_config_root());
}

static void app_cli_fixture_tear_down(AppCliFixture *fixture, gconstpointer user_data) {
    (void)fixture;
    (void)user_data;
    pixelterm_env_reset_for_test();
}

static void add_app_cli_test(const gchar *path,
                             void (*test_func)(AppCliFixture *fixture, gconstpointer user_data)) {
    g_test_add(path, AppCliFixture, NULL,
               app_cli_fixture_set_up,
               test_func,
               app_cli_fixture_tear_down);
}

static ErrorCode parse_cli_args(char *argv[], gchar **path_out, AppConfig *config) {
    gint argc = 0;
    while (argv[argc] != NULL) {
        argc++;
    }

    *path_out = NULL;
    return app_parse_arguments(argc, argv, path_out, config);
}

static void invoke_parse_cli_args(gpointer data) {
    AppCliParseInvocation *invocation = data;

    invocation->error = parse_cli_args(invocation->argv,
                                       invocation->path_out,
                                       invocation->config);
}

static gchar *capture_stderr(void (*func)(gpointer), gpointer data) {
    GError *error = NULL;
    gchar *capture_path = NULL;
    gint capture_fd = g_file_open_tmp("pixelterm-app-cli-stderr-XXXXXX", &capture_path, &error);
    if (capture_fd < 0) {
        g_error("Failed to create stderr capture file: %s",
                error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
        error = NULL;
    }

    fflush(stderr);
    gint saved_fd = dup(STDERR_FILENO);
    if (saved_fd < 0) {
        close(capture_fd);
        g_error("Failed to duplicate stderr");
    }
    if (dup2(capture_fd, STDERR_FILENO) < 0) {
        close(saved_fd);
        close(capture_fd);
        g_error("Failed to redirect stderr");
    }
    close(capture_fd);

    func(data);

    fflush(stderr);
    if (dup2(saved_fd, STDERR_FILENO) < 0) {
        close(saved_fd);
        g_error("Failed to restore stderr");
    }
    close(saved_fd);

    gchar *captured = NULL;
    if (!g_file_get_contents(capture_path, &captured, NULL, &error)) {
        g_error("Failed to read stderr capture: %s",
                error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
    }

    g_remove(capture_path);
    g_free(capture_path);
    return captured;
}

static gchar *capture_stdout(void (*func)(gpointer), gpointer data) {
    GError *error = NULL;
    gchar *capture_path = NULL;
    gint capture_fd = g_file_open_tmp("pixelterm-app-cli-stdout-XXXXXX", &capture_path, &error);
    if (capture_fd < 0) {
        g_error("Failed to create stdout capture file: %s",
                error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
        error = NULL;
    }

    fflush(stdout);
    gint saved_fd = dup(STDOUT_FILENO);
    if (saved_fd < 0) {
        close(capture_fd);
        g_error("Failed to duplicate stdout");
    }
    if (dup2(capture_fd, STDOUT_FILENO) < 0) {
        close(saved_fd);
        close(capture_fd);
        g_error("Failed to redirect stdout");
    }
    close(capture_fd);

    func(data);

    fflush(stdout);
    if (dup2(saved_fd, STDOUT_FILENO) < 0) {
        close(saved_fd);
        g_error("Failed to restore stdout");
    }
    close(saved_fd);

    gchar *captured = NULL;
    if (!g_file_get_contents(capture_path, &captured, NULL, &error)) {
        g_error("Failed to read stdout capture: %s",
                error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
    }

    g_remove(capture_path);
    g_free(capture_path);
    return captured;
}

static gchar *write_temp_config_file(const gchar *contents) {
    GError *error = NULL;
    gchar *path = NULL;
    gint fd = g_file_open_tmp("pixelterm-app-cli-config-XXXXXX.ini", &path, &error);
    if (fd < 0 || !path) {
        g_error("Failed to create temp config file: %s", error ? error->message : "unknown error");
    }

    if (error) {
        g_error_free(error);
        error = NULL;
    }

    g_close(fd, &error);
    if (error) {
        g_error("Failed to close temp config file: %s", error->message);
    }

    if (error) {
        g_error_free(error);
        error = NULL;
    }

    if (!g_file_set_contents(path, contents, -1, &error)) {
        g_error("Failed to write temp config file: %s", error ? error->message : "unknown error");
    }

    if (error) {
        g_error_free(error);
    }

    g_test_queue_destroy(remove_path, g_strdup(path));
    return path;
}

static gchar *make_temp_config_root(void) {
    GError *error = NULL;
    gchar *path = g_dir_make_tmp("pixelterm-app-cli-root-XXXXXX", &error);
    if (!path) {
        g_error("Failed to create temp config root: %s", error ? error->message : "unknown error");
    }

    if (error) {
        g_error_free(error);
    }

    g_test_queue_destroy(remove_tree, g_strdup(path));
    return path;
}

static gchar *write_default_config_file_at_root(const gchar *config_root, const gchar *contents) {
    gchar *config_dir = g_build_filename(config_root, "pixelterm", NULL);
    if (g_mkdir_with_parents(config_dir, 0700) != 0) {
        g_error("Failed to create default config directory: %s", config_dir);
    }

    gchar *path = g_build_filename(config_dir, "config.ini", NULL);
    GError *error = NULL;
    if (!g_file_set_contents(path, contents, -1, &error)) {
        g_error("Failed to write default config file: %s", error ? error->message : "unknown error");
    }

    if (error) {
        g_error_free(error);
    }

    g_test_queue_destroy(remove_path, g_strdup(path));
    g_test_queue_destroy(remove_empty_dir, g_strdup(config_dir));
    g_free(config_dir);
    return path;
}

static gchar *write_default_config_file(const gchar *contents) {
    return write_default_config_file_at_root(app_cli_shared_config_root(), contents);
}

static void test_cli_boolean_aliases_parse_for_preload_and_alt_screen(AppCliFixture *fixture,
                                                                      gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    static const BooleanAliasCase cases[] = {
        {"--preload", "on", TRUE},
        {"--preload", "off", FALSE},
        {"--preload", "1", TRUE},
        {"--preload", "0", FALSE},
        {"--alt-screen", "yes", TRUE},
        {"--alt-screen", "no", FALSE},
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        AppConfig config;
        gchar *path = NULL;
        app_config_init(&config);

        char *argv[] = {
            "pixelterm",
            (char *)cases[i].option,
            (char *)cases[i].value,
            NULL,
        };

        g_assert_cmpint(parse_cli_args(argv, &path, &config), ==, ERROR_NONE);
        g_assert_null(path);

        if (strcmp(cases[i].option, "--preload") == 0) {
            g_assert_cmpint(config.preload_enabled, ==, cases[i].expected_value);
        } else {
            g_assert_cmpint(config.alt_screen_enabled, ==, cases[i].expected_value);
        }

        g_free(path);
    }
}

static void test_cli_invalid_boolean_values_return_error(AppCliFixture *fixture,
                                                         gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    static const BooleanAliasCase cases[] = {
        {"--preload", "maybe", FALSE},
        {"--alt-screen", "2", FALSE},
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        AppConfig config;
        gchar *path = NULL;
        AppCliParseInvocation invocation = {0};
        app_config_init(&config);

        char *argv[] = {
            "pixelterm",
            (char *)cases[i].option,
            (char *)cases[i].value,
            NULL,
        };

        invocation.argv = argv;
        invocation.path_out = &path;
        invocation.config = &config;

        gchar *stderr_output = capture_stderr(invoke_parse_cli_args, &invocation);

        g_assert_cmpint(invocation.error, ==, ERROR_INVALID_ARGS);
        g_assert_null(path);
        g_assert_nonnull(stderr_output);
        if (strcmp(cases[i].option, "--preload") == 0) {
            g_assert_cmpstr(stderr_output,
                            ==,
                            "Invalid --preload value: maybe (expected true/false)\n");
        } else {
            g_assert_cmpstr(stderr_output,
                            ==,
                            "Invalid --alt-screen value: 2 (expected true/false)\n");
        }
        g_free(stderr_output);
        g_free(path);
    }
}

static void test_cli_protocol_argument_parses_supported_modes(AppCliFixture *fixture,
                                                              gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    static const ProtocolCase cases[] = {
        {"auto", APP_PROTOCOL_AUTO},
        {"TEXT", APP_PROTOCOL_TEXT},
        {"Sixel", APP_PROTOCOL_SIXEL},
        {"kitty", APP_PROTOCOL_KITTY},
        {"iTerm2", APP_PROTOCOL_ITERM2},
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        AppConfig config;
        gchar *path = NULL;
        app_config_init(&config);

        char *argv[] = {
            "pixelterm",
            "--protocol",
            (char *)cases[i].value,
            NULL,
        };

        g_assert_cmpint(parse_cli_args(argv, &path, &config), ==, ERROR_NONE);
        g_assert_null(path);
        g_assert_cmpint(config.protocol_mode, ==, cases[i].expected_mode);
        g_free(path);
    }
}

static void test_cli_text_symbols_argument_parses_supported_modes(AppCliFixture *fixture,
                                                                  gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    static const TextSymbolCase cases[] = {
        {"auto", TEXT_SYMBOL_MODE_AUTO},
        {"HALF", TEXT_SYMBOL_MODE_HALF},
        {"quarter", TEXT_SYMBOL_MODE_QUARTER},
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        AppConfig config;
        gchar *path = NULL;
        app_config_init(&config);

        char *argv[] = {
            "pixelterm",
            "--text-symbols",
            (char *)cases[i].value,
            NULL,
        };

        g_assert_cmpint(parse_cli_args(argv, &path, &config), ==, ERROR_NONE);
        g_assert_null(path);
        g_assert_cmpint(config.text_symbol_mode, ==, cases[i].expected_mode);
        g_free(path);
    }
}

static void test_cli_help_mentions_xdg_and_home_config_defaults(AppCliFixture *fixture,
                                                                gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    AppConfig config;
    gchar *path = NULL;
    AppCliParseInvocation invocation = {0};
    app_config_init(&config);

    char *argv[] = {
        "pixelterm",
        "--help",
        NULL,
    };

    invocation.argv = argv;
    invocation.path_out = &path;
    invocation.config = &config;

    gchar *stdout_output = capture_stdout(invoke_parse_cli_args, &invocation);

    g_assert_cmpint(invocation.error, ==, ERROR_HELP_EXIT);
    g_assert_null(path);
    g_assert_nonnull(stdout_output);
    g_assert_nonnull(g_strstr_len(stdout_output,
                                  -1,
                                  "$XDG_CONFIG_HOME/pixelterm/config.ini, fallback: $HOME/.config/pixelterm/config.ini"));

    g_free(stdout_output);
    g_free(path);
}

static void test_cli_apply_runtime_copies_app_owned_config_fields(AppCliFixture *fixture,
                                                                  gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    AppConfig config;
    PixelTermApp app = {0};

    app_config_init(&config);
    config.preload_enabled = FALSE;
    config.dither_enabled = TRUE;
    config.clear_workaround_enabled = TRUE;
    config.work_factor = 4;
    config.gamma = 1.75;
    config.color_enhance = COLOR_ENHANCE_VIVID;
    config.force_text = TRUE;
    config.force_sixel = FALSE;
    config.force_kitty = TRUE;
    config.force_iterm2 = FALSE;
    config.text_symbol_mode = TEXT_SYMBOL_MODE_QUARTER;

    app.running = TRUE;
    app.video_scale = 2.0;

    app_config_apply_runtime(&app, &config);

    g_assert_false(app.preload_enabled);
    g_assert_true(app.dither_enabled);
    g_assert_true(app.clear_workaround_enabled);
    g_assert_cmpint(app.render_work_factor, ==, 4);
    g_assert_cmpfloat_with_epsilon(app.gamma, 1.75, 0.0001);
    g_assert_cmpint(app.color_enhance, ==, COLOR_ENHANCE_VIVID);
    g_assert_true(app.force_text);
    g_assert_false(app.force_sixel);
    g_assert_true(app.force_kitty);
    g_assert_false(app.force_iterm2);
    g_assert_cmpint(app.text_symbol_mode, ==, TEXT_SYMBOL_MODE_QUARTER);
    g_assert_true(app.running);
    g_assert_cmpfloat_with_epsilon(app.video_scale, 2.0, 0.0001);
}

static void test_cli_default_config_applies_terminal_specific_precedence(AppCliFixture *fixture,
                                                                         gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    gchar *default_config_path = write_default_config_file(
        "[default]\n"
        "preload=false\n"
        "alt_screen=true\n"
        "clear_workaround=false\n"
        "work_factor=3\n"
        "protocol=text\n"
        "text_symbols=quarter\n"
        "color_enhance=off\n"
        "gamma=1.25\n"
        "\n"
        "[WarpTerminal]\n"
        "alt_screen=false\n"
        "clear_workaround=true\n"
        "protocol=kitty\n"
        "text_symbols=half\n"
        "color_enhance=vivid\n");

    pixelterm_env_set_for_test("TERM_PROGRAM", "WarpTerminal");

    AppConfig config;
    gchar *path = NULL;
    app_config_init(&config);

    char *argv[] = {"pixelterm", NULL};

    g_assert_cmpint(parse_cli_args(argv, &path, &config), ==, ERROR_NONE);
    g_assert_null(path);
    g_assert_false(config.preload_enabled);
    g_assert_false(config.alt_screen_enabled);
    g_assert_true(config.clear_workaround_enabled);
    g_assert_cmpint(config.work_factor, ==, 3);
    g_assert_cmpint(config.protocol_mode, ==, APP_PROTOCOL_KITTY);
    g_assert_cmpint(config.text_symbol_mode, ==, TEXT_SYMBOL_MODE_HALF);
    g_assert_cmpint(config.color_enhance, ==, COLOR_ENHANCE_VIVID);
    g_assert_true(config.gamma_set);
    g_assert_cmpfloat_with_epsilon(config.gamma, 1.25, 0.0001);
    g_free(path);
    g_free(default_config_path);
}

static void test_cli_default_config_respects_runtime_xdg_after_glib_cache_prime(void) {
    pixelterm_env_reset_for_test();
    clear_app_cli_env();
    (void)g_get_user_config_dir();

    gchar *config_root = make_temp_config_root();
    gchar *default_config_path = write_default_config_file_at_root(
        config_root,
        "[default]\n"
        "preload=false\n"
        "alt_screen=true\n"
        "clear_workaround=false\n"
        "work_factor=3\n"
        "protocol=text\n"
        "text_symbols=quarter\n"
        "color_enhance=off\n"
        "gamma=1.25\n"
        "\n"
        "[WarpTerminal]\n"
        "alt_screen=false\n"
        "clear_workaround=true\n"
        "protocol=kitty\n"
        "text_symbols=half\n"
        "color_enhance=vivid\n");

    pixelterm_env_set_for_test("XDG_CONFIG_HOME", config_root);
    pixelterm_env_set_for_test("TERM_PROGRAM", "WarpTerminal");

    AppConfig config;
    gchar *path = NULL;
    app_config_init(&config);

    char *argv[] = {"pixelterm", NULL};

    g_assert_cmpint(parse_cli_args(argv, &path, &config), ==, ERROR_NONE);
    g_assert_null(path);
    g_assert_false(config.preload_enabled);
    g_assert_false(config.alt_screen_enabled);
    g_assert_true(config.clear_workaround_enabled);
    g_assert_cmpint(config.work_factor, ==, 3);
    g_assert_cmpint(config.protocol_mode, ==, APP_PROTOCOL_KITTY);
    g_assert_cmpint(config.text_symbol_mode, ==, TEXT_SYMBOL_MODE_HALF);
    g_assert_cmpint(config.color_enhance, ==, COLOR_ENHANCE_VIVID);
    g_assert_true(config.gamma_set);
    g_assert_cmpfloat_with_epsilon(config.gamma, 1.25, 0.0001);

    g_free(path);
    g_free(default_config_path);
    g_free(config_root);
    pixelterm_env_reset_for_test();
}

static void test_cli_flags_override_loaded_config_values(AppCliFixture *fixture,
                                                         gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    gchar *config_path = write_temp_config_file(
        "[default]\n"
        "preload=false\n"
        "dither=false\n"
        "alt_screen=false\n"
        "clear_workaround=false\n"
        "work_factor=2\n"
        "protocol=text\n"
        "text_symbols=half\n"
        "color_enhance=off\n"
        "gamma=1.5\n");

    AppConfig config;
    gchar *path = NULL;
    app_config_init(&config);

    char *argv[] = {
        "pixelterm",
        "--config",
        config_path,
        "--preload",
        "on",
        "--dither",
        "--alt-screen",
        "yes",
        "--clear-workaround",
        "--work-factor",
        "8",
        "--protocol",
        "sixel",
        "--text-symbols",
        "quarter",
        "--color-enhance",
        "vivid",
        "--gamma",
        "2.5",
        "sample.png",
        NULL,
    };

    g_assert_cmpint(parse_cli_args(argv, &path, &config), ==, ERROR_NONE);
    g_assert_cmpstr(path, ==, "sample.png");
    g_assert_true(config.preload_enabled);
    g_assert_true(config.dither_enabled);
    g_assert_true(config.alt_screen_enabled);
    g_assert_true(config.clear_workaround_enabled);
    g_assert_cmpint(config.work_factor, ==, 8);
    g_assert_cmpint(config.protocol_mode, ==, APP_PROTOCOL_SIXEL);
    g_assert_cmpint(config.text_symbol_mode, ==, TEXT_SYMBOL_MODE_QUARTER);
    g_assert_cmpint(config.color_enhance, ==, COLOR_ENHANCE_VIVID);
    g_assert_true(config.gamma_set);
    g_assert_cmpfloat_with_epsilon(config.gamma, 2.5, 0.0001);
    g_free(path);
    g_free(config_path);
}

static void test_cli_color_enhance_argument_parses_supported_modes(AppCliFixture *fixture,
                                                                   gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    const ColorEnhanceCase cases[] = {
        {"off", COLOR_ENHANCE_OFF},
        {"vivid", COLOR_ENHANCE_VIVID},
        {"VIVID", COLOR_ENHANCE_VIVID},
    };

    for (gsize i = 0; i < G_N_ELEMENTS(cases); i++) {
        AppConfig config;
        gchar *path = NULL;
        app_config_init(&config);

        char *argv[] = {"pixelterm", "--color-enhance", (char *)cases[i].value, "sample.png", NULL};

        g_assert_cmpint(parse_cli_args(argv, &path, &config), ==, ERROR_NONE);
        g_assert_cmpint(config.color_enhance, ==, cases[i].expected_mode);
        g_assert_cmpstr(path, ==, "sample.png");
        g_free(path);
    }
}

static void test_cli_color_enhance_argument_rejects_unknown_mode(AppCliFixture *fixture,
                                                                 gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    AppConfig config;
    gchar *path = NULL;
    app_config_init(&config);

    char *argv[] = {"pixelterm", "--color-enhance", "neon", NULL};

    g_assert_cmpint(parse_cli_args(argv, &path, &config), ==, ERROR_INVALID_ARGS);
    g_assert_null(path);
}

static void test_cli_double_dash_preserves_positional_config_like_path(AppCliFixture *fixture,
                                                                       gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    AppConfig config;
    gchar *path = NULL;
    app_config_init(&config);

    char *argv[] = {
        "pixelterm",
        "--",
        "--config=gallery.txt",
        NULL,
    };

    g_assert_cmpint(parse_cli_args(argv, &path, &config), ==, ERROR_NONE);
    g_assert_cmpstr(path, ==, "--config=gallery.txt");
    g_assert_true(config.preload_enabled);
    g_assert_true(config.alt_screen_enabled);
    g_assert_cmpint(config.protocol_mode, ==, APP_PROTOCOL_AUTO);
    g_free(path);
}

static void test_cli_protocol_resolution_auto_probe_avoids_raw_mode_transitions(AppCliFixture *fixture,
                                                                                 gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    static const gchar k_sixel_response[] = "\033[?4;1;2c";
    static const TerminalProbeTransportHooks hooks = {
        .stdin_is_tty = app_cli_probe_fixture_stdin_is_tty,
        .stdout_is_tty = app_cli_probe_fixture_stdout_is_tty,
        .tcgetattr_fn = app_cli_probe_fixture_tcgetattr,
        .tcsetattr_fn = app_cli_probe_fixture_tcsetattr,
        .tcflush_fn = app_cli_probe_fixture_tcflush,
        .write_fn = app_cli_probe_fixture_write,
        .read_char_with_timeout_fn = app_cli_probe_fixture_read_char_with_timeout,
        .monotonic_time_us_fn = app_cli_probe_fixture_monotonic_time_us,
    };
    AppCliProbeFixture probe_fixture = {
        .response = k_sixel_response,
        .response_length = sizeof(k_sixel_response) - 1,
        .writes = g_string_new(NULL),
    };
    AppConfig config;

    queue_terminal_protocol_env_restore_and_clear();

    terminal_probe_set_transport_hooks_for_test(&hooks, &probe_fixture);
    g_test_queue_destroy(reset_app_cli_probe_hooks, NULL);
    input_set_enable_raw_mode_observer_for_test(app_cli_probe_fixture_enable_raw_mode,
                                                &probe_fixture);
    input_set_disable_raw_mode_observer_for_test(app_cli_probe_fixture_disable_raw_mode,
                                                 &probe_fixture);

    app_config_init(&config);
    app_config_resolve_protocol(&config);

    g_assert_true(config.force_sixel);
    g_assert_false(config.force_iterm2);
    g_assert_false(config.force_kitty);
    g_assert_cmpstr(probe_fixture.writes->str, ==, "\033[c");
    g_assert_cmpint(probe_fixture.enable_raw_mode_calls, ==, 0);
    g_assert_cmpint(probe_fixture.disable_raw_mode_calls, ==, 0);

    g_string_free(probe_fixture.writes, TRUE);
}

static void test_cli_protocol_resolution_auto_prefers_affirmative_signal_before_generic_probe_order(
    AppCliFixture *fixture,
    gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    static const gchar k_kitty_response[] = "kitty";
    static const TerminalProbeTransportHooks hooks = {
        .stdin_is_tty = app_cli_probe_fixture_stdin_is_tty,
        .stdout_is_tty = app_cli_probe_fixture_stdout_is_tty,
        .tcgetattr_fn = app_cli_probe_fixture_tcgetattr,
        .tcsetattr_fn = app_cli_probe_fixture_tcsetattr,
        .tcflush_fn = app_cli_probe_fixture_tcflush,
        .write_fn = app_cli_probe_fixture_write,
        .read_char_with_timeout_fn = app_cli_probe_fixture_read_char_with_timeout,
        .monotonic_time_us_fn = app_cli_probe_fixture_monotonic_time_us,
    };
    AppCliProbeFixture probe_fixture = {
        .response = k_kitty_response,
        .response_length = sizeof(k_kitty_response) - 1,
        .writes = g_string_new(NULL),
    };
    AppConfig config;

    queue_terminal_protocol_env_restore_and_clear();
    pixelterm_env_set_for_test("TERM", "xterm-kitty");

    terminal_probe_set_transport_hooks_for_test(&hooks, &probe_fixture);
    g_test_queue_destroy(reset_app_cli_probe_hooks, NULL);
    input_set_enable_raw_mode_observer_for_test(app_cli_probe_fixture_enable_raw_mode,
                                                &probe_fixture);
    input_set_disable_raw_mode_observer_for_test(app_cli_probe_fixture_disable_raw_mode,
                                                 &probe_fixture);

    app_config_init(&config);
    app_config_resolve_protocol(&config);

    g_assert_false(config.force_text);
    g_assert_false(config.force_sixel);
    g_assert_true(config.force_kitty);
    g_assert_false(config.force_iterm2);
    g_assert_cmpstr(probe_fixture.writes->str, ==, "\033[>q\033[5n");
    g_assert_cmpint(probe_fixture.enable_raw_mode_calls, ==, 0);
    g_assert_cmpint(probe_fixture.disable_raw_mode_calls, ==, 0);

    g_string_free(probe_fixture.writes, TRUE);
}

static void test_cli_protocol_resolution_auto_accepts_libghostty_xtversion_as_kitty_signal(
    AppCliFixture *fixture,
    gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    assert_app_cli_probe_resolves_to_kitty("\033P>|libghostty\033\\", "TERM_PROGRAM", "ghostty");
}

static void test_cli_protocol_resolution_auto_accepts_ghostty_xtversion_as_kitty_signal(
    AppCliFixture *fixture,
    gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    assert_app_cli_probe_resolves_to_kitty("\033P>|Ghostty 1.2.3\033\\", "TERM", "xterm-ghostty");
}

static void test_cli_protocol_resolution_auto_multi_protocol_hint_keeps_local_sixel_first_order(
    AppCliFixture *fixture,
    gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    static const gchar k_sixel_response[] = "\033[?4;1;2c";
    static const TerminalProbeTransportHooks hooks = {
        .stdin_is_tty = app_cli_probe_fixture_stdin_is_tty,
        .stdout_is_tty = app_cli_probe_fixture_stdout_is_tty,
        .tcgetattr_fn = app_cli_probe_fixture_tcgetattr,
        .tcsetattr_fn = app_cli_probe_fixture_tcsetattr,
        .tcflush_fn = app_cli_probe_fixture_tcflush,
        .write_fn = app_cli_probe_fixture_write,
        .read_char_with_timeout_fn = app_cli_probe_fixture_read_char_with_timeout,
        .monotonic_time_us_fn = app_cli_probe_fixture_monotonic_time_us,
    };
    AppCliProbeFixture probe_fixture = {
        .response = k_sixel_response,
        .response_length = sizeof(k_sixel_response) - 1,
        .writes = g_string_new(NULL),
    };
    AppConfig config;

    queue_terminal_protocol_env_restore_and_clear();
    pixelterm_env_set_for_test("TERM_PROGRAM", "WezTerm");

    terminal_probe_set_transport_hooks_for_test(&hooks, &probe_fixture);
    g_test_queue_destroy(reset_app_cli_probe_hooks, NULL);
    input_set_enable_raw_mode_observer_for_test(app_cli_probe_fixture_enable_raw_mode,
                                                &probe_fixture);
    input_set_disable_raw_mode_observer_for_test(app_cli_probe_fixture_disable_raw_mode,
                                                 &probe_fixture);

    app_config_init(&config);
    app_config_resolve_protocol(&config);

    g_assert_false(config.force_text);
    g_assert_true(config.force_sixel);
    g_assert_false(config.force_kitty);
    g_assert_false(config.force_iterm2);
    g_assert_cmpstr(probe_fixture.writes->str, ==, "\033[c");
    g_assert_cmpint(probe_fixture.enable_raw_mode_calls, ==, 0);
    g_assert_cmpint(probe_fixture.disable_raw_mode_calls, ==, 0);

    g_string_free(probe_fixture.writes, TRUE);
}

static void test_cli_protocol_resolution_auto_prefers_warp_hint_without_affirmative_probe(
    AppCliFixture *fixture,
    gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    static const TerminalProbeTransportHooks hooks = {
        .stdin_is_tty = app_cli_probe_fixture_stdin_is_tty,
        .stdout_is_tty = app_cli_probe_fixture_stdout_is_tty,
        .tcgetattr_fn = app_cli_probe_fixture_tcgetattr,
        .tcsetattr_fn = app_cli_probe_fixture_tcsetattr,
        .tcflush_fn = app_cli_probe_fixture_tcflush,
        .write_fn = app_cli_probe_fixture_write,
        .read_char_with_timeout_fn = app_cli_probe_fixture_read_char_with_timeout,
        .monotonic_time_us_fn = app_cli_probe_fixture_monotonic_time_us,
    };
    AppCliProbeFixture probe_fixture = {
        .writes = g_string_new(NULL),
    };
    AppConfig config;

    queue_terminal_protocol_env_restore_and_clear();
    pixelterm_env_set_for_test("TERM_PROGRAM", "WarpTerminal");

    terminal_probe_set_transport_hooks_for_test(&hooks, &probe_fixture);
    g_test_queue_destroy(reset_app_cli_probe_hooks, NULL);
    input_set_enable_raw_mode_observer_for_test(app_cli_probe_fixture_enable_raw_mode,
                                                &probe_fixture);
    input_set_disable_raw_mode_observer_for_test(app_cli_probe_fixture_disable_raw_mode,
                                                 &probe_fixture);

    app_config_init(&config);
    app_config_resolve_protocol(&config);

    g_assert_false(config.force_text);
    g_assert_false(config.force_sixel);
    g_assert_true(config.force_kitty);
    g_assert_false(config.force_iterm2);
    g_assert_cmpstr(probe_fixture.writes->str, ==, "\033[>q\033[5n");
    g_assert_cmpint(probe_fixture.enable_raw_mode_calls, ==, 0);
    g_assert_cmpint(probe_fixture.disable_raw_mode_calls, ==, 0);

    g_string_free(probe_fixture.writes, TRUE);
}

static void test_cli_protocol_resolution_auto_uses_text_when_probe_is_inconclusive(
    AppCliFixture *fixture,
    gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    static const TerminalProbeTransportHooks hooks = {
        .stdin_is_tty = app_cli_probe_fixture_stdin_is_tty,
        .stdout_is_tty = app_cli_probe_fixture_stdout_is_tty,
        .tcgetattr_fn = app_cli_probe_fixture_tcgetattr,
        .tcsetattr_fn = app_cli_probe_fixture_tcsetattr,
        .tcflush_fn = app_cli_probe_fixture_tcflush,
        .write_fn = app_cli_probe_fixture_write,
        .read_char_with_timeout_fn = app_cli_probe_fixture_read_char_with_timeout,
        .monotonic_time_us_fn = app_cli_probe_fixture_monotonic_time_us,
    };
    AppCliProbeFixture probe_fixture = {
        .writes = g_string_new(NULL),
    };
    AppConfig config;

    queue_terminal_protocol_env_restore_and_clear();

    terminal_probe_set_transport_hooks_for_test(&hooks, &probe_fixture);
    g_test_queue_destroy(reset_app_cli_probe_hooks, NULL);
    input_set_enable_raw_mode_observer_for_test(app_cli_probe_fixture_enable_raw_mode,
                                                &probe_fixture);
    input_set_disable_raw_mode_observer_for_test(app_cli_probe_fixture_disable_raw_mode,
                                                 &probe_fixture);

    app_config_init(&config);
    app_config_resolve_protocol(&config);

    g_assert_true(config.force_text);
    g_assert_false(config.force_sixel);
    g_assert_false(config.force_kitty);
    g_assert_false(config.force_iterm2);
    g_assert_cmpint(probe_fixture.enable_raw_mode_calls, ==, 0);
    g_assert_cmpint(probe_fixture.disable_raw_mode_calls, ==, 0);

    g_string_free(probe_fixture.writes, TRUE);
}

static void test_cli_protocol_resolution_auto_direct_ssh_requires_affirmative_signal(
    AppCliFixture *fixture,
    gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    static const gchar k_sixel_response[] = "\033[?4;1;2c";
    static const TerminalProbeTransportHooks hooks = {
        .stdin_is_tty = app_cli_probe_fixture_stdin_is_tty,
        .stdout_is_tty = app_cli_probe_fixture_stdout_is_tty,
        .tcgetattr_fn = app_cli_probe_fixture_tcgetattr,
        .tcsetattr_fn = app_cli_probe_fixture_tcsetattr,
        .tcflush_fn = app_cli_probe_fixture_tcflush,
        .write_fn = app_cli_probe_fixture_write,
        .read_char_with_timeout_fn = app_cli_probe_fixture_read_char_with_timeout,
        .monotonic_time_us_fn = app_cli_probe_fixture_monotonic_time_us,
    };
    AppCliProbeFixture probe_fixture = {
        .response = k_sixel_response,
        .response_length = sizeof(k_sixel_response) - 1,
        .writes = g_string_new(NULL),
    };
    AppConfig config;

    queue_terminal_protocol_env_restore_and_clear();
    pixelterm_env_set_for_test("SSH_CONNECTION", "client 22 server 22");

    terminal_probe_set_transport_hooks_for_test(&hooks, &probe_fixture);
    g_test_queue_destroy(reset_app_cli_probe_hooks, NULL);
    input_set_enable_raw_mode_observer_for_test(app_cli_probe_fixture_enable_raw_mode,
                                                &probe_fixture);
    input_set_disable_raw_mode_observer_for_test(app_cli_probe_fixture_disable_raw_mode,
                                                 &probe_fixture);

    app_config_init(&config);
    app_config_resolve_protocol(&config);

    g_assert_true(config.force_text);
    g_assert_false(config.force_sixel);
    g_assert_false(config.force_kitty);
    g_assert_false(config.force_iterm2);
    g_assert_cmpstr(probe_fixture.writes->str, ==, "");
    g_assert_cmpint(probe_fixture.enable_raw_mode_calls, ==, 0);
    g_assert_cmpint(probe_fixture.disable_raw_mode_calls, ==, 0);

    g_string_free(probe_fixture.writes, TRUE);
}

void register_app_cli_tests(void) {
    g_test_add_func("/app_cli/config/runtime_xdg_after_glib_cache_prime",
                    test_cli_default_config_respects_runtime_xdg_after_glib_cache_prime);
    add_app_cli_test("/app_cli/parse/boolean_aliases", test_cli_boolean_aliases_parse_for_preload_and_alt_screen);
    add_app_cli_test("/app_cli/parse/invalid_boolean_values", test_cli_invalid_boolean_values_return_error);
    add_app_cli_test("/app_cli/parse/double_dash_preserves_positional_config_like_path",
                     test_cli_double_dash_preserves_positional_config_like_path);
    add_app_cli_test("/app_cli/parse/help_mentions_xdg_and_home_config_defaults",
                     test_cli_help_mentions_xdg_and_home_config_defaults);
    add_app_cli_test("/app_cli/parse/protocol", test_cli_protocol_argument_parses_supported_modes);
    add_app_cli_test("/app_cli/parse/text_symbols",
                     test_cli_text_symbols_argument_parses_supported_modes);
    add_app_cli_test("/app_cli/parse/color_enhance",
                     test_cli_color_enhance_argument_parses_supported_modes);
    add_app_cli_test("/app_cli/parse/color_enhance_invalid",
                     test_cli_color_enhance_argument_rejects_unknown_mode);
    add_app_cli_test("/app_cli/protocol_resolution/auto_prefers_affirmative_signal_before_generic_probe_order",
                     test_cli_protocol_resolution_auto_prefers_affirmative_signal_before_generic_probe_order);
    add_app_cli_test("/app_cli/protocol_resolution/auto_accepts_libghostty_xtversion_as_kitty_signal",
                     test_cli_protocol_resolution_auto_accepts_libghostty_xtversion_as_kitty_signal);
    add_app_cli_test("/app_cli/protocol_resolution/auto_accepts_ghostty_xtversion_as_kitty_signal",
                     test_cli_protocol_resolution_auto_accepts_ghostty_xtversion_as_kitty_signal);
    add_app_cli_test("/app_cli/protocol_resolution/auto_multi_protocol_hint_keeps_local_sixel_first_order",
                     test_cli_protocol_resolution_auto_multi_protocol_hint_keeps_local_sixel_first_order);
    add_app_cli_test("/app_cli/protocol_resolution/auto_prefers_warp_hint_without_affirmative_probe",
                     test_cli_protocol_resolution_auto_prefers_warp_hint_without_affirmative_probe);
    add_app_cli_test("/app_cli/protocol_resolution/auto_probe_avoids_raw_mode_transitions",
                     test_cli_protocol_resolution_auto_probe_avoids_raw_mode_transitions);
    add_app_cli_test("/app_cli/protocol_resolution/auto_direct_ssh_requires_affirmative_signal",
                     test_cli_protocol_resolution_auto_direct_ssh_requires_affirmative_signal);
    add_app_cli_test("/app_cli/protocol_resolution/auto_uses_text_when_probe_is_inconclusive",
                     test_cli_protocol_resolution_auto_uses_text_when_probe_is_inconclusive);
    add_app_cli_test("/app_cli/config/apply_runtime_copies_app_fields",
                     test_cli_apply_runtime_copies_app_owned_config_fields);
    add_app_cli_test("/app_cli/config/default_file_terminal_precedence", test_cli_default_config_applies_terminal_specific_precedence);
    add_app_cli_test("/app_cli/config/cli_overrides_loaded_values", test_cli_flags_override_loaded_config_values);
}
