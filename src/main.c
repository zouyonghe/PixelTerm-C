#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <locale.h>
#include <unistd.h>

#include <glib.h>

#include "app.h"
#include "input.h"
#include "common.h"
#include "video_player.h"

// Global application instance
static PixelTermApp *g_app = NULL;
static volatile sig_atomic_t g_terminate_requested = 0;
static volatile sig_atomic_t g_last_signal = 0;
static gboolean g_force_sixel = FALSE;
static gboolean g_force_kitty = FALSE;
static gboolean g_force_iterm2 = FALSE;
static gboolean g_force_text = FALSE;
static const gint64 k_click_threshold_us = 400000;
static const useconds_t k_input_poll_sleep_us = 10000;
static const useconds_t k_resize_sleep_us = 100000;
static const gint64 k_protocol_toggle_debounce_us = 150000;
static gint64 g_last_protocol_toggle_us = 0;
static const KeyCode g_nav_keys_lr[] = {
    KEY_LEFT, (KeyCode)'h', KEY_UP, KEY_DOWN, KEY_RIGHT, (KeyCode)'l', KEY_PAGE_UP, KEY_PAGE_DOWN
};
static const KeyCode g_nav_keys_ud[] = {
    KEY_UP, (KeyCode)'k', KEY_LEFT, (KeyCode)'h', KEY_RIGHT, (KeyCode)'l', KEY_DOWN, (KeyCode)'j',
    KEY_PAGE_UP, KEY_PAGE_DOWN
};
static const KeyCode g_nav_keys_page[] = {
    KEY_PAGE_DOWN, KEY_PAGE_UP, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, (KeyCode)'a'
};

static void handle_delete_current_image(PixelTermApp *app);

static gboolean app_current_is_video(const PixelTermApp *app) {
    if (!app || app->preview_mode || app->file_manager_mode) {
        return FALSE;
    }
    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return FALSE;
    }
    gboolean is_animated_image = is_animated_image_candidate(filepath);
    gboolean is_video = is_video_file(filepath);
    if (!is_video && !is_animated_image && !is_image_file(filepath)) {
        is_video = is_valid_video_file(filepath);
    }
    if (is_animated_image && is_video) {
        is_video = FALSE;
    }
    return is_video;
}

static gint delete_prompt_display_width(const char *text) {
    if (!text) {
        return 0;
    }
    gint width = 0;
    const char *cursor = text;
    while (*cursor) {
        gunichar ch = g_utf8_get_char_validated(cursor, -1);
        if (ch == (gunichar)-1 || ch == (gunichar)-2) {
            width += 1;
            cursor++;
            continue;
        }
        width += g_unichar_iswide(ch) ? 2 : 1;
        cursor = g_utf8_next_char(cursor);
    }
    return width;
}

static gint delete_prompt_row(const PixelTermApp *app) {
    if (!app) {
        return 1;
    }

    gint term_height = app->term_height > 0 ? app->term_height : 24;
    gint row = term_height - 1;

    if (!app->preview_mode && !app->file_manager_mode) {
        if (app_current_is_video(app) && app->video_player && app->video_player->last_frame_height > 0) {
            row = app->video_player->last_frame_top_row + app->video_player->last_frame_height;
        } else if (app->last_render_height > 0 && app->last_render_top_row > 0) {
            row = app->last_render_top_row + app->last_render_height;
        }
    }

    if (row < 1) {
        row = 1;
    } else if (row > term_height - 1) {
        row = term_height - 1;
    }

    return row;
}

static void app_show_delete_prompt(PixelTermApp *app) {
    if (!app) {
        return;
    }
    const char *message = "Press r again to delete";
    gint term_width = app->term_width > 0 ? app->term_width : 80;
    gint row = delete_prompt_row(app);

    gint message_len = delete_prompt_display_width(message);
    gint col = term_width > message_len ? (term_width - message_len) / 2 + 1 : 1;

    printf("\033[%d;1H\033[2K", row);
    printf("\033[%d;%dH\033[31m%s\033[0m", row, col, message);
    fflush(stdout);
}

static void app_clear_delete_prompt(PixelTermApp *app) {
    if (!app) {
        return;
    }
    gint row = delete_prompt_row(app);
    printf("\033[%d;1H\033[2K", row);
    fflush(stdout);
}

static void handle_delete_current_in_preview(PixelTermApp *app) {
    if (app_has_images(app)) {
        app->current_index = app->preview_selected;
        app_delete_current_image(app);
    }

    if (app_has_images(app)) {
        if (app->current_index < 0) app->current_index = 0;
        if (app->current_index >= app->total_images) app->current_index = app->total_images - 1;
        app->preview_selected = app->current_index;
        app->needs_screen_clear = TRUE;
        app_render_preview_grid(app);
    } else {
        app->preview_mode = FALSE;
        app->needs_screen_clear = TRUE;
        if (app_enter_file_manager(app) == ERROR_NONE) {
            app_render_file_manager(app);
        } else {
            app_refresh_display(app);
        }
    }
}

static gboolean handle_delete_request(PixelTermApp *app, const InputEvent *event) {
    if (!app || !event || event->type != INPUT_KEY_PRESS) {
        return FALSE;
    }

    if (app->file_manager_mode) {
        if (app->delete_pending) {
            app->delete_pending = FALSE;
            app_clear_delete_prompt(app);
        }
        return FALSE;
    }

    if (app->delete_pending) {
        app->delete_pending = FALSE;
        if (event->key_code == (KeyCode)'r') {
            if (app->preview_mode) {
                handle_delete_current_in_preview(app);
            } else {
                handle_delete_current_image(app);
            }
            return TRUE;
        }
        app_clear_delete_prompt(app);
        return FALSE;
    }

    if (event->key_code == (KeyCode)'r') {
        app->delete_pending = TRUE;
        app_show_delete_prompt(app);
        return TRUE;
    }

    return FALSE;
}

static void app_pause_video_for_resize(PixelTermApp *app) {
    if (!app || !app->video_player) {
        return;
    }
    if (!app_current_is_video(app)) {
        return;
    }
    if (video_player_is_playing(app->video_player)) {
        video_player_pause(app->video_player);
    }
}

static void app_toggle_video_playback(PixelTermApp *app) {
    if (!app || !app->video_player) {
        return;
    }
    if (!app_current_is_video(app)) {
        return;
    }
    if (video_player_is_playing(app->video_player)) {
        video_player_pause(app->video_player);
    } else if (video_player_has_video(app->video_player)) {
        video_player_play(app->video_player);
    }
}

static gboolean probe_sixel_support(void) {
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

static gboolean is_kitty_terminal_env(void) {
    const char *term = getenv("TERM");
    if (term && (strcmp(term, "xterm-kitty") == 0 || strcmp(term, "kitty") == 0)) {
        return TRUE;
    }

    const char *kitty_window_id = getenv("KITTY_WINDOW_ID");
    if (kitty_window_id && *kitty_window_id) {
        return TRUE;
    }

    const char *term_program = getenv("TERM_PROGRAM");
    if (term_program && strcmp(term_program, "kitty") == 0) {
        return TRUE;
    }

    return FALSE;
}

static gboolean probe_kitty_support(void) {
    if (is_kitty_terminal_env()) {
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
    const char *term_program = getenv("TERM_PROGRAM");
    if (term_program && (strcmp(term_program, "iTerm.app") == 0 || strcmp(term_program, "iTerm2") == 0)) {
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

// Signal handler for graceful shutdown
static void signal_handler(int sig) {
    (void)sig; // Suppress unused parameter warning
    g_terminate_requested = 1;
    g_last_signal = sig;
}

// Print usage information
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
    printf("  %-29s %s\n", "--clear-workaround", "Improve UI appearance on some terminals but may reduce performance (default: disabled)");
    printf("  %-29s %s\n", "--work-factor N", "Quality/speed tradeoff (1-9, default: 9)");
    printf("  %-29s %s\n", "--protocol MODE", "Output protocol: auto, text, sixel, kitty, iterm2");
    printf("  %-29s %s\n", "--gamma G", "Gamma correction for image rendering (default: 0.5 in kitty on Linux, 1.0 otherwise)");
    printf("\n");
}

// Print version information
static void print_version(void) {
    printf("%s\n", APP_VERSION);
}

// Parse command line arguments
static ErrorCode parse_arguments(int argc, char *argv[], char **path, gboolean *preload_enabled, gboolean *dither_enabled, gboolean *alt_screen_enabled, gboolean *clear_workaround_enabled, gint *work_factor, gchar **protocol, gdouble *gamma, gboolean *gamma_set) {
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
                *dither_enabled = TRUE;
                break;
            case 1000:  // --no-preload
                *preload_enabled = FALSE;
                break;
            case 1001: // --dither
                *dither_enabled = TRUE;
                break;
            case 1002: // --no-alt-screen
                *alt_screen_enabled = FALSE;
                break;
            case 1003: // --clear-workaround
                *clear_workaround_enabled = TRUE;
                break;
            case 1004: { // --work-factor
                char *end = NULL;
                long value = strtol(optarg, &end, 10);
                if (!optarg || optarg[0] == '\0' || (end && *end != '\0')) {
                    fprintf(stderr, "Invalid --work-factor value: %s (expected 1-9)\n", optarg ? optarg : "");
                    return ERROR_INVALID_ARGS;
                }
                if (value < 1 || value > 9) {
                    fprintf(stderr, "Invalid --work-factor value: %ld (expected 1-9)\n", value);
                    return ERROR_INVALID_ARGS;
                }
                *work_factor = (gint)value;
                break;
            }
            case 1005: { // --protocol
                if (protocol) {
                    g_free(*protocol);
                    *protocol = g_ascii_strdown(optarg, -1);
                }
                break;
            }
            case 1006: { // --gamma
                char *end = NULL;
                double value = strtod(optarg, &end);
                if (!optarg || optarg[0] == '\0' || (end && *end != '\0')) {
                    fprintf(stderr, "Invalid --gamma value: %s (expected float)\n", optarg ? optarg : "");
                    return ERROR_INVALID_ARGS;
                }
                if (value <= 0.0 || value > 5.0) {
                    fprintf(stderr, "Invalid --gamma value: %.2f (expected >0 and <=5)\n", value);
                    return ERROR_INVALID_ARGS;
                }
                if (gamma) {
                    *gamma = value;
                }
                if (gamma_set) {
                    *gamma_set = TRUE;
                }
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

// Validate path and determine if it's a file or directory
static ErrorCode validate_path(const char *path, gboolean *is_directory) {
    if (!path) {
        return ERROR_FILE_NOT_FOUND;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return ERROR_FILE_NOT_FOUND;
    }

    *is_directory = S_ISDIR(st.st_mode);
    return ERROR_NONE;
}

static gboolean key_in_list(KeyCode key, const KeyCode *keys, size_t key_count) {
    for (size_t i = 0; i < key_count; i++) {
        if (keys[i] == key) {
            return TRUE;
        }
    }
    return FALSE;
}

static void skip_queued_navigation(InputHandler *input_handler,
                                   const KeyCode *keys,
                                   size_t key_count) {
    InputEvent skip_event;
    while (input_has_pending_input(input_handler)) {
        ErrorCode skip_error = input_get_event(input_handler, &skip_event);
        if (skip_error != ERROR_NONE) {
            break;
        }
        if (skip_event.type != INPUT_KEY_PRESS ||
            !key_in_list(skip_event.key_code, keys, key_count)) {
            input_unget_event(input_handler, &skip_event);
            break;
        }
        // Skip this navigation event
    }
}

static void handle_mouse_press_preview(PixelTermApp *app, const InputEvent *event) {
    app->pending_grid_single_click = TRUE;
    app->pending_grid_click_time = g_get_monotonic_time();
    app->pending_grid_click_x = event->mouse_x;
    app->pending_grid_click_y = event->mouse_y;
}

static void handle_mouse_press_file_manager(PixelTermApp *app, const InputEvent *event) {
    app->pending_file_manager_single_click = TRUE;
    app->pending_file_manager_click_time = g_get_monotonic_time();
    app->pending_file_manager_click_x = event->mouse_x;
    app->pending_file_manager_click_y = event->mouse_y;
}

static void handle_mouse_press_single(PixelTermApp *app, const InputEvent *event) {
    if (event->mouse_button == MOUSE_BUTTON_LEFT && app_current_is_video(app)) {
        app_toggle_video_playback(app);
        app->pending_single_click = FALSE;
        return;
    }
    app->pending_single_click = TRUE;
    app->pending_click_time = g_get_monotonic_time();
}

static void handle_mouse_press(PixelTermApp *app, const InputEvent *event) {
    if (app->preview_mode) {
        handle_mouse_press_preview(app, event);
    } else if (app->file_manager_mode) {
        handle_mouse_press_file_manager(app, event);
    } else {
        handle_mouse_press_single(app, event);
    }
}

static void handle_mouse_double_click_preview(PixelTermApp *app, const InputEvent *event) {
    app->pending_grid_single_click = FALSE;

    gboolean redraw_needed = FALSE;
    gboolean hit = FALSE;
    app_handle_mouse_click_preview(app, event->mouse_x, event->mouse_y, &redraw_needed, &hit);
    if (!hit) {
        return;
    }

    if (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) {
        app->return_to_mode = RETURN_MODE_PREVIEW;
    }
    app->preview_mode = FALSE;
    app_render_current_image(app);
}

static void handle_mouse_double_click_file_manager(PixelTermApp *app, const InputEvent *event) {
    app->pending_file_manager_single_click = FALSE;
    ErrorCode err = app_file_manager_enter_at_position(app, event->mouse_x, event->mouse_y);
    if (err == ERROR_NONE && app->file_manager_mode) {
        app_render_file_manager(app);
    }
}

static void handle_mouse_double_click_single(PixelTermApp *app, const InputEvent *event) {
    (void)event;
    app->pending_single_click = FALSE;

    if (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) {
        app->return_to_mode = RETURN_MODE_PREVIEW;
    }
    if (app_enter_preview(app) == ERROR_NONE) {
        app_render_preview_grid(app);
    }
}

static void handle_mouse_double_click(PixelTermApp *app, const InputEvent *event) {
    if (app->preview_mode) {
        handle_mouse_double_click_preview(app, event);
    } else if (app->file_manager_mode) {
        handle_mouse_double_click_file_manager(app, event);
    } else {
        handle_mouse_double_click_single(app, event);
    }
}

static void handle_mouse_scroll_preview(PixelTermApp *app, const InputEvent *event) {
    gint old_selected = app->preview_selected;
    gint old_scroll = app->preview_scroll;
    if (event->mouse_button == MOUSE_SCROLL_UP) {
        app_preview_page_move(app, -1);
    } else if (event->mouse_button == MOUSE_SCROLL_DOWN) {
        app_preview_page_move(app, 1);
    }
    if (app->preview_scroll != old_scroll) {
        app_render_preview_grid(app);
    } else if (app->preview_selected != old_selected) {
        app_render_preview_selection_change(app, old_selected);
    }
}

static void handle_mouse_scroll_file_manager(PixelTermApp *app, const InputEvent *event) {
    gint old_selected = app->selected_entry;
    gint old_scroll = app->scroll_offset;
    if (event->mouse_button == MOUSE_SCROLL_UP) {
        app_file_manager_up(app);
    } else if (event->mouse_button == MOUSE_SCROLL_DOWN) {
        app_file_manager_down(app);
    }
    if (app->selected_entry != old_selected || app->scroll_offset != old_scroll) {
        app_render_file_manager(app);
    }
}

static void handle_mouse_scroll_single(PixelTermApp *app, const InputEvent *event) {
    gboolean redraw_needed = FALSE;
    if (event->mouse_button == MOUSE_SCROLL_UP) {
        gint old_index = app_get_current_index(app);
        app_previous_image(app);
        if (old_index != app_get_current_index(app)) {
            redraw_needed = TRUE;
        }
    } else if (event->mouse_button == MOUSE_SCROLL_DOWN) {
        gint old_index = app_get_current_index(app);
        app_next_image(app);
        if (old_index != app_get_current_index(app)) {
            redraw_needed = TRUE;
        }
    }
    if (redraw_needed) {
        app_refresh_display(app);
        app->needs_redraw = FALSE;
    }
}

static void handle_mouse_scroll(PixelTermApp *app, const InputEvent *event) {
    if (app->preview_mode) {
        handle_mouse_scroll_preview(app, event);
    } else if (app->file_manager_mode) {
        handle_mouse_scroll_file_manager(app, event);
    } else {
        handle_mouse_scroll_single(app, event);
    }
}

static void process_pending_clicks(PixelTermApp *app) {
    if (!app) {
        return;
    }

    // Process pending single click action (Single Image Mode).
    if (app->pending_single_click) {
        gint64 current_time = g_get_monotonic_time();
        if (current_time - app->pending_click_time > k_click_threshold_us) {
            app->pending_single_click = FALSE;
            app_next_image(app);
            if (app->needs_redraw) {
                app_refresh_display(app);
                app->needs_redraw = FALSE;
            }
        }
    }

    // Process pending single click action (Preview Grid Mode).
    if (app->pending_grid_single_click) {
        gint64 current_time = g_get_monotonic_time();
        if (current_time - app->pending_grid_click_time > k_click_threshold_us) {
            app->pending_grid_single_click = FALSE;
            gboolean redraw_needed = FALSE;
            gint old_selected = app->preview_selected;
            gint old_scroll = app->preview_scroll;
            app_handle_mouse_click_preview(app,
                                           app->pending_grid_click_x,
                                           app->pending_grid_click_y,
                                           &redraw_needed,
                                           NULL);
            if (redraw_needed) {
                if (app->preview_scroll != old_scroll) {
                    app_render_preview_grid(app);
                } else if (app->preview_selected != old_selected) {
                    app_render_preview_selection_change(app, old_selected);
                }
            }
        }
    }

    // Process pending single click action (File Manager Mode).
    if (app->file_manager_mode && app->pending_file_manager_single_click) {
        gint64 current_time = g_get_monotonic_time();
        if (current_time - app->pending_file_manager_click_time > k_click_threshold_us) {
            app->pending_file_manager_single_click = FALSE;
            gint old_selected = app->selected_entry;
            gint old_scroll = app->scroll_offset;
            app_handle_mouse_file_manager(app,
                                          app->pending_file_manager_click_x,
                                          app->pending_file_manager_click_y);
            if (app->selected_entry != old_selected || app->scroll_offset != old_scroll) {
                app_render_file_manager(app);
            }
        }
    } else if (!app->file_manager_mode && app->pending_file_manager_single_click) {
        app->pending_file_manager_single_click = FALSE;
    }
}

static void drain_main_context_if_playing(gboolean is_playing) {
    if (!is_playing) {
        return;
    }

    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
}

static void process_animation_events(PixelTermApp *app) {
    if (!app) {
        return;
    }

    drain_main_context_if_playing(app->gif_player && gif_player_is_playing(app->gif_player));
    drain_main_context_if_playing(app->video_player && video_player_is_playing(app->video_player));
}

static void handle_delete_current_image(PixelTermApp *app) {
    if (!app) {
        return;
    }

    ErrorCode err = app_delete_current_image(app);
    if (err != ERROR_NONE) {
        app_render_by_mode(app);
        return;
    }

    if (!app_has_images(app)) {
        app->needs_screen_clear = TRUE;
        if (app_enter_file_manager(app) == ERROR_NONE) {
            app_render_file_manager(app);
        } else {
            app_refresh_display(app);
        }
        return;
    }

    app_render_by_mode(app);
}

static void handle_toggle_video_fps(PixelTermApp *app) {
    if (!app || !app_current_is_video(app) || !app->video_player) {
        return;
    }
    app->show_fps = !app->show_fps;
    app->video_player->show_stats = app->show_fps && !app->ui_text_hidden;
    if (!app->show_fps && !app->ui_text_hidden) {
        gint stats_row = 4;
        if (stats_row >= 1 && stats_row <= app->term_height) {
            gboolean restored_line = FALSE;
            VideoPlayer *player = app->video_player;
            if (player->last_frame_lines && player->last_frame_height > 0) {
                gint line_index = stats_row - player->last_frame_top_row;
                if (line_index >= 0 && line_index < (gint)player->last_frame_lines->len) {
                    const gchar *line = g_ptr_array_index(player->last_frame_lines, line_index);
                    printf("\033[%d;1H\033[2K", stats_row);
                    if (line) {
                        fwrite(line, 1, strlen(line), stdout);
                    }
                    restored_line = TRUE;
                }
            }
            if (!restored_line) {
                printf("\033[%d;1H\033[2K", stats_row);
            }
            fflush(stdout);
        }
    }
}

static void handle_video_scale_change(PixelTermApp *app, gdouble delta) {
    if (!app || !app_current_is_video(app)) {
        return;
    }
    gdouble next_scale = app->video_scale + delta;
    if (next_scale < 0.3) {
        next_scale = 0.3;
    } else if (next_scale > 1.5) {
        next_scale = 1.5;
    }
    if (delta > 0.0) {
        gint base_w = app->term_width > 0 ? app->term_width : 80;
        gint base_h = app->term_height > 0 ? app->term_height : 24;
        if (app->info_visible) {
            base_h -= 10;
        } else {
            base_h -= 6;
        }
        if (base_h < 1) {
            base_h = 1;
        }
        gint scaled_w = (gint)(base_w * next_scale + 0.5);
        gint scaled_h = (gint)(base_h * next_scale + 0.5);
        if (scaled_w > base_w || scaled_h > base_h) {
            next_scale = app->video_scale;
        }
    }
    if (next_scale == app->video_scale) {
        return;
    }
    app->video_scale = next_scale;
    if (app->video_player) {
        video_player_stop(app->video_player);
    }
    app_render_current_image(app);
    if (app->video_player) {
        video_player_play(app->video_player);
    }
}

static void handle_video_protocol_toggle(PixelTermApp *app) {
    if (!app || !app_current_is_video(app) || !app->video_player || !app->video_player->renderer) {
        return;
    }

    gboolean was_playing = video_player_is_playing(app->video_player);
    if (was_playing) {
        video_player_stop(app->video_player);
    }

    g_mutex_lock(&app->video_player->render_mutex);
    gboolean force_text = app->video_player->renderer->config.force_text;
    gboolean force_kitty = app->video_player->renderer->config.force_kitty;
    gboolean force_iterm2 = app->video_player->renderer->config.force_iterm2;
    gboolean force_sixel = app->video_player->renderer->config.force_sixel;
    ChafaPixelMode current_mode = CHAFA_PIXEL_MODE_SYMBOLS;
    if (app->video_player->renderer->canvas_config) {
        current_mode = chafa_canvas_config_get_pixel_mode(app->video_player->renderer->canvas_config);
    }
    gboolean was_text = force_text || current_mode == CHAFA_PIXEL_MODE_SYMBOLS;
    gboolean next_text = FALSE;
    gboolean next_kitty = FALSE;
    gboolean next_iterm2 = FALSE;
    gboolean next_sixel = FALSE;

    if (force_text) {
        next_kitty = TRUE;
    } else if (force_kitty) {
        next_iterm2 = TRUE;
    } else if (force_iterm2) {
        next_sixel = TRUE;
    } else if (force_sixel) {
        next_text = TRUE;
    } else {
        next_kitty = TRUE;
    }

    app->video_player->renderer->config.force_text = next_text;
    app->video_player->renderer->config.force_kitty = next_kitty;
    app->video_player->renderer->config.force_iterm2 = next_iterm2;
    app->video_player->renderer->config.force_sixel = next_sixel;
    gboolean next_graphics = next_kitty || next_iterm2 || next_sixel;
    gboolean should_clear = was_text && next_graphics;
    renderer_update_terminal_size(app->video_player->renderer);
    g_mutex_unlock(&app->video_player->render_mutex);

    if (should_clear) {
        video_player_clear_render_area(app->video_player);
    }

    if (was_playing) {
        video_player_play(app->video_player);
    }
}

static gboolean handle_key_press_common(PixelTermApp *app,
                                        InputHandler *input_handler,
                                        const InputEvent *event) {
    switch (event->key_code) {
        case KEY_ESCAPE:
            app->running = FALSE;
            input_handler->should_exit = TRUE;
            return TRUE;
        case (KeyCode)'d':
        case (KeyCode)'D':
            app->dither_enabled = !app->dither_enabled;
            if (app->preloader) {
                preloader_stop(app->preloader);
                preloader_cache_clear(app->preloader);
                preloader_initialize(app->preloader, app->dither_enabled, app->render_work_factor,
                                     app->force_text, app->force_sixel, app->force_kitty, app->force_iterm2,
                                     app->gamma);
                preloader_start(app->preloader);
            }
            app_render_by_mode(app);
            return TRUE;
        case (KeyCode)'i':
            if (app_current_is_video(app)) {
                return TRUE;
            }
            if (!app->preview_mode) {
                if (app->ui_text_hidden) {
                    return TRUE;
                }
                if (app->info_visible) {
                    app->info_visible = FALSE;
                    app_render_current_image(app);
                } else {
                    app_display_image_info(app);
                }
            }
            return TRUE;
        case (KeyCode)'f':
        case (KeyCode)'F':
            handle_toggle_video_fps(app);
            return TRUE;
        case (KeyCode)'~':
        case (KeyCode)'`':
            if (!app->file_manager_mode) {
                gboolean info_was_visible = app->info_visible;
                app->ui_text_hidden = !app->ui_text_hidden;
                if (app->ui_text_hidden) {
                    app->info_visible = FALSE;
                }
                if (app->preview_mode) {
                    app->needs_screen_clear = TRUE;
                } else if (!info_was_visible) {
                    app->suppress_full_clear = TRUE;
                }
                app_render_by_mode(app);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

static void handle_key_press_preview(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    switch (event->key_code) {
        case KEY_LEFT:
        case (KeyCode)'h': {
            gint old_selected = app->preview_selected;
            gint old_scroll = app->preview_scroll;
            app_preview_move_selection(app, 0, -1);
            if (app->preview_scroll != old_scroll) {
                app_render_preview_grid(app);
            } else if (app->preview_selected != old_selected) {
                app_render_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case KEY_RIGHT:
        case (KeyCode)'l': {
            gint old_selected = app->preview_selected;
            gint old_scroll = app->preview_scroll;
            app_preview_move_selection(app, 0, 1);
            if (app->preview_scroll != old_scroll) {
                app_render_preview_grid(app);
            } else if (app->preview_selected != old_selected) {
                app_render_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case (KeyCode)'k':
        case KEY_UP: {
            gint old_selected = app->preview_selected;
            gint old_scroll = app->preview_scroll;
            app_preview_move_selection(app, -1, 0);
            if (app->preview_scroll != old_scroll) {
                app_render_preview_grid(app);
            } else if (app->preview_selected != old_selected) {
                app_render_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case (KeyCode)'j':
        case KEY_DOWN: {
            gint old_selected = app->preview_selected;
            gint old_scroll = app->preview_scroll;
            app_preview_move_selection(app, 1, 0);
            if (app->preview_scroll != old_scroll) {
                app_render_preview_grid(app);
            } else if (app->preview_selected != old_selected) {
                app_render_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case KEY_PAGE_DOWN: {
            gint old_selected = app->preview_selected;
            gint old_scroll = app->preview_scroll;
            app_preview_page_move(app, 1);
            if (app->preview_scroll != old_scroll) {
                app_render_preview_grid(app);
            } else if (app->preview_selected != old_selected) {
                app_render_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        }
        case KEY_PAGE_UP: {
            gint old_selected = app->preview_selected;
            gint old_scroll = app->preview_scroll;
            app_preview_page_move(app, -1);
            if (app->preview_scroll != old_scroll) {
                app_render_preview_grid(app);
            } else if (app->preview_selected != old_selected) {
                app_render_preview_selection_change(app, old_selected);
            }
            skip_queued_navigation(input_handler, g_nav_keys_page, G_N_ELEMENTS(g_nav_keys_page));
            break;
        }
        case (KeyCode)'+':
        case (KeyCode)'=':
            app_preview_change_zoom(app, 1);
            break;
        case (KeyCode)'-':
            app_preview_change_zoom(app, -1);
            break;
        case KEY_TAB:
            if (app->return_to_mode == RETURN_MODE_PREVIEW) {
                app_exit_preview(app, TRUE);
                app_refresh_display(app);
            } else {
                ReturnMode saved_return_mode = app->return_to_mode;
                app->return_to_mode = RETURN_MODE_PREVIEW;
                app_exit_preview(app, TRUE);
                app_enter_file_manager(app);
                if (saved_return_mode == RETURN_MODE_PREVIEW_VIRTUAL && app->previous_selected_entry >= 0) {
                    app->selected_entry = app->previous_selected_entry;
                    app->previous_selected_entry = -1;
                }
                app_render_file_manager(app);
            }
            break;
        case KEY_ENTER:
        case 13:
            if (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) {
                app->return_to_mode = RETURN_MODE_PREVIEW;
            }
            app_exit_preview(app, TRUE);
            app_refresh_display(app);
            break;
        default:
            break;
    }
}

static void handle_key_press_file_manager(PixelTermApp *app,
                                          InputHandler *input_handler,
                                          const InputEvent *event) {
    if ((event->key_code >= 'A' && event->key_code <= 'Z') ||
        (event->key_code >= 'a' && event->key_code <= 'z')) {
        gint old_selected = app->selected_entry;
        gint old_scroll = app->scroll_offset;
        app_file_manager_jump_to_letter(app, (char)event->key_code);
        if (app->selected_entry != old_selected || app->scroll_offset != old_scroll) {
            app_render_file_manager(app);
        }
        return;
    }

    switch (event->key_code) {
        case KEY_LEFT:
        case (KeyCode)'h': {
            gint old_selected = app->selected_entry;
            gint old_scroll = app->scroll_offset;
            GList *old_entries = app->directory_entries;
            gchar *old_dir = app->file_manager_directory ? g_strdup(app->file_manager_directory) : NULL;
            ErrorCode err = app_file_manager_left(app);
            gboolean dir_changed = (g_strcmp0(old_dir, app->file_manager_directory) != 0);
            gboolean state_changed = dir_changed ||
                                     (app->directory_entries != old_entries) ||
                                     (app->selected_entry != old_selected) ||
                                     (app->scroll_offset != old_scroll);
            g_free(old_dir);
            if (err == ERROR_NONE && state_changed) {
                app_render_file_manager(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case KEY_RIGHT:
        case (KeyCode)'l': {
            gint old_selected = app->selected_entry;
            gint old_scroll = app->scroll_offset;
            GList *old_entries = app->directory_entries;
            gchar *old_dir = app->file_manager_directory ? g_strdup(app->file_manager_directory) : NULL;
            ErrorCode err = app_file_manager_right(app);
            gboolean dir_changed = (g_strcmp0(old_dir, app->file_manager_directory) != 0);
            gboolean state_changed = dir_changed ||
                                     (app->directory_entries != old_entries) ||
                                     (app->selected_entry != old_selected) ||
                                     (app->scroll_offset != old_scroll);
            g_free(old_dir);
            if (err == ERROR_NONE && app->file_manager_mode) {
                if (state_changed) {
                    app_render_file_manager(app);
                }
            } else if (app->file_manager_mode) {
                app_render_file_manager(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case (KeyCode)'k':
        case KEY_UP: {
            gint old_selected = app->selected_entry;
            gint old_scroll = app->scroll_offset;
            app_file_manager_up(app);
            if (app->selected_entry != old_selected || app->scroll_offset != old_scroll) {
                app_render_file_manager(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case (KeyCode)'j':
        case KEY_DOWN: {
            gint old_selected = app->selected_entry;
            gint old_scroll = app->scroll_offset;
            app_file_manager_down(app);
            if (app->selected_entry != old_selected || app->scroll_offset != old_scroll) {
                app_render_file_manager(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case KEY_TAB: {
            if (!app_file_manager_has_images(app)) {
                break;
            }

            ErrorCode load_error = app_load_directory(app, app->file_manager_directory);
            if (load_error != ERROR_NONE) {
                app_render_file_manager(app);
                break;
            }

            if (app_file_manager_selection_is_image(app)) {
                app->return_to_mode = RETURN_MODE_PREVIEW;
                gint selected_image_index = app_file_manager_get_selected_image_index(app);
                if (selected_image_index >= 0) {
                    app->current_index = selected_image_index;
                }
                app_exit_file_manager(app);
                if (app_enter_preview(app) == ERROR_NONE) {
                    app_render_preview_grid(app);
                } else {
                    app_refresh_display(app);
                }
            } else {
                app->return_to_mode = RETURN_MODE_PREVIEW_VIRTUAL;
                app->previous_selected_entry = app->selected_entry;
                app_exit_file_manager(app);
                if (app_enter_preview(app) == ERROR_NONE) {
                    app->preview_selected = 0;
                    app_render_preview_grid(app);
                } else {
                    app_refresh_display(app);
                }
            }
            break;
        }
        case KEY_ENTER:
        case 13:
            {
                ErrorCode error;
                input_flush_buffer(input_handler);
                error = app_file_manager_enter(app);
                if (error != ERROR_NONE) {
                    app_render_file_manager(app);
                } else if (app->file_manager_mode) {
                    app_render_file_manager(app);
                }
            }
            break;
        case KEY_BACKSPACE:
        case 8:
            if (app_file_manager_toggle_hidden(app) == ERROR_NONE) {
                app_render_file_manager(app);
            }
            break;
        default:
            break;
    }
}

static void handle_key_press_single(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    switch (event->key_code) {
        case (KeyCode)' ':
            app_toggle_video_playback(app);
            break;
        case (KeyCode)'+':
        case (KeyCode)'=':
            handle_video_scale_change(app, 0.1);
            break;
        case (KeyCode)'-':
            handle_video_scale_change(app, -0.1);
            break;
        case (KeyCode)'p':
        case (KeyCode)'P':
            {
                gint64 now_us = g_get_monotonic_time();
                if (g_last_protocol_toggle_us > 0 &&
                    (now_us - g_last_protocol_toggle_us) < k_protocol_toggle_debounce_us) {
                    break;
                }
                g_last_protocol_toggle_us = now_us;
                handle_video_protocol_toggle(app);
            }
            break;
        case KEY_LEFT:
        case (KeyCode)'h': {
            gint old_index = app_get_current_index(app);
            app_previous_image(app);
            if (old_index != app_get_current_index(app)) {
                app_refresh_display(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case KEY_RIGHT:
        case (KeyCode)'l': {
            gint old_index = app_get_current_index(app);
            app_next_image(app);
            if (old_index != app_get_current_index(app)) {
                app_refresh_display(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_lr, G_N_ELEMENTS(g_nav_keys_lr));
            break;
        }
        case (KeyCode)'k':
        case KEY_UP: {
            gint old_index = app_get_current_index(app);
            app_previous_image(app);
            if (old_index != app_get_current_index(app)) {
                app_refresh_display(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case (KeyCode)'j':
        case KEY_DOWN: {
            gint old_index = app_get_current_index(app);
            app_next_image(app);
            if (old_index != app_get_current_index(app)) {
                app_refresh_display(app);
            }
            skip_queued_navigation(input_handler, g_nav_keys_ud, G_N_ELEMENTS(g_nav_keys_ud));
            break;
        }
        case KEY_TAB:
            app->return_to_mode = RETURN_MODE_SINGLE;
            app_enter_file_manager(app);
            app_render_file_manager(app);
            break;
        case KEY_ENTER:
        case 13:
            if (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) {
                app->return_to_mode = RETURN_MODE_PREVIEW;
            }
            if (app_enter_preview(app) == ERROR_NONE) {
                app_render_preview_grid(app);
            }
            break;
        default:
            break;
    }
}

static void handle_key_press(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    if (handle_delete_request(app, event)) {
        return;
    }
    if (handle_key_press_common(app, input_handler, event)) {
        return;
    }
    if (app->preview_mode) {
        handle_key_press_preview(app, input_handler, event);
    } else if (app->file_manager_mode) {
        handle_key_press_file_manager(app, input_handler, event);
    } else {
        handle_key_press_single(app, input_handler, event);
    }
}

static void handle_input_event(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    switch (event->type) {
        case INPUT_MOUSE_PRESS:
            handle_mouse_press(app, event);
            break;
        case INPUT_MOUSE_DOUBLE_CLICK:
            handle_mouse_double_click(app, event);
            break;
        case INPUT_MOUSE_SCROLL:
            handle_mouse_scroll(app, event);
            break;
        case INPUT_KEY_PRESS:
            handle_key_press(app, input_handler, event);
            break;
        default:
            break;
    }
}

// Main application loop
static ErrorCode run_application(PixelTermApp *app, gboolean alt_screen_enabled) {
    InputHandler *input_handler = input_handler_create();
    if (!input_handler) {
        return ERROR_MEMORY_ALLOC;
    }
    input_handler->use_alt_screen = alt_screen_enabled;

    ErrorCode error = ERROR_NONE;

    error = input_handler_initialize(input_handler);
    if (error != ERROR_NONE) {
        input_handler_destroy(input_handler);
        return error;
    }

    error = input_enable_raw_mode(input_handler);
    if (error != ERROR_NONE) {
        input_handler_destroy(input_handler);
        return error;
    }

    error = input_enable_mouse(input_handler);
    if (error != ERROR_NONE) {
        input_disable_raw_mode(input_handler);
        input_handler_destroy(input_handler);
        return error;
    }

    // Drop any pending bytes (some terminals can leave an ESC or other bytes queued)
    // which would otherwise immediately trigger an action like exit.
    input_flush_buffer(input_handler);

    // Initial render
    error = app_render_by_mode(app);
    if (error != ERROR_NONE) {
        input_disable_raw_mode(input_handler);
        input_handler_destroy(input_handler);
        return error;
    }

    // Main event loop
    InputEvent event;
    static gint last_term_width = 0, last_term_height = 0;
    // Initialize terminal size tracking
    last_term_width = input_handler->terminal_width;
    last_term_height = input_handler->terminal_height;
        
    while (app->running && !input_handler->should_exit) {
        if (g_terminate_requested) {
            app->running = FALSE;
            input_handler->should_exit = TRUE;
            break;
        }
        
        // Check for terminal size changes
        input_update_terminal_size(input_handler);
        if (last_term_width != input_handler->terminal_width || 
            last_term_height != input_handler->terminal_height) {
            // Terminal size changed, update tracking and refresh
            last_term_width = input_handler->terminal_width;
            last_term_height = input_handler->terminal_height;
            get_terminal_size(&app->term_width, &app->term_height);

            app_pause_video_for_resize(app);
            if (app->preview_mode) {
                app->needs_screen_clear = TRUE;
            }
            app_render_by_mode(app);
            usleep(k_resize_sleep_us);
            continue;
        }
        
        process_pending_clicks(app);
        process_animation_events(app);

        
        // Check if we have pending input with timeout to allow signal checking
        if (!input_has_pending_input(input_handler)) {
            usleep(k_input_poll_sleep_us);
            continue;
        }

        error = input_get_event(input_handler, &event);
        if (error != ERROR_NONE) {
            break;
        }

        handle_input_event(app, input_handler, &event);
    }

    // Cleanup - Reset terminal state before exiting
    printf("\033[2J\033[H\033[0m"); // Clear screen and reset attributes
    printf("\033[?25h"); // Show cursor
    fflush(stdout);

    input_disable_mouse(input_handler);
    input_disable_raw_mode(input_handler);
    input_handler_destroy(input_handler);

    return error;
}

int main(int argc, char *argv[]) {
    // Set locale for proper character handling
    setlocale(LC_ALL, "");

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Parse command line arguments
    char *path = NULL;
    gboolean preload_enabled = TRUE;
    gboolean dither_enabled = FALSE;
    gboolean alt_screen_enabled = TRUE;
    gboolean clear_workaround_enabled = FALSE;
    gint work_factor = 9;
    gchar *protocol = NULL;
    gdouble gamma = 1.0;
    gboolean gamma_set = FALSE;
    
    ErrorCode error = parse_arguments(argc, argv, &path, &preload_enabled, &dither_enabled,
                                      &alt_screen_enabled, &clear_workaround_enabled, &work_factor,
                                      &protocol, &gamma, &gamma_set);
    if (error != ERROR_NONE) {
        if (path) g_free(path);
        g_free(protocol);
        if (error == ERROR_HELP_EXIT || error == ERROR_VERSION_EXIT) {
            return 0;
        }
        return 1;
    }
    
    // If no path provided, use current directory
    if (!path) {
        path = g_get_current_dir();
    }

    if (protocol && *protocol) {
        if (strcmp(protocol, "auto") == 0) {
            // Fall through to auto detection below.
        } else if (strcmp(protocol, "text") == 0) {
            g_force_text = TRUE;
        } else if (strcmp(protocol, "sixel") == 0) {
            g_force_sixel = TRUE;
        } else if (strcmp(protocol, "kitty") == 0) {
            g_force_kitty = TRUE;
        } else if (strcmp(protocol, "iterm2") == 0) {
            g_force_iterm2 = TRUE;
        } else {
            fprintf(stderr, "Unknown protocol: %s\n", protocol);
            g_free(protocol);
            if (path) g_free(path);
            return 1;
        }
    }
    if (!g_force_text && !g_force_kitty && !g_force_iterm2 && !g_force_sixel) {
        g_force_kitty = probe_kitty_support();
        if (g_force_kitty) {
            g_force_iterm2 = FALSE;
            g_force_sixel = FALSE;
        } else {
            g_force_iterm2 = probe_iterm2_support();
            if (g_force_iterm2) {
                g_force_sixel = FALSE;
            } else {
                g_force_sixel = probe_sixel_support();
            }
        }
    }
    if (g_force_text) {
        g_force_kitty = FALSE;
        g_force_iterm2 = FALSE;
        g_force_sixel = FALSE;
    }
#if defined(__linux__)
    if (!gamma_set && g_force_kitty && is_kitty_terminal_env()) {
        gamma = 0.5;
    }
#endif

    // Create and initialize application
    g_app = app_create();
    if (!g_app) {
        g_free(path);
        g_free(protocol);
        return 1;
    }
    g_app->force_sixel = g_force_sixel;
    g_app->force_kitty = g_force_kitty;
    g_app->force_iterm2 = g_force_iterm2;
    g_app->force_text = g_force_text;
    g_app->gamma = gamma;

    g_app->render_work_factor = work_factor;
    error = app_initialize(g_app, dither_enabled);
    if (error != ERROR_NONE) {
        fprintf(stderr, "Failed to initialize application: %d\n", error);
        app_destroy(g_app);
        g_free(path);
        g_free(protocol);
        return 1;
    }

    // Configure application settings
    g_app->preload_enabled = preload_enabled;
    g_app->clear_workaround_enabled = clear_workaround_enabled;
    g_free(protocol);

    // Validate and load path
    gboolean is_directory;
    error = validate_path(path, &is_directory);
    if (error != ERROR_NONE) {
        fprintf(stderr, "Error: Path '%s' not found or inaccessible\n", path);
        app_destroy(g_app);
        g_free(path);
        return 1;
    }

    if (is_directory) {
        error = app_load_directory(g_app, path);
        if (error == ERROR_NONE) {
            // Always start in file manager for directories
            error = app_enter_file_manager(g_app);
            if (error == ERROR_NONE) {
                app_render_file_manager(g_app);
            }
        }
    } else {
        // Check if the file is a valid media file first
        if (!is_valid_media_file(path)) {
            // If the file is not valid, load the directory and enter file manager
            gchar *directory = g_path_get_dirname(path);
            error = app_load_directory(g_app, directory);
            g_free(directory);

            if (error == ERROR_NONE) {
                // Always start in file manager
                error = app_enter_file_manager(g_app);
                if (error == ERROR_NONE) {
                    app_render_file_manager(g_app);
                }
            }
        } else {
            // Load the single valid file as normal
            error = app_load_single_file(g_app, path);
        }
    }

    if (error != ERROR_NONE) {
        fprintf(stderr, "Error: Failed to load images from '%s'\n", path);
        app_destroy(g_app);
        g_free(path);
        return 1;
    }

    if (!app_has_images(g_app)) {
        if (is_directory) {
            // Directory provided but no images: go to file manager in that directory
            error = app_enter_file_manager(g_app);
            if (error != ERROR_NONE) {
                fprintf(stderr, "Failed to start file manager: %d\n", error);
                app_destroy(g_app);
                g_free(path);
                return 1;
            }
            app_render_file_manager(g_app);
            error = run_application(g_app, alt_screen_enabled);
            app_destroy(g_app);
            g_free(path);
            g_app = NULL;
            if (g_terminate_requested) {
                if (g_last_signal == SIGINT) return 130;
                if (g_last_signal == SIGTERM) return 143;
                return 1;
            }
            return error == ERROR_NONE ? 0 : 1;
        } else {
            // No path provided and no images: start in file manager mode for browsing
            error = app_enter_file_manager(g_app);
            if (error != ERROR_NONE) {
                fprintf(stderr, "Failed to start file manager: %d\n", error);
                app_destroy(g_app);
                g_free(path);
                return 1;
            }
            // Continue into main loop with file manager active
            error = run_application(g_app, alt_screen_enabled);
            app_destroy(g_app);
            g_free(path);
            g_app = NULL;
            if (g_terminate_requested) {
                if (g_last_signal == SIGINT) return 130;
                if (g_last_signal == SIGTERM) return 143;
                return 1;
            }
            return error == ERROR_NONE ? 0 : 1;
        }
    }

    // Run main application loop
    error = run_application(g_app, alt_screen_enabled);
    if (error != ERROR_NONE) {
        fprintf(stderr, "Application error: %d (%s)\n", error, 
                error_code_to_string(error));
    }

    // Cleanup
    app_destroy(g_app);
    g_free(path);
    g_app = NULL;

    if (g_terminate_requested) {
        if (g_last_signal == SIGINT) return 130;
        if (g_last_signal == SIGTERM) return 143;
        return 1;
    }
    return error == ERROR_NONE ? 0 : 1;
}
