#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <unistd.h>
#include <math.h>

#include <glib.h>

#include "app.h"
#include "app_startup.h"
#include "app_cli.h"
#include "app_config_runtime.h"
#include "input.h"
#include "input_dispatch.h"
#include "common.h"
#include "terminal_output.h"
#include "media_utils.h"
#include "terminal_probe.h"
#include "ui_render_utils.h"
#include "text_utils.h"

// Global application instance
static PixelTermApp *g_app = NULL;
static volatile sig_atomic_t g_terminate_requested = 0;
static volatile sig_atomic_t g_last_signal = 0;
static const useconds_t k_input_poll_sleep_us = 10000;
static const useconds_t k_resize_sleep_us = 100000;


// Signal handler for graceful shutdown
static void signal_handler(int sig) {
    (void)sig; // Suppress unused parameter warning
    g_terminate_requested = 1;
    g_last_signal = sig;
}

static ErrorCode run_application_cleanup(InputHandler *input_handler) {
    if (!input_handler) {
        return ERROR_NONE;
    }

    terminal_probe_retry_restore();
    const char clear[] = "\033[2J\033[H\033[0m";
    gboolean cleared = terminal_output_write(clear, sizeof(clear) - 1);

    input_disable_mouse(input_handler);
    gboolean original_known = input_handler->has_orig_termios;
    ErrorCode restore_error = input_disable_raw_mode(input_handler);
    input_handler_destroy(input_handler);
    gboolean restored = terminal_probe_retry_restore();
    if (!restored || (!original_known && restore_error == ERROR_TERMINAL_SIZE)) {
        return ERROR_TERMINAL_SIZE;
    }
    gboolean output_restored = terminal_output_flush();
    return cleared && output_restored ? ERROR_NONE : ERROR_OUTPUT_IO;
}

static ErrorCode finish_application(InputHandler *input_handler, ErrorCode primary_error) {
    ErrorCode cleanup_error = run_application_cleanup(input_handler);
    if (primary_error != ERROR_NONE && cleanup_error != ERROR_NONE) {
        fprintf(stderr, "Terminal restoration failed: %s\n", error_code_to_string(cleanup_error));
    }
    return primary_error != ERROR_NONE ? primary_error : cleanup_error;
}

static ErrorCode run_application_loop(PixelTermApp *app, gboolean alt_screen_enabled) {
    if (!terminal_output_flush()) return ERROR_OUTPUT_IO;
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
        if (!input_handler->raw_mode_enabled) {
            input_handler_destroy(input_handler);
            return error;
        }
        return finish_application(input_handler, error);
    }

    error = input_enable_mouse(input_handler);
    if (error != ERROR_NONE) {
        return finish_application(input_handler, error);
    }

    // Initial render
    if (!terminal_output_flush()) {
        return finish_application(input_handler, ERROR_OUTPUT_IO);
    }
    if (input_handler->alt_screen_enabled) {
        app->suppress_full_clear = TRUE;
    }
    error = app_render_by_mode(app);
    if (app->suppress_full_clear) {
        app->suppress_full_clear = FALSE;
    }
    if (error != ERROR_NONE) {
        return finish_application(input_handler, error);
    }

    // Main event loop
    InputEvent event;
    static gint last_term_width = 0, last_term_height = 0;
    gboolean resize_pending = FALSE;
    gint64 output_blocked_since_us = 0;
    // Initialize terminal size tracking
    last_term_width = input_handler->terminal_width;
    last_term_height = input_handler->terminal_height;
        
    while (app->running && !input_handler->should_exit) {
        if (app->last_error != ERROR_NONE) break;
        if (!terminal_probe_retry_restore()) {
            error = ERROR_TERMINAL_SIZE;
            break;
        }
        if (g_terminate_requested) {
            app->running = FALSE;
            input_handler->should_exit = TRUE;
            break;
        }
        gint64 now_us = g_get_monotonic_time();
        if (!terminal_output_ready(now_us)) {
            if (!output_blocked_since_us) output_blocked_since_us = now_us;
            if (now_us - output_blocked_since_us >= 2 * G_TIME_SPAN_SECOND) {
                error = ERROR_OUTPUT_IO;
                break;
            }
            usleep(k_input_poll_sleep_us);
            continue;
        }
        output_blocked_since_us = 0;
        
        // Check for terminal size changes
        input_update_terminal_size(input_handler);
        if (last_term_width != input_handler->terminal_width || 
            last_term_height != input_handler->terminal_height) {
            // Wait for resize events to settle before repainting heavy image frames.
            last_term_width = input_handler->terminal_width;
            last_term_height = input_handler->terminal_height;
            app->term_width = last_term_width;
            app->term_height = last_term_height;

            if (!resize_pending) {
                const gchar *path = app_get_current_filepath(app);
                MediaKind kind = path ? media_classify(path) : MEDIA_KIND_IMAGE;
                app->preserve_media_pause_on_redraw =
                    (media_is_video(kind) && app->video_player &&
                     !video_player_is_playing(app->video_player)) ||
                    (media_is_animated_image(kind) && app->gif_player &&
                     !gif_player_is_playing(app->gif_player));
            }
            input_dispatch_pause_video_for_resize(app);
            if (app->gif_player && gif_player_is_playing(app->gif_player)) {
                error = gif_player_pause(app->gif_player);
                if (error != ERROR_NONE) break;
            }
            if (!terminal_output_ready(g_get_monotonic_time())) {
                resize_pending = TRUE;
                continue;
            }
            if (app_is_preview_mode(app)) {
                app->needs_screen_clear = TRUE;
            }
            if (!ui_clear_kitty_images(app)) {
                error = ERROR_OUTPUT_IO;
                break;
            }
            ui_clear_screen_for_refresh(app);
            if (!terminal_output_flush()) {
                error = ERROR_OUTPUT_IO;
                break;
            }
            resize_pending = TRUE;
            usleep(k_resize_sleep_us);
            continue;
        }

        if (resize_pending) {
            resize_pending = FALSE;
            gboolean resume_video_after_resize = app->video_was_playing_before_resize;
            app->video_was_playing_before_resize = FALSE;
            get_terminal_size(&app->term_width, &app->term_height);
            app->suppress_full_clear = FALSE;
            error = app_render_by_mode(app);
            app->preserve_media_pause_on_redraw = FALSE;
            if (error != ERROR_NONE) break;
            if (resume_video_after_resize && app->video_player) {
                error = video_player_play(app->video_player);
                if (error != ERROR_NONE) break;
            }
            continue;
        }
        
        gboolean handled_input = FALSE;

        input_dispatch_process_pending(app);
        if (app->last_error != ERROR_NONE) break;
        if (!terminal_output_ready(g_get_monotonic_time())) continue;

        if (input_has_pending_input(input_handler)) {
            error = input_get_event(input_handler, &event);
            if (error != ERROR_NONE) {
                break;
            }

            input_dispatch_handle_event(app, input_handler, &event);
            handled_input = TRUE;
        }

        if (app->last_error != ERROR_NONE) break;
        if (!terminal_output_ready(g_get_monotonic_time())) continue;
        input_dispatch_process_animations(app);
        if (app->last_error != ERROR_NONE) break;
        if (!terminal_output_ready(g_get_monotonic_time())) continue;
        app_process_async_render(app);
        if (app->last_error != ERROR_NONE) break;
        if (!terminal_output_ready(g_get_monotonic_time())) continue;

        if (handled_input) {
            continue;
        }

        if (!input_has_pending_input(input_handler)) {
            usleep(k_input_poll_sleep_us);
            continue;
        }

        error = input_get_event(input_handler, &event);
        if (error != ERROR_NONE) {
            break;
        }

        input_dispatch_handle_event(app, input_handler, &event);
    }

    if (error == ERROR_NONE) error = app->last_error;
    return finish_application(input_handler, error);
}

static ErrorCode run_application(PixelTermApp *app, gboolean alt_screen_enabled) {
    if (!terminal_output_begin_session()) return ERROR_OUTPUT_IO;
    ErrorCode error = run_application_loop(app, alt_screen_enabled);
    gboolean output_restored = terminal_output_end_session();
    return error != ERROR_NONE ? error : output_restored ? ERROR_NONE : ERROR_OUTPUT_IO;
}

int main(int argc, char *argv[]) {
    // Set locale for proper character handling
    setlocale(LC_ALL, "");

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    /* Ignore SIGPIPE so broken pipe writes return EPIPE instead of terminating. */
    signal(SIGPIPE, SIG_IGN);

    // Parse command line arguments
    char *path = NULL;
    AppConfig config;
    AppStartupPathDecision startup = {0};
    app_config_init(&config);
    
    ErrorCode error = app_parse_arguments(argc, argv, &path, &config);
    if (error != ERROR_NONE) {
        if (path) g_free(path);
        if (error == ERROR_HELP_EXIT || error == ERROR_VERSION_EXIT) {
            return 0;
        }
        return 1;
    }
    
    app_config_resolve_protocol(&config);

    // Create and initialize application
    g_app = app_create();
    if (!g_app) {
        g_free(path);
        return 1;
    }

    app_config_apply_runtime(g_app, &config);
    error = app_initialize(g_app, g_app->dither_enabled);
    if (error != ERROR_NONE) {
        fprintf(stderr, "Failed to initialize application: %s\n", error_code_to_string(error));
        app_destroy(g_app);
        g_free(path);
        return 1;
    }

    // Validate and classify startup path
    error = app_startup_classify_path(path, &startup);
    if (error != ERROR_NONE) {
        const char *display_path = path ? path : "current directory";
        gchar *safe_display_path = sanitize_for_terminal(display_path);
        if (startup.failure_errno != 0) {
            fprintf(stderr, "Error: Cannot open '%s': %s\n",
                    safe_display_path,
                    g_strerror(startup.failure_errno));
        } else if (error == ERROR_INVALID_IMAGE) {
            fprintf(stderr, "Unsupported file type: %s\n", safe_display_path);
        } else {
            fprintf(stderr, "Error: Path '%s' not found or inaccessible\n", safe_display_path);
        }
        g_free(safe_display_path);
        app_destroy(g_app);
        g_free(path);
        return 1;
    }

    g_free(path);
    path = startup.path;

    if (startup.kind == APP_STARTUP_PATH_DIRECTORY) {
        error = app_load_directory(g_app, path);
        if (error == ERROR_NONE) {
            // Always start in file manager for directories
            error = app_enter_file_manager(g_app);
        }
    } else {
        if (startup.kind == APP_STARTUP_PATH_BOOK) {
            error = app_open_book(g_app, path);
            if (error == ERROR_NONE) {
                error = app_enter_book_page(g_app, 0);
            }
        } else {
            // Load the single valid file as normal
            error = app_load_single_file(g_app, path);
        }
    }

    if (error != ERROR_NONE) {
        gchar *safe_path = sanitize_for_terminal(path);
        fprintf(stderr, "Error: Failed to load images from '%s'\n", safe_path);
        g_free(safe_path);
        app_destroy(g_app);
        g_free(path);
        return 1;
    }

    if (!app_has_images(g_app) && !app_is_book_mode(g_app) && !app_is_book_preview_mode(g_app)) {
        // No displayable content: start in file manager mode for browsing
        error = app_enter_file_manager(g_app);
        if (error != ERROR_NONE) {
            fprintf(stderr, "Failed to start file manager: %s\n", error_code_to_string(error));
            app_destroy(g_app);
            g_free(path);
            return 1;
        }
        error = run_application(g_app, config.alt_screen_enabled);
        if (error != ERROR_NONE) {
            fprintf(stderr, "Application error: %s\n", error_code_to_string(error));
        }
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

    // Run main application loop
    error = run_application(g_app, config.alt_screen_enabled);
    if (error != ERROR_NONE) {
        fprintf(stderr, "Application error: %s\n", error_code_to_string(error));
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
