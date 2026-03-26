#include <glib.h>
#include <glib/gstdio.h>

#include "app_cli.h"

#include <stdlib.h>
#include <string.h>

static const gchar * const k_app_cli_env_keys[] = {
    "TERM_PROGRAM",
    "LC_TERMINAL",
    "TERMINAL_NAME",
    "TERM",
    "XDG_CONFIG_HOME",
    NULL,
};

#define APP_CLI_ENV_KEY_COUNT (G_N_ELEMENTS(k_app_cli_env_keys) - 1)

typedef struct {
    gchar *saved_values[APP_CLI_ENV_KEY_COUNT];
} AppCliFixture;

typedef struct {
    const gchar *value;
    AppProtocolMode expected_mode;
} ProtocolCase;

typedef struct {
    const gchar *option;
    const gchar *value;
    gboolean expected_value;
} BooleanAliasCase;

typedef struct {
    gchar *name;
    gchar *value;
} EnvVarRestore;

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

static void restore_env_var(gpointer data) {
    EnvVarRestore *saved = data;
    if (!saved) {
        return;
    }

    if (saved->value) {
        g_setenv(saved->name, saved->value, TRUE);
    } else {
        g_unsetenv(saved->name);
    }

    g_free(saved->name);
    g_free(saved->value);
    g_free(saved);
}

static void queue_env_restore(const gchar *name) {
    EnvVarRestore *saved = g_new0(EnvVarRestore, 1);
    saved->name = g_strdup(name);
    saved->value = g_strdup(g_getenv(name));
    g_test_queue_destroy(restore_env_var, saved);
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
        g_unsetenv(k_app_cli_env_keys[i]);
    }
}

static void app_cli_fixture_set_up(AppCliFixture *fixture, gconstpointer user_data) {
    (void)user_data;

    for (gsize i = 0; i < APP_CLI_ENV_KEY_COUNT; i++) {
        fixture->saved_values[i] = g_strdup(g_getenv(k_app_cli_env_keys[i]));
    }

    clear_app_cli_env();
    g_assert_true(g_setenv("XDG_CONFIG_HOME", app_cli_shared_config_root(), TRUE));
}

static void app_cli_fixture_tear_down(AppCliFixture *fixture, gconstpointer user_data) {
    (void)user_data;

    for (gsize i = 0; i < APP_CLI_ENV_KEY_COUNT; i++) {
        if (fixture->saved_values[i]) {
            g_setenv(k_app_cli_env_keys[i], fixture->saved_values[i], TRUE);
        } else {
            g_unsetenv(k_app_cli_env_keys[i]);
        }

        g_clear_pointer(&fixture->saved_values[i], g_free);
    }
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
        app_config_init(&config);

        char *argv[] = {
            "pixelterm",
            (char *)cases[i].option,
            (char *)cases[i].value,
            NULL,
        };

        g_assert_cmpint(parse_cli_args(argv, &path, &config), ==, ERROR_INVALID_ARGS);
        g_assert_null(path);
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

static void test_cli_default_config_applies_terminal_specific_precedence(AppCliFixture *fixture,
                                                                         gconstpointer user_data) {
    (void)fixture;
    (void)user_data;

    write_default_config_file(
        "[default]\n"
        "preload=false\n"
        "alt_screen=true\n"
        "clear_workaround=false\n"
        "work_factor=3\n"
        "protocol=text\n"
        "gamma=1.25\n"
        "\n"
        "[WarpTerminal]\n"
        "alt_screen=false\n"
        "clear_workaround=true\n"
        "protocol=kitty\n");

    g_assert_true(g_setenv("TERM_PROGRAM", "WarpTerminal", TRUE));

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
    g_assert_true(config.gamma_set);
    g_assert_cmpfloat_with_epsilon(config.gamma, 1.25, 0.0001);
    g_free(path);
}

static void test_cli_default_config_respects_runtime_xdg_after_glib_cache_prime(void) {
    queue_env_restore("TERM_PROGRAM");
    queue_env_restore("XDG_CONFIG_HOME");

    g_unsetenv("XDG_CONFIG_HOME");
    (void)g_get_user_config_dir();

    gchar *config_root = make_temp_config_root();
    write_default_config_file_at_root(
        config_root,
        "[default]\n"
        "preload=false\n"
        "alt_screen=true\n"
        "clear_workaround=false\n"
        "work_factor=3\n"
        "protocol=text\n"
        "gamma=1.25\n"
        "\n"
        "[WarpTerminal]\n"
        "alt_screen=false\n"
        "clear_workaround=true\n"
        "protocol=kitty\n");

    g_assert_true(g_setenv("XDG_CONFIG_HOME", config_root, TRUE));
    g_assert_true(g_setenv("TERM_PROGRAM", "WarpTerminal", TRUE));

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
    g_assert_true(config.gamma_set);
    g_assert_cmpfloat_with_epsilon(config.gamma, 1.25, 0.0001);

    g_free(path);
    g_free(config_root);
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
    g_assert_true(config.gamma_set);
    g_assert_cmpfloat_with_epsilon(config.gamma, 2.5, 0.0001);
    g_free(path);
}

void register_app_cli_tests(void) {
    g_test_add_func("/app_cli/config/runtime_xdg_after_glib_cache_prime",
                    test_cli_default_config_respects_runtime_xdg_after_glib_cache_prime);
    add_app_cli_test("/app_cli/parse/boolean_aliases", test_cli_boolean_aliases_parse_for_preload_and_alt_screen);
    add_app_cli_test("/app_cli/parse/invalid_boolean_values", test_cli_invalid_boolean_values_return_error);
    add_app_cli_test("/app_cli/parse/protocol", test_cli_protocol_argument_parses_supported_modes);
    add_app_cli_test("/app_cli/config/default_file_terminal_precedence", test_cli_default_config_applies_terminal_specific_precedence);
    add_app_cli_test("/app_cli/config/cli_overrides_loaded_values", test_cli_flags_override_loaded_config_values);
}
