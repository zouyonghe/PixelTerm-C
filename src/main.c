#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <locale.h>
#include <unistd.h>

#include "app.h"
#include "input.h"
#include "common.h"

// Global application instance
static PixelTermApp *g_app = NULL;

// Signal handler for graceful shutdown
static void signal_handler(int sig) {
    (void)sig; // Suppress unused parameter warning
    if (g_app) {
        g_app->running = FALSE;
    }
}

// Print usage information
static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS] [PATH]\n", program_name);
    printf("\n");
    printf("A high-performance terminal image browser written in C.\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  PATH    Path to an image file or directory containing images\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --version  Show version information\n");
    printf("  -i, --info     Start with image information visible\n");
    printf("  -p, --preload  Enable image preloading (default: enabled)\n");
    printf("  --no-preload   Disable image preloading\n");
    printf("\n");
    printf("Key bindings:\n");
    printf("  ←/→ or a/d     Previous/Next image\n");
    printf("  i              Show image information\n");
    printf("  r              Delete current image\n");
    printf("  q              Quit application\n");
    printf("  Ctrl+C         Force exit\n");
    printf("\n");
    printf("Supported formats: ");
    for (int i = 0; SUPPORTED_EXTENSIONS[i] != NULL; i++) {
        printf("%s", SUPPORTED_EXTENSIONS[i]);
        if (SUPPORTED_EXTENSIONS[i + 1] != NULL) {
            printf(", ");
        }
    }
    printf("\n");
}

// Print version information
static void print_version(void) {
    printf("%s version %s\n", APP_NAME, APP_VERSION);
    printf("Built with Chafa library support\n");
    printf("Performance: 5-10x faster than Python version\n");
}

// Parse command line arguments
static ErrorCode parse_arguments(int argc, char *argv[], char **path, gboolean *show_info, gboolean *preload_enabled) {
    static struct option long_options[] = {
        {"help",      no_argument,       0, 'h'},
        {"version",   no_argument,       0, 'v'},
        {"info",      no_argument,       0, 'i'},
        {"preload",   no_argument,       0, 'p'},
        {"no-preload", no_argument,      0, 1000},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "hvip", long_options, NULL)) != -1) {
        switch (c) {
            case 'h':
                print_usage(argv[0]);
                return ERROR_NONE;
            case 'v':
                print_version();
                return ERROR_NONE;
            case 'i':
                *show_info = TRUE;
                break;
            case 'p':
                *preload_enabled = TRUE;
                break;
            case 1000:  // --no-preload
                *preload_enabled = FALSE;
                break;
            case '?':
                return ERROR_INVALID_IMAGE;
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

// Main application loop
static ErrorCode run_application(PixelTermApp *app) {
    InputHandler *input_handler = input_handler_create();
    if (!input_handler) {
        return ERROR_MEMORY_ALLOC;
    }

    ErrorCode error = input_handler_initialize(input_handler);
    if (error != ERROR_NONE) {
        input_handler_destroy(input_handler);
        return error;
    }

    error = input_enable_raw_mode(input_handler);
    if (error != ERROR_NONE) {
        input_handler_destroy(input_handler);
        return error;
    }

    // Initial render
    error = app_refresh_display(app);
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
        // Check for terminal size changes
        input_update_terminal_size(input_handler);
        if (last_term_width != input_handler->terminal_width || 
            last_term_height != input_handler->terminal_height) {
            // Terminal size changed, update tracking and refresh
            last_term_width = input_handler->terminal_width;
            last_term_height = input_handler->terminal_height;
            app_refresh_display(app);
            usleep(100000); // 100ms delay
            continue;
        }
        
        // Check if we have pending input with timeout to allow signal checking
        if (!input_has_pending_input(input_handler)) {
            // Small delay to allow signal handling without busy waiting
            usleep(50000); // 50ms
            continue;
        }
        
        error = input_get_event(input_handler, &event);
        if (error != ERROR_NONE) {
            break;
        }

        // Process input events
        switch (event.type) {
            case INPUT_KEY_PRESS:
                switch (event.key_code) {
                    case KEY_LEFT:
                    case (KeyCode)'a':
                        app_previous_image(app);
                        app_refresh_display(app);
                        break;
                    case KEY_RIGHT:
                    case (KeyCode)'d':
                        app_next_image(app);
                        app_refresh_display(app);
                        break;
                    case (KeyCode)'i':
                        app_display_image_info(app);
                        break;
                    case (KeyCode)'r':
                        app_delete_current_image(app);
                        app_refresh_display(app);
                        break;
                    case (KeyCode)'q':
                    case KEY_ESCAPE:
                        app->running = FALSE;
                        break;
                    default:
                        break;
                }
                break;
            case INPUT_RESIZE:
                input_update_terminal_size(input_handler);
                app_refresh_display(app);
                break;
            default:
                break;
        }
    }

    // Cleanup
    input_disable_raw_mode(input_handler);
    input_handler_destroy(input_handler);

    return ERROR_NONE;
}

int main(int argc, char *argv[]) {
    // Set locale for proper character handling
    setlocale(LC_ALL, "");

    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Parse command line arguments
    char *path = NULL;
    gboolean show_info = FALSE;
    gboolean preload_enabled = TRUE;
    
    ErrorCode error = parse_arguments(argc, argv, &path, &show_info, &preload_enabled);
    if (error != ERROR_NONE || !path) {
        if (path) g_free(path);
        return error == ERROR_NONE ? 0 : 1;
    }

    // Create and initialize application
    g_app = app_create();
    if (!g_app) {
        g_free(path);
        return 1;
    }

    error = app_initialize(g_app);
    if (error != ERROR_NONE) {
        fprintf(stderr, "Failed to initialize application: %d\n", error);
        app_destroy(g_app);
        g_free(path);
        return 1;
    }

    // Configure application settings
    g_app->show_info = show_info;
    g_app->preload_enabled = preload_enabled;

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
    } else {
        error = app_load_single_file(g_app, path);
    }

    if (error != ERROR_NONE) {
        fprintf(stderr, "Error: Failed to load images from '%s'\n", path);
        app_destroy(g_app);
        g_free(path);
        return 1;
    }

    if (!app_has_images(g_app)) {
        printf("No supported image files found in '%s'\n", path);
        printf("Total images loaded: %d\n", app_get_total_images(g_app));
        app_destroy(g_app);
        g_free(path);
        return 0;
    }

    // Run main application loop
    error = run_application(g_app);
    if (error != ERROR_NONE) {
        fprintf(stderr, "Application error: %d (%s)\n", error, 
                error_code_to_string(error));
    }

    // Cleanup
    app_destroy(g_app);
    g_free(path);
    g_app = NULL;

    return error == ERROR_NONE ? 0 : 1;
}