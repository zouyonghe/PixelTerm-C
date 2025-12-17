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

// Global application instance
static PixelTermApp *g_app = NULL;
static volatile sig_atomic_t g_terminate_requested = 0;

// Signal handler for graceful shutdown
static void signal_handler(int sig) {
    (void)sig; // Suppress unused parameter warning
    g_terminate_requested = 1;
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
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --version  Show version information\n");
    printf("  -D, --dither   Enable image dithering (default: disabled)\n");
    printf("  --no-preload   Disable image preloading (default: enabled)\n");
    printf("\n");
    printf("Controls:\n");
    printf("  Arrow Keys / hjkl             Navigate between images\n");
    printf("  Enter                         Toggle preview grid mode\n");
    printf("  Tab                           Toggle file manager mode\n");
    printf("  i                             Toggle image information\n");
    printf("  +/-                           Zoom in/out in preview mode\n");
    printf("  PgUp/PgDn                     Page up/down in preview mode\n");
    printf("  Mouse Click                   Select image in preview grid\n");
    printf("  Mouse Wheel                   Navigate/Scroll in all modes\n");
    printf("  Esc                           Quit application\n");
    printf("\n");
}

// Print version information
static void print_version(void) {
    printf("%s\n", APP_VERSION);
}

// Parse command line arguments
static ErrorCode parse_arguments(int argc, char *argv[], char **path, gboolean *preload_enabled, gboolean *dither_enabled) {
    static struct option long_options[] = {
        {"help",      no_argument,       0, 'h'},
        {"version",   no_argument,       0, 'v'},
        {"Version",   no_argument,       0, 'V'},
        {"no-preload", no_argument,      0, 1000},
        {"dither",     no_argument,      0, 1001},
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

// Main application loop
static ErrorCode run_application(PixelTermApp *app) {
    InputHandler *input_handler = input_handler_create();
    if (!input_handler) {
        return ERROR_MEMORY_ALLOC;
    }

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

    // Initial render: show file manager if already active
    if (app->file_manager_mode) {
        error = app_render_file_manager(app);
    } else {
        error = app_refresh_display(app);
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
            if (app->file_manager_mode) {
                app_render_file_manager(app);
            } else {
                app_refresh_display(app);
            }
            usleep(100000); // 100ms delay
            continue;
        }
        
                    // Process pending single click action (Single Image Mode)
                    if (app->pending_single_click) {
                        gint64 current_time = g_get_monotonic_time();
                        // Threshold: 400ms (400000 microseconds)
                        if (current_time - app->pending_click_time > 400000) {
                            app->pending_single_click = FALSE;
                            // Execute the deferred "Next Image" action
                            app_next_image(app);
                            if (app->needs_redraw) { // Only refresh if the image actually changed
                                app_refresh_display(app);
                                app->needs_redraw = FALSE;
                            }
                        }
                    }
        
                    // Process pending single click action (Preview Grid Mode)
                    if (app->pending_grid_single_click) {
                        gint64 current_time = g_get_monotonic_time();
                        // Threshold: 400ms (400000 microseconds)
                        if (current_time - app->pending_grid_click_time > 400000) {
                            app->pending_grid_single_click = FALSE;
                            // Execute the deferred "Select image" action
                            gboolean redraw_needed = FALSE;
                            app_handle_mouse_click_preview(app, app->pending_grid_click_x, app->pending_grid_click_y, &redraw_needed);
                            if (redraw_needed) {
                                app_render_preview_grid(app);
                            }
                        }
                    }
                    
                    // Process pending single click action (File Manager Mode)
                    if (app->file_manager_mode && app->pending_file_manager_single_click) {
                        gint64 current_time = g_get_monotonic_time();
                        if (current_time - app->pending_file_manager_click_time > 400000) {
                            app->pending_file_manager_single_click = FALSE;
                            app_handle_mouse_file_manager(app,
                                                          app->pending_file_manager_click_x,
                                                          app->pending_file_manager_click_y);
                            app_render_file_manager(app);
                        }
                    } else if (!app->file_manager_mode && app->pending_file_manager_single_click) {
                        app->pending_file_manager_single_click = FALSE;
                    }
                    
                    // Process any pending GLib events (such as timer callbacks for GIF animation)        // Only process these if we're currently displaying an animated GIF
        if (app->gif_player && gif_player_is_playing(app->gif_player)) {
            while (g_main_context_pending(NULL)) {
                g_main_context_iteration(NULL, FALSE);
            }
        }
        
        // Check if we have pending input with timeout to allow signal checking
        if (!input_has_pending_input(input_handler)) {
            // Use a shorter delay to allow more responsive event processing
            usleep(10000); // 10ms instead of 50ms for better animation responsiveness
            continue;
        }
        
        error = input_get_event(input_handler, &event);
        if (error != ERROR_NONE) {
            break;
        }

        // Process input events
        switch (event.type) {
            case INPUT_MOUSE_PRESS:
                if (app->preview_mode) {
                    // In preview grid mode, defer click action to wait for potential double click
                    app->pending_grid_single_click = TRUE;
                    app->pending_grid_click_time = g_get_monotonic_time();
                    app->pending_grid_click_x = event.mouse_x;
                    app->pending_grid_click_y = event.mouse_y;
                } else if (app->file_manager_mode) {
                    // Defer file manager single click to distinguish from double click
                    app->pending_file_manager_single_click = TRUE;
                    app->pending_file_manager_click_time = g_get_monotonic_time();
                    app->pending_file_manager_click_x = event.mouse_x;
                    app->pending_file_manager_click_y = event.mouse_y;
                } else {
                    // In single image mode, defer click action to wait for potential double click
                    app->pending_single_click = TRUE;
                    app->pending_click_time = g_get_monotonic_time();
                }
                break;
            case INPUT_MOUSE_DOUBLE_CLICK:
                if (app->preview_mode) {
                    // Handle double click: Enter single view
                    // Cancel any pending grid single click action
                    app->pending_grid_single_click = FALSE;
                    
                    // Ensure selection is on the clicked image
                    gboolean redraw_needed = FALSE;
                    app_handle_mouse_click_preview(app, event.mouse_x, event.mouse_y, &redraw_needed);

                    app->return_to_mode = 2; // Virtual selection for return to preview
                    app->preview_mode = FALSE;
                    app_render_current_image(app);
                } else if (app->file_manager_mode) {
                    // Handle double click: open entry directly without extra selection jumps
                    app->pending_file_manager_single_click = FALSE;
                    ErrorCode err = app_file_manager_enter_at_position(app, event.mouse_x, event.mouse_y);
                    if (err == ERROR_NONE && app->file_manager_mode) {
                        app_render_file_manager(app);
                    }
                } else {
                    // Handle double click in single image mode: Enter preview grid
                    // Cancel any pending single click action
                    app->pending_single_click = FALSE;
                    
                    if (app_enter_preview(app) == ERROR_NONE) {
                        app_render_preview_grid(app);
                    }
                }
                break;
            case INPUT_MOUSE_SCROLL:
                if (app->file_manager_mode) {
                    // Handle scroll in file manager
                    if (event.mouse_button == MOUSE_SCROLL_UP) {
                        app_file_manager_up(app);
                    } else if (event.mouse_button == MOUSE_SCROLL_DOWN) {
                        app_file_manager_down(app);
                    }
                    app_render_file_manager(app);
                } else if (app->preview_mode) {
                    // Handle scroll in preview grid mode
                    if (event.mouse_button == MOUSE_SCROLL_UP) {
                        app_preview_page_move(app, -1);
                    } else if (event.mouse_button == MOUSE_SCROLL_DOWN) {
                        app_preview_page_move(app, 1);
                    }
                    app_render_preview_grid(app);
                } else {
                    // Handle mouse scroll in single image mode
                    gboolean redraw_needed = FALSE;
                    if (event.mouse_button == MOUSE_SCROLL_UP) {
                        gint old_index = app_get_current_index(app);
                        app_previous_image(app);
                        if (old_index != app_get_current_index(app)) {
                            redraw_needed = TRUE;
                        }
                    } else if (event.mouse_button == MOUSE_SCROLL_DOWN) {
                        gint old_index = app_get_current_index(app);
                        app_next_image(app);
                        if (old_index != app_get_current_index(app)) {
                            redraw_needed = TRUE;
                        }
                    }
                    if (redraw_needed) {
                        app_refresh_display(app);
                        app->needs_redraw = FALSE; // Reset after refresh
                    }
                }
                break;
            case INPUT_KEY_PRESS:
                // In file manager, any letter key acts as jump-to-letter
                if (app->file_manager_mode &&
                    ((event.key_code >= 'A' && event.key_code <= 'Z') ||
                     (event.key_code >= 'a' && event.key_code <= 'z'))) {
                    app_file_manager_jump_to_letter(app, (char)event.key_code);
                    app_render_file_manager(app);
                    break;
                }

                switch (event.key_code) {
                    case (KeyCode)'d':
                    case (KeyCode)'D':
                        app->dither_enabled = !app->dither_enabled;
                        if (app->preloader) {
                            preloader_stop(app->preloader); // Stop the preloader thread
                            preloader_cache_clear(app->preloader); // Clear preloader cache
                            preloader_initialize(app->preloader, app->dither_enabled); // Re-initialize with new setting
                            preloader_start(app->preloader); // Restart the preloader
                        }
                        if (app->preview_mode) {
                            app_render_preview_grid(app);
                        } else {
                            app_refresh_display(app);
                        }
                        break;
                    case KEY_LEFT:
                    case (KeyCode)'h':
                        if (app->preview_mode) {
                            app_preview_move_selection(app, 0, -1);
                            app_render_preview_grid(app);
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_LEFT && 
                                    skip_event.key_code != (KeyCode)'h' &&
                                    skip_event.key_code != KEY_UP &&
                                    skip_event.key_code != KEY_DOWN &&
                                    skip_event.key_code != KEY_RIGHT &&
                                    skip_event.key_code != (KeyCode)'l' &&
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_PAGE_DOWN) {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        } else if (app->file_manager_mode) {
                            ErrorCode err = app_file_manager_left(app);
                            if (err == ERROR_NONE) {
                                app_render_file_manager(app);
                            } else {
                                app_render_file_manager(app);
                            }
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_LEFT && 
                                    skip_event.key_code != (KeyCode)'h' &&
                                    skip_event.key_code != KEY_UP &&
                                    skip_event.key_code != KEY_DOWN &&
                                    skip_event.key_code != KEY_RIGHT &&
                                    skip_event.key_code != (KeyCode)'l' &&
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_PAGE_DOWN) {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        } else {
                            gint old_index = app_get_current_index(app);
                            app_previous_image(app);
                            if (old_index != app_get_current_index(app)) {
                                app_refresh_display(app);
                            }
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_LEFT && 
                                    skip_event.key_code != (KeyCode)'h' &&
                                    skip_event.key_code != KEY_UP &&
                                    skip_event.key_code != KEY_DOWN &&
                                    skip_event.key_code != KEY_RIGHT &&
                                    skip_event.key_code != (KeyCode)'l' &&
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_PAGE_DOWN) {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        }
                        break;
                    case KEY_RIGHT:
                    case (KeyCode)'l':
                        if (app->preview_mode) {
                            app_preview_move_selection(app, 0, 1);
                            app_render_preview_grid(app);
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_RIGHT && 
                                    skip_event.key_code != (KeyCode)'l' &&
                                    skip_event.key_code != KEY_UP &&
                                    skip_event.key_code != KEY_DOWN &&
                                    skip_event.key_code != KEY_LEFT &&
                                    skip_event.key_code != (KeyCode)'h' &&
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_PAGE_DOWN) {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        } else if (app->file_manager_mode) {
                            ErrorCode err = app_file_manager_right(app);
                            if (err == ERROR_NONE && app->file_manager_mode) {
                                app_render_file_manager(app);
                            } else if (app->file_manager_mode) {
                                app_render_file_manager(app);
                            }
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_RIGHT && 
                                    skip_event.key_code != (KeyCode)'l' &&
                                    skip_event.key_code != KEY_UP &&
                                    skip_event.key_code != KEY_DOWN &&
                                    skip_event.key_code != KEY_LEFT &&
                                    skip_event.key_code != (KeyCode)'h' &&
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_PAGE_DOWN) {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        } else {
                            gint old_index = app_get_current_index(app);
                            app_next_image(app);
                            if (old_index != app_get_current_index(app)) {
                                app_refresh_display(app);
                            }
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_RIGHT && 
                                    skip_event.key_code != (KeyCode)'l' &&
                                    skip_event.key_code != KEY_UP &&
                                    skip_event.key_code != KEY_DOWN &&
                                    skip_event.key_code != KEY_LEFT &&
                                    skip_event.key_code != (KeyCode)'h' &&
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_PAGE_DOWN) {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        }
                        break;
                    case (KeyCode)'i':
                        if (!app->preview_mode) {
                            if (g_app->info_visible) {
                                g_app->info_visible = FALSE;
                                app_render_current_image(app);
                            } else {
                                app_display_image_info(app);
                            }
                        }
                        break;
                    case (KeyCode)'r':
                        if (!app->preview_mode) {
                            app_delete_current_image(app);
                            app_refresh_display(app);
                        }
                        break;

                    case (KeyCode)'+':
                    case (KeyCode)'=': // treat '=' as unshifted '+'
                        if (app->preview_mode) {
                            app_preview_change_zoom(app, 1);
                        }
                        break;
                    case (KeyCode)'-':
                        if (app->preview_mode) {
                            app_preview_change_zoom(app, -1);
                        }
                        break;
                    case KEY_ESCAPE:
                        app->running = FALSE;
                        input_handler->should_exit = TRUE;
                        break;
                    case KEY_TAB:
                        if (app->preview_mode) {
                            // From preview mode, behavior depends on how we entered
                            if (app->return_to_mode == 1) {
                                // Blue border mode (entered from file manager with image selected)
                                // Tab should enter single image view
                                app_exit_preview(app, TRUE);
                                app_refresh_display(app);
                            } else {
                                // Yellow border mode or other modes: return to file manager
                                gint saved_return_mode = app->return_to_mode;
                                app->return_to_mode = 1; // Mark return to PREVIEW
                                app_exit_preview(app, TRUE);
                                app_enter_file_manager(app);
                                // If returning from yellow preview mode, restore the previous file manager selection
                                if (saved_return_mode == 2 && app->previous_selected_entry >= 0) {
                                    app->selected_entry = app->previous_selected_entry;
                                    app->previous_selected_entry = -1; // Reset
                                }
                                app_render_file_manager(app);
                            }
                        } else if (app->file_manager_mode) {
                            // From file manager, check if directory has images
                            if (!app_file_manager_has_images(app)) {
                                // No images in directory, TAB invalid
                                break;
                            }

                            // Load images from current file manager directory
                            ErrorCode load_error = app_load_directory(app, app->file_manager_directory);
                            if (load_error != ERROR_NONE) {
                                // If loading fails, stay in file manager
                                app_render_file_manager(app);
                                break;
                            }

                            // Check if current selection is an image
                            if (app_file_manager_selection_is_image(app)) {
                                // Selected item is an image: enter preview with blue border (actual selection)
                                app->return_to_mode = 1; // Mark return to PREVIEW
                                // Set current_index to the selected image's index in the image list
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
                                // Selected item is not an image: enter preview with yellow border (virtual selection)
                                app->return_to_mode = 2; // Mark return to YELLOW PREVIEW
                                app->previous_selected_entry = app->selected_entry; // Remember the file manager selection
                                app_exit_file_manager(app);
                                if (app_enter_preview(app) == ERROR_NONE) {
                                    app->preview_selected = 0; // Always select first image in yellow border mode
                                    app_render_preview_grid(app);
                                } else {
                                    app_refresh_display(app);
                                }
                            }
                        } else {
                            // From single image view, enter file manager
                            app->return_to_mode = 0; // Mark return to SINGLE
                            app_enter_file_manager(app);
                            app_render_file_manager(app);
                        }
                        break;
                    case (KeyCode)'k':
                    case KEY_UP:
                        if (app->preview_mode) {
                            app_preview_move_selection(app, -1, 0);
                            app_render_preview_grid(app);
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_UP && 
                                    skip_event.key_code != (KeyCode)'k' &&
                                    skip_event.key_code != KEY_LEFT &&
                                    skip_event.key_code != (KeyCode)'h' &&
                                    skip_event.key_code != KEY_RIGHT &&
                                    skip_event.key_code != (KeyCode)'l' &&
                                    skip_event.key_code != KEY_DOWN &&
                                    skip_event.key_code != (KeyCode)'j' &&
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_PAGE_DOWN) {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        } else if (app->file_manager_mode) {
                            app_file_manager_up(app);
                            app_render_file_manager(app);
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_UP && 
                                    skip_event.key_code != (KeyCode)'k' &&
                                    skip_event.key_code != KEY_LEFT &&
                                    skip_event.key_code != (KeyCode)'h' &&
                                    skip_event.key_code != KEY_RIGHT &&
                                    skip_event.key_code != (KeyCode)'l' &&
                                    skip_event.key_code != KEY_DOWN &&
                                    skip_event.key_code != (KeyCode)'j' &&
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_PAGE_DOWN) {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        } else {
                            gint old_index = app_get_current_index(app);
                            app_previous_image(app);
                            if (old_index != app_get_current_index(app)) {
                                app_refresh_display(app);
                            }
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_UP && 
                                    skip_event.key_code != (KeyCode)'k' &&
                                    skip_event.key_code != KEY_LEFT &&
                                    skip_event.key_code != (KeyCode)'h' &&
                                    skip_event.key_code != KEY_RIGHT &&
                                    skip_event.key_code != (KeyCode)'l' &&
                                    skip_event.key_code != KEY_DOWN &&
                                    skip_event.key_code != (KeyCode)'j' &&
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_PAGE_DOWN) {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        }
                        break;
                    case (KeyCode)'j':
                    case KEY_DOWN:
                        if (app->preview_mode) {
                            app_preview_move_selection(app, 1, 0);
                            app_render_preview_grid(app);
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_DOWN && 
                                    skip_event.key_code != (KeyCode)'j' &&
                                    skip_event.key_code != KEY_LEFT &&
                                    skip_event.key_code != (KeyCode)'h' &&
                                    skip_event.key_code != KEY_RIGHT &&
                                    skip_event.key_code != (KeyCode)'l' &&
                                    skip_event.key_code != KEY_UP &&
                                    skip_event.key_code != (KeyCode)'k' &&
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_PAGE_DOWN) {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        } else if (app->file_manager_mode) {
                            app_file_manager_down(app);
                            app_render_file_manager(app);
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_DOWN && 
                                    skip_event.key_code != (KeyCode)'j' &&
                                    skip_event.key_code != KEY_LEFT &&
                                    skip_event.key_code != (KeyCode)'h' &&
                                    skip_event.key_code != KEY_RIGHT &&
                                    skip_event.key_code != (KeyCode)'l' &&
                                    skip_event.key_code != KEY_UP &&
                                    skip_event.key_code != (KeyCode)'k' &&
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_PAGE_DOWN) {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        } else {
                            gint old_index = app_get_current_index(app);
                            app_next_image(app);
                            if (old_index != app_get_current_index(app)) {
                                app_refresh_display(app);
                            }
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_DOWN && 
                                    skip_event.key_code != (KeyCode)'j' &&
                                    skip_event.key_code != KEY_LEFT &&
                                    skip_event.key_code != (KeyCode)'h' &&
                                    skip_event.key_code != KEY_RIGHT &&
                                    skip_event.key_code != (KeyCode)'l' &&
                                    skip_event.key_code != KEY_UP &&
                                    skip_event.key_code != (KeyCode)'k' &&
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_PAGE_DOWN) {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        }
                        break;
                    case KEY_PAGE_DOWN:
                        if (app->preview_mode) {
                            app_preview_page_move(app, 1); // jump a page down
                            app_render_preview_grid(app);
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_PAGE_DOWN && 
                                    skip_event.key_code != KEY_PAGE_UP &&
                                    skip_event.key_code != KEY_UP &&
                                    skip_event.key_code != KEY_DOWN &&
                                    skip_event.key_code != KEY_LEFT &&
                                    skip_event.key_code != KEY_RIGHT &&
                                    skip_event.key_code != (KeyCode)'a') {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        }
                        break;
                    case KEY_PAGE_UP:
                        if (app->preview_mode) {
                            app_preview_page_move(app, -1); // jump a page up
                            app_render_preview_grid(app);
                            // Skip any queued navigation key events to prevent skipping when holding keys
                            InputEvent skip_event;
                            while (input_has_pending_input(input_handler)) {
                                ErrorCode skip_error = input_get_event(input_handler, &skip_event);
                                if (skip_error != ERROR_NONE) break;
                                if (skip_event.type != INPUT_KEY_PRESS) continue;
                                if (skip_event.key_code != KEY_PAGE_UP && 
                                    skip_event.key_code != KEY_PAGE_DOWN &&
                                    skip_event.key_code != KEY_UP &&
                                    skip_event.key_code != KEY_DOWN &&
                                    skip_event.key_code != KEY_LEFT &&
                                    skip_event.key_code != KEY_RIGHT &&
                                    skip_event.key_code != (KeyCode)'a') {
                                    // If we encounter a non-navigation key, put it back by breaking
                                    break;
                                }
                                // Skip this navigation event
                            }
                        }
                        break;
                    case KEY_BACKSPACE:
                    case 8: // Ctrl+H on many terminals
                        if (app->file_manager_mode) {
                            ErrorCode err = app_file_manager_toggle_hidden(app);
                            if (err == ERROR_NONE) {
                                app_render_file_manager(app);
                            }
                        }
                        break;
                    case KEY_ENTER:
                    case 13:  // Handle both LF (10) and CR (13) for Enter key
                        if (app->preview_mode) {
                            // If entering from yellow border mode, change to actual selection mode
                            if (app->return_to_mode == 2) {
                                app->return_to_mode = 1; // Change to actual selection
                            }
                            app_exit_preview(app, TRUE);
                            app_refresh_display(app);
                        } else if (app->file_manager_mode) {
                            // Clear any pending input to avoid multiple triggers
                            input_flush_buffer(input_handler);
                            
                            ErrorCode error = app_file_manager_enter(app);
                            if (error != ERROR_NONE) {
                                // Handle error - refresh display
                                app_render_file_manager(app);
                            } else if (app->file_manager_mode) {
                                // If still in file manager (directory entered), redraw immediately
                                app_render_file_manager(app);
                            }
                            // Note: when opening a file, app_file_manager_enter renders the image
                        } else {
                            // From normal view, Enter toggles into preview grid
                            if (app->return_to_mode == 2) {
                                app->return_to_mode = 1; // Change to actual selection
                            }
                            if (app_enter_preview(app) == ERROR_NONE) {
                                app_render_preview_grid(app);
                            }
                        }
                        break;
                    default:
                        if (app->file_manager_mode) {
                            if ((event.key_code >= 'A' && event.key_code <= 'Z') ||
                                (event.key_code >= 'a' && event.key_code <= 'z')) {
                                app_file_manager_jump_to_letter(app, (char)event.key_code);
                                app_render_file_manager(app);
                            }
                        }
                        break;
                }
                break;
            case INPUT_RESIZE:
                input_update_terminal_size(input_handler);
                get_terminal_size(&app->term_width, &app->term_height);
                printf("\033[2J\033[H\033[0m"); // Clear screen to avoid artifacts
                fflush(stdout);
                if (app->file_manager_mode) {
                    app_render_file_manager(app);
                } else {
                    app_refresh_display(app);
                }
                break;
            default:
                break;
        }
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
    
    ErrorCode error = parse_arguments(argc, argv, &path, &preload_enabled, &dither_enabled);
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

    // Create and initialize application
    g_app = app_create();
    if (!g_app) {
        g_free(path);
        return 1;
    }

    error = app_initialize(g_app, dither_enabled);
    if (error != ERROR_NONE) {
        fprintf(stderr, "Failed to initialize application: %d\n", error);
        app_destroy(g_app);
        g_free(path);
        return 1;
    }

    // Configure application settings
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
        if (error == ERROR_NONE) {
            // Always start in file manager for directories
            error = app_enter_file_manager(g_app);
            if (error == ERROR_NONE) {
                app_render_file_manager(g_app);
            }
        }
    } else {
        // Check if the file is a valid image file first
        if (!is_valid_image_file(path)) {
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
            error = run_application(g_app);
            app_destroy(g_app);
            g_free(path);
            g_app = NULL;
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
            error = run_application(g_app);
            app_destroy(g_app);
            g_free(path);
            g_app = NULL;
            return error == ERROR_NONE ? 0 : 1;
        }
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
