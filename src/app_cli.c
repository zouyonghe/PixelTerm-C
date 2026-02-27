#define _GNU_SOURCE

#include "app_cli.h"
#include "input.h"
#include "terminal_protocols.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program_name) {
    printf("PixelTerm-C: A high-performance terminal image/video/book browser written in C.\n");
    printf("\n");
    printf("Usage: %s [OPTIONS] [PATH]\n", program_name);
    printf("\n");
    printf("Arguments:\n");
    printf("  PATH    Path to an image/video/book file or a directory to browse\n");
    printf("\n");
    printf("Options:\n");
    printf("  %-29s %s\n", "-h, --help", "Show this help message");
    printf("  %-29s %s\n", "-v, --version", "Show version information");
    printf("  %-29s %s\n", "-D, --dither", "Enable image dithering (default: disabled)");
    printf("  %-29s %s\n", "--preload BOOL", "Enable image preloading (default: true)");
    printf("  %-29s %s\n", "--alt-screen BOOL", "Use alternate screen buffer (default: true)");
    printf("  %-29s %s\n", "--clear-workaround",
           "Improve UI appearance on some terminals but may reduce performance (default: disabled)");
    printf("  %-29s %s\n", "--work-factor N", "Quality/speed tradeoff (1-9, default: 9)");
    printf("  %-29s %s\n", "--protocol MODE", "Output protocol: auto, text, sixel, kitty, iterm2");
    printf("  %-29s %s\n", "--config PATH", "Load configuration file (default: $XDG_CONFIG_HOME/pixelterm/config.ini)");
    printf("  %-29s %s\n", "--gamma G",
           "Gamma correction for image rendering (default: 1.0)");
    printf("\n");
}

static void print_version(void) {
    printf("%s\n", APP_VERSION);
}

static gboolean parse_protocol_mode(const char *value, AppProtocolMode *out_mode) {
    if (!value || !out_mode) {
        return FALSE;
    }
    if (g_ascii_strcasecmp(value, "auto") == 0) {
        *out_mode = APP_PROTOCOL_AUTO;
        return TRUE;
    }
    if (g_ascii_strcasecmp(value, "text") == 0) {
        *out_mode = APP_PROTOCOL_TEXT;
        return TRUE;
    }
    if (g_ascii_strcasecmp(value, "sixel") == 0) {
        *out_mode = APP_PROTOCOL_SIXEL;
        return TRUE;
    }
    if (g_ascii_strcasecmp(value, "kitty") == 0) {
        *out_mode = APP_PROTOCOL_KITTY;
        return TRUE;
    }
    if (g_ascii_strcasecmp(value, "iterm2") == 0) {
        *out_mode = APP_PROTOCOL_ITERM2;
        return TRUE;
    }
    return FALSE;
}

static gchar *app_default_config_path(void) {
    const gchar *config_dir = g_get_user_config_dir();
    if (!config_dir || config_dir[0] == '\0') {
        config_dir = g_get_home_dir();
        if (!config_dir || config_dir[0] == '\0') {
            return NULL;
        }
        return g_build_filename(config_dir, ".config", "pixelterm", "config.ini", NULL);
    }
    return g_build_filename(config_dir, "pixelterm", "config.ini", NULL);
}

static const char *app_config_base_group(GKeyFile *key_file) {
    if (g_key_file_has_group(key_file, "default")) {
        return "default";
    }
    return NULL;
}

static const char *app_config_terminal_group(GKeyFile *key_file) {
    const char *candidates[] = {
        getenv("TERM_PROGRAM"),
        getenv("LC_TERMINAL"),
        getenv("TERMINAL_NAME"),
        getenv("TERM"),
        NULL
    };
    for (gsize i = 0; candidates[i]; i++) {
        if (candidates[i][0] == '\0') {
            continue;
        }
        if (g_key_file_has_group(key_file, candidates[i])) {
            return candidates[i];
        }
    }
    return NULL;
}

static gboolean app_parse_boolean(const char *value, gboolean *out_value) {
    if (!value || !out_value) {
        return FALSE;
    }
    if (g_ascii_strcasecmp(value, "true") == 0 ||
        g_ascii_strcasecmp(value, "yes") == 0 ||
        g_ascii_strcasecmp(value, "on") == 0 ||
        strcmp(value, "1") == 0) {
        *out_value = TRUE;
        return TRUE;
    }
    if (g_ascii_strcasecmp(value, "false") == 0 ||
        g_ascii_strcasecmp(value, "no") == 0 ||
        g_ascii_strcasecmp(value, "off") == 0 ||
        strcmp(value, "0") == 0) {
        *out_value = FALSE;
        return TRUE;
    }
    return FALSE;
}

static gboolean app_config_read_boolean(GKeyFile *key_file,
                                        const char *group,
                                        const char *key,
                                        const char *path,
                                        gboolean *out_value) {
    if (!g_key_file_has_key(key_file, group, key, NULL)) {
        return TRUE;
    }
    GError *error = NULL;
    gboolean value = g_key_file_get_boolean(key_file, group, key, &error);
    if (error) {
        fprintf(stderr, "Invalid '%s' in config file '%s': %s\n", key, path, error->message);
        g_error_free(error);
        return FALSE;
    }
    *out_value = value;
    return TRUE;
}

static gboolean app_config_read_integer(GKeyFile *key_file,
                                        const char *group,
                                        const char *key,
                                        const char *path,
                                        gint min_value,
                                        gint max_value,
                                        gint *out_value) {
    if (!g_key_file_has_key(key_file, group, key, NULL)) {
        return TRUE;
    }
    GError *error = NULL;
    gint value = g_key_file_get_integer(key_file, group, key, &error);
    if (error) {
        fprintf(stderr, "Invalid '%s' in config file '%s': %s\n", key, path, error->message);
        g_error_free(error);
        return FALSE;
    }
    if (value < min_value || value > max_value) {
        fprintf(stderr, "Invalid '%s' in config file '%s' (expected %d-%d)\n",
                key, path, min_value, max_value);
        return FALSE;
    }
    *out_value = value;
    return TRUE;
}

static gboolean app_config_read_double(GKeyFile *key_file,
                                       const char *group,
                                       const char *key,
                                       const char *path,
                                       gdouble min_value,
                                       gdouble max_value,
                                       gdouble *out_value) {
    if (!g_key_file_has_key(key_file, group, key, NULL)) {
        return TRUE;
    }
    GError *error = NULL;
    gdouble value = g_key_file_get_double(key_file, group, key, &error);
    if (error) {
        fprintf(stderr, "Invalid '%s' in config file '%s': %s\n", key, path, error->message);
        g_error_free(error);
        return FALSE;
    }
    if (value <= min_value || value > max_value) {
        fprintf(stderr, "Invalid '%s' in config file '%s' (expected >%.2f and <=%.2f)\n",
                key, path, min_value, max_value);
        return FALSE;
    }
    *out_value = value;
    return TRUE;
}

static gboolean app_config_apply_group(GKeyFile *key_file,
                                       const char *group,
                                       const char *path,
                                       AppConfig *config) {
    if (!key_file || !group || !path || !config) {
        return TRUE;
    }
    if (!g_key_file_has_group(key_file, group)) {
        return TRUE;
    }

    if (!app_config_read_boolean(key_file, group, "preload", path, &config->preload_enabled) ||
        !app_config_read_boolean(key_file, group, "dither", path, &config->dither_enabled) ||
        !app_config_read_boolean(key_file, group, "alt_screen", path, &config->alt_screen_enabled) ||
        !app_config_read_boolean(key_file, group, "clear_workaround", path,
                                 &config->clear_workaround_enabled) ||
        !app_config_read_integer(key_file, group, "work_factor", path, 1, 9, &config->work_factor)) {
        return FALSE;
    }

    if (g_key_file_has_key(key_file, group, "protocol", NULL)) {
        GError *error = NULL;
        gchar *value = g_key_file_get_string(key_file, group, "protocol", &error);
        if (error) {
            fprintf(stderr, "Invalid 'protocol' in config file '%s': %s\n", path, error->message);
            g_error_free(error);
            g_free(value);
            return FALSE;
        }
        AppProtocolMode mode = APP_PROTOCOL_AUTO;
        if (!parse_protocol_mode(value, &mode)) {
            fprintf(stderr, "Invalid 'protocol' in config file '%s': %s\n", path, value);
            g_free(value);
            return FALSE;
        }
        config->protocol_mode = mode;
        g_free(value);
    }

    if (g_key_file_has_key(key_file, group, "gamma", NULL)) {
        gdouble gamma = config->gamma;
        if (!app_config_read_double(key_file, group, "gamma", path, 0.0, 5.0, &gamma)) {
            return FALSE;
        }
        config->gamma = gamma;
        config->gamma_set = TRUE;
    }

    return TRUE;
}

static ErrorCode app_config_load_file(AppConfig *config,
                                      const char *path,
                                      gboolean required) {
    if (!config || !path) {
        return ERROR_INVALID_ARGS;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
        if (!required && error && g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_error_free(error);
            g_key_file_free(key_file);
            return ERROR_NONE;
        }
        fprintf(stderr, "Failed to load config file '%s': %s\n", path,
                error ? error->message : "unknown error");
        if (error) {
            g_error_free(error);
        }
        g_key_file_free(key_file);
        return ERROR_INVALID_ARGS;
    }

    const char *base_group = app_config_base_group(key_file);
    if (base_group && !app_config_apply_group(key_file, base_group, path, config)) {
        g_key_file_free(key_file);
        return ERROR_INVALID_ARGS;
    }

    const char *terminal_group = app_config_terminal_group(key_file);
    if (terminal_group && !app_config_apply_group(key_file, terminal_group, path, config)) {
        g_key_file_free(key_file);
        return ERROR_INVALID_ARGS;
    }

    g_key_file_free(key_file);
    return ERROR_NONE;
}

static ErrorCode app_config_preload_from_args(int argc, char *argv[], AppConfig *config) {
    gchar *config_path = NULL;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (g_str_has_prefix(arg, "--config=")) {
            const char *value = arg + strlen("--config=");
            if (!value || value[0] == '\0') {
                fprintf(stderr, "Invalid --config value: (expected path)\n");
                g_free(config_path);
                return ERROR_INVALID_ARGS;
            }
            g_free(config_path);
            config_path = g_strdup(value);
        } else if (strcmp(arg, "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Invalid --config value: (expected path)\n");
                g_free(config_path);
                return ERROR_INVALID_ARGS;
            }
            g_free(config_path);
            config_path = g_strdup(argv[i + 1]);
        }
    }

    ErrorCode error = ERROR_NONE;
    if (config_path) {
        error = app_config_load_file(config, config_path, TRUE);
    } else {
        gchar *default_path = app_default_config_path();
        if (default_path) {
            error = app_config_load_file(config, default_path, FALSE);
            g_free(default_path);
        }
    }

    g_free(config_path);
    return error;
}

static gboolean probe_sixel_support(void) {
    if (terminal_env_supports_sixel()) {
        return TRUE;
    }
    InputHandler *probe = input_handler_create();
    if (!probe) {
        return FALSE;
    }

    if (input_handler_initialize(probe) != ERROR_NONE) {
        input_handler_destroy(probe);
        return FALSE;
    }

    if (input_enable_raw_mode(probe) != ERROR_NONE) {
        input_handler_destroy(probe);
        return FALSE;
    }

    gboolean sixel_supported = input_probe_sixel_support(probe, 120);
    input_disable_raw_mode(probe);
    input_handler_destroy(probe);

    return sixel_supported;
}

static gboolean probe_kitty_support(void) {
    if (terminal_env_supports_kitty()) {
        return TRUE;
    }

    InputHandler *probe = input_handler_create();
    if (!probe) {
        return FALSE;
    }

    if (input_handler_initialize(probe) != ERROR_NONE) {
        input_handler_destroy(probe);
        return FALSE;
    }

    if (input_enable_raw_mode(probe) != ERROR_NONE) {
        input_handler_destroy(probe);
        return FALSE;
    }

    gboolean kitty_supported = input_probe_kitty_support(probe, 120);
    input_disable_raw_mode(probe);
    input_handler_destroy(probe);

    return kitty_supported;
}

static gboolean probe_iterm2_support(void) {
    if (terminal_env_supports_iterm2()) {
        return TRUE;
    }

    InputHandler *probe = input_handler_create();
    if (!probe) {
        return FALSE;
    }

    if (input_handler_initialize(probe) != ERROR_NONE) {
        input_handler_destroy(probe);
        return FALSE;
    }

    if (input_enable_raw_mode(probe) != ERROR_NONE) {
        input_handler_destroy(probe);
        return FALSE;
    }

    gboolean iterm2_supported = input_probe_iterm2_support(probe, 120);
    input_disable_raw_mode(probe);
    input_handler_destroy(probe);

    return iterm2_supported;
}

void app_config_init(AppConfig *config) {
    if (!config) {
        return;
    }
    config->preload_enabled = TRUE;
    config->dither_enabled = FALSE;
    config->alt_screen_enabled = TRUE;
    config->clear_workaround_enabled = FALSE;
    config->work_factor = 9;
    config->gamma = 1.0;
    config->gamma_set = FALSE;
    config->protocol_mode = APP_PROTOCOL_AUTO;
    config->force_text = FALSE;
    config->force_sixel = FALSE;
    config->force_kitty = FALSE;
    config->force_iterm2 = FALSE;
}

ErrorCode app_parse_arguments(int argc, char *argv[], char **path, AppConfig *config) {
    static struct option long_options[] = {
        {"help",      no_argument,       0, 'h'},
        {"version",   no_argument,       0, 'v'},
        {"Version",   no_argument,       0, 'V'},
        {"preload", required_argument,   0, 1000},
        {"dither",     no_argument,      0, 1001},
        {"alt-screen", required_argument,   0, 1002},
        {"clear-workaround", no_argument, 0, 1003},
        {"work-factor", required_argument, 0, 1004},
        {"protocol", required_argument, 0, 1005},
        {"gamma", required_argument, 0, 1006},
        {"config", required_argument, 0, 1007},
        {0, 0, 0, 0}
    };

    // Disable getopt error messages
    opterr = 0;

    if (!config) {
        return ERROR_MEMORY_ALLOC;
    }

    ErrorCode config_error = app_config_preload_from_args(argc, argv, config);
    if (config_error != ERROR_NONE) {
        return config_error;
    }

    int c;
    while ((c = getopt_long(argc, argv, "hvVD", long_options, NULL)) != -1) {
        switch (c) {
            case 'h':
                print_usage(argv[0]);
                return ERROR_HELP_EXIT;
            case 'v':
                print_version();
                return ERROR_VERSION_EXIT;
            case 'V':
                print_version();
                return ERROR_VERSION_EXIT;
            case 'D': // -D option for dithering
                config->dither_enabled = TRUE;
                break;
            case 1000: { // --preload
                gboolean value = TRUE;
                if (!app_parse_boolean(optarg, &value)) {
                    fprintf(stderr, "Invalid --preload value: %s (expected true/false)\n",
                            optarg ? optarg : "");
                    return ERROR_INVALID_ARGS;
                }
                config->preload_enabled = value;
                break;
            }
            case 1001: // --dither
                config->dither_enabled = TRUE;
                break;
            case 1002: { // --alt-screen
                gboolean value = FALSE;
                if (!app_parse_boolean(optarg, &value)) {
                    fprintf(stderr, "Invalid --alt-screen value: %s (expected true/false)\n",
                            optarg ? optarg : "");
                    return ERROR_INVALID_ARGS;
                }
                config->alt_screen_enabled = value;
                break;
            }
            case 1003: // --clear-workaround
                config->clear_workaround_enabled = TRUE;
                break;
            case 1004: { // --work-factor
                char *end = NULL;
                long value = strtol(optarg, &end, 10);
                if (!optarg || optarg[0] == '\0' || (end && *end != '\0')) {
                    fprintf(stderr, "Invalid --work-factor value: %s (expected 1-9)\n",
                            optarg ? optarg : "");
                    return ERROR_INVALID_ARGS;
                }
                if (value < 1 || value > 9) {
                    fprintf(stderr, "Invalid --work-factor value: %ld (expected 1-9)\n", value);
                    return ERROR_INVALID_ARGS;
                }
                config->work_factor = (gint)value;
                break;
            }
            case 1005: { // --protocol
                AppProtocolMode mode = APP_PROTOCOL_AUTO;
                if (!parse_protocol_mode(optarg, &mode)) {
                    fprintf(stderr, "Unknown protocol: %s\n", optarg ? optarg : "");
                    return ERROR_INVALID_ARGS;
                }
                config->protocol_mode = mode;
                break;
            }
            case 1006: { // --gamma
                char *end = NULL;
                double value = strtod(optarg, &end);
                if (!optarg || optarg[0] == '\0' || (end && *end != '\0')) {
                    fprintf(stderr, "Invalid --gamma value: %s (expected float)\n",
                            optarg ? optarg : "");
                    return ERROR_INVALID_ARGS;
                }
                if (value <= 0.0 || value > 5.0) {
                    fprintf(stderr, "Invalid --gamma value: %.2f (expected >0 and <=5)\n", value);
                    return ERROR_INVALID_ARGS;
                }
                config->gamma = value;
                config->gamma_set = TRUE;
                break;
            }
            case 1007: // --config
                break;
            case '?':
                // Check if it's a long option (starts with --)
                if (optind > 0 && argv[optind - 1] && strncmp(argv[optind - 1], "--", 2) == 0) {
                    fprintf(stderr, "Invalid option: %s\n", argv[optind - 1]);
                }
                // Check if it's a short option (starts with - but not --)
                else if (optind > 0 && argv[optind - 1] && strncmp(argv[optind - 1], "-", 1) == 0) {
                    fprintf(stderr, "Invalid option: %s\n", argv[optind - 1]);
                }
                // Fallback for unknown cases
                else {
                    fprintf(stderr, "Invalid option\n");
                }
                fprintf(stderr, "Use --help for usage information\n");
                return ERROR_INVALID_ARGS;
            default:
                break;
        }
    }

    // Get path from remaining arguments
    if (optind < argc) {
        *path = g_strdup(argv[optind]);
    }

    return ERROR_NONE;
}

void app_config_resolve_protocol(AppConfig *config) {
    if (!config) {
        return;
    }

    config->force_text = FALSE;
    config->force_sixel = FALSE;
    config->force_kitty = FALSE;
    config->force_iterm2 = FALSE;

    switch (config->protocol_mode) {
        case APP_PROTOCOL_TEXT:
            config->force_text = TRUE;
            break;
        case APP_PROTOCOL_SIXEL:
            config->force_sixel = TRUE;
            break;
        case APP_PROTOCOL_KITTY:
            config->force_kitty = TRUE;
            break;
        case APP_PROTOCOL_ITERM2:
            config->force_iterm2 = TRUE;
            break;
        case APP_PROTOCOL_AUTO:
        default:
            break;
    }

    if (!config->force_text && !config->force_kitty && !config->force_iterm2 && !config->force_sixel) {
        config->force_sixel = probe_sixel_support();
        if (config->force_sixel) {
            config->force_iterm2 = FALSE;
            config->force_kitty = FALSE;
        } else {
            config->force_iterm2 = probe_iterm2_support();
            if (config->force_iterm2) {
                config->force_kitty = FALSE;
            } else {
                config->force_kitty = probe_kitty_support();
            }
        }
    }
    if (config->force_text) {
        config->force_kitty = FALSE;
        config->force_iterm2 = FALSE;
        config->force_sixel = FALSE;
    }
}
