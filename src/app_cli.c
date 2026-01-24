#define _GNU_SOURCE

#include "app_cli.h"
#include "input.h"
#include "terminal_protocols.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program_name) {
    printf("PixelTerm-C: A high-performance terminal image browser written in C.\n");
    printf("\n");
    printf("Usage: %s [OPTIONS] [PATH]\n", program_name);
    printf("\n");
    printf("Arguments:\n");
    printf("  PATH    Path to an image file or directory containing images\n");
    printf("\n");
    printf("Options:\n");
    printf("  %-29s %s\n", "-h, --help", "Show this help message");
    printf("  %-29s %s\n", "-v, --version", "Show version information");
    printf("  %-29s %s\n", "-D, --dither", "Enable image dithering (default: disabled)");
    printf("  %-29s %s\n", "--no-preload", "Disable image preloading (default: enabled)");
    printf("  %-29s %s\n", "--no-alt-screen", "Disable alternate screen buffer (default: enabled)");
    printf("  %-29s %s\n", "--clear-workaround",
           "Improve UI appearance on some terminals but may reduce performance (default: disabled)");
    printf("  %-29s %s\n", "--work-factor N", "Quality/speed tradeoff (1-9, default: 9)");
    printf("  %-29s %s\n", "--protocol MODE", "Output protocol: auto, text, sixel, kitty, iterm2");
    printf("  %-29s %s\n", "--gamma G",
           "Gamma correction for image rendering (default: 0.5 in kitty on Linux, 1.0 otherwise)");
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
        {"no-preload", no_argument,      0, 1000},
        {"dither",     no_argument,      0, 1001},
        {"no-alt-screen", no_argument,   0, 1002},
        {"clear-workaround", no_argument, 0, 1003},
        {"work-factor", required_argument, 0, 1004},
        {"protocol", required_argument, 0, 1005},
        {"gamma", required_argument, 0, 1006},
        {0, 0, 0, 0}
    };

    // Disable getopt error messages
    opterr = 0;

    if (!config) {
        return ERROR_MEMORY_ALLOC;
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
            case 1000:  // --no-preload
                config->preload_enabled = FALSE;
                break;
            case 1001: // --dither
                config->dither_enabled = TRUE;
                break;
            case 1002: // --no-alt-screen
                config->alt_screen_enabled = FALSE;
                break;
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
        config->force_kitty = probe_kitty_support();
        if (config->force_kitty) {
            config->force_iterm2 = FALSE;
            config->force_sixel = FALSE;
        } else {
            config->force_iterm2 = probe_iterm2_support();
            if (config->force_iterm2) {
                config->force_sixel = FALSE;
            } else {
                config->force_sixel = probe_sixel_support();
            }
        }
    }
    if (config->force_text) {
        config->force_kitty = FALSE;
        config->force_iterm2 = FALSE;
        config->force_sixel = FALSE;
    }
#if defined(__linux__)
    if (!config->gamma_set && config->force_kitty) {
        const TerminalProtocolHint *hint = terminal_protocol_env_match();
        if (hint && g_strcmp0(hint->name, "kitty") == 0) {
            config->gamma = 0.5;
        }
    }
#endif
}
