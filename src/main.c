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
#include "app_cli.h"
#include "input.h"
#include "input_dispatch.h"
#include "common.h"

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
    if (input_handler->alt_screen_enabled) {
        app->suppress_full_clear = TRUE;
    }
    error = app_render_by_mode(app);
    if (app->suppress_full_clear) {
        app->suppress_full_clear = FALSE;
    }
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

            input_dispatch_pause_video_for_resize(app);
            if (app_is_preview_mode(app)) {
                app->needs_screen_clear = TRUE;
            }
            app_render_by_mode(app);
            usleep(k_resize_sleep_us);
            continue;
        }
        
        input_dispatch_process_pending(app);
        input_dispatch_process_animations(app);
        app_process_async_render(app);

        
        // Check if we have pending input with timeout to allow signal checking
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
    AppConfig config;
    app_config_init(&config);
    
    ErrorCode error = app_parse_arguments(argc, argv, &path, &config);
    if (error != ERROR_NONE) {
        if (path) g_free(path);
        if (error == ERROR_HELP_EXIT || error == ERROR_VERSION_EXIT) {
            return 0;
        }
        return 1;
    }
    
    // If no path provided, use current directory
    if (!path) {
        path = g_get_current_dir();
    }

    app_config_resolve_protocol(&config);

    // Create and initialize application
    g_app = app_create();
    if (!g_app) {
        g_free(path);
        return 1;
    }
    g_app->force_sixel = config.force_sixel;
    g_app->force_kitty = config.force_kitty;
    g_app->force_iterm2 = config.force_iterm2;
    g_app->force_text = config.force_text;
    g_app->gamma = config.gamma;

    g_app->render_work_factor = config.work_factor;
    error = app_initialize(g_app, config.dither_enabled);
    if (error != ERROR_NONE) {
        fprintf(stderr, "Failed to initialize application: %d\n", error);
        app_destroy(g_app);
        g_free(path);
        return 1;
    }

    // Configure application settings
    g_app->preload_enabled = config.preload_enabled;
    g_app->clear_workaround_enabled = config.clear_workaround_enabled;

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
        }
    } else {
        if (is_valid_book_file(path)) {
            error = app_open_book(g_app, path);
            if (error == ERROR_NONE) {
                error = app_enter_book_page(g_app, 0);
            }
        } else if (!is_valid_media_file(path)) {
            // If the file is not valid, load the directory and enter file manager
            gchar *directory = g_path_get_dirname(path);
            error = app_load_directory(g_app, directory);
            g_free(directory);

            if (error == ERROR_NONE) {
                // Always start in file manager
                error = app_enter_file_manager(g_app);
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

    if (!app_has_images(g_app) && !app_is_book_mode(g_app) && !app_is_book_preview_mode(g_app)) {
        if (is_directory) {
            // Directory provided but no images: go to file manager in that directory
            error = app_enter_file_manager(g_app);
            if (error != ERROR_NONE) {
                fprintf(stderr, "Failed to start file manager: %d\n", error);
                app_destroy(g_app);
                g_free(path);
                return 1;
            }
            error = run_application(g_app, config.alt_screen_enabled);
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
            error = run_application(g_app, config.alt_screen_enabled);
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
    error = run_application(g_app, config.alt_screen_enabled);
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
