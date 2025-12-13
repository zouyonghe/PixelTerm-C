#include "app.h"
#include "browser.h"
#include "renderer.h"
#include "input.h"
#include "preloader.h"
#include <sys/ioctl.h>

// Create a new application instance
PixelTermApp* app_create(void) {
    PixelTermApp *app = g_new0(PixelTermApp, 1);
    if (!app) {
        return NULL;
    }

    // Initialize all pointers to NULL
    app->canvas = NULL;
    app->canvas_config = NULL;
    app->term_info = NULL;
    app->image_files = NULL;
    app->current_directory = NULL;
    app->preloader = NULL;
    app->preload_thread = NULL;
    app->preload_queue = NULL;
    app->render_cache = NULL;
    app->gerror = NULL;

    // Initialize mutexes
    g_mutex_init(&app->preload_mutex);
    g_mutex_init(&app->cache_mutex);

    // Set default state
    app->current_index = 0;
    app->total_images = 0;
    app->running = TRUE;
    app->show_info = FALSE;
    app->info_visible = FALSE;
    app->preload_enabled = TRUE;
    app->needs_redraw = TRUE;
    app->term_width = 80;
    app->term_height = 24;
    app->last_error = ERROR_NONE;

    return app;
}

// Destroy application and free resources
void app_destroy(PixelTermApp *app) {
    if (!app) {
        return;
    }

    // Stop any running threads
    app->running = FALSE;
    
    // Stop and destroy preloader
    if (app->preloader) {
        preloader_stop(app->preloader);
        preloader_destroy(app->preloader);
        app->preloader = NULL;
    }
    
    if (app->preload_thread) {
        g_thread_join(app->preload_thread);
    }

    // Cleanup Chafa resources
    if (app->canvas) {
        chafa_canvas_unref(app->canvas);
    }
    
    if (app->canvas_config) {
        chafa_canvas_config_unref(app->canvas_config);
    }
    
    if (app->term_info) {
        chafa_term_info_unref(app->term_info);
    }

    // Cleanup file list
    if (app->image_files) {
        g_list_free_full(app->image_files, (GDestroyNotify)g_free);
    }

    // Cleanup directory path
    g_free(app->current_directory);

    // Cleanup preloader queue
    if (app->preload_queue) {
        g_queue_free(app->preload_queue);
    }

    // Cleanup render cache
    if (app->render_cache) {
        g_hash_table_destroy(app->render_cache);
    }

    // Cleanup error
    if (app->gerror) {
        g_error_free(app->gerror);
    }

    // Cleanup mutexes
    g_mutex_clear(&app->preload_mutex);
    g_mutex_clear(&app->cache_mutex);

    g_free(app);
}

// Initialize application
ErrorCode app_initialize(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }

    // Detect terminal capabilities
    ChafaTermDb *term_db = chafa_term_db_get_default();
    if (!term_db) {
        return ERROR_CHAFA_INIT;
    }

    app->term_info = chafa_term_db_detect(term_db, NULL);
    if (!app->term_info) {
        return ERROR_CHAFA_INIT;
    }

    // Get terminal dimensions
    get_terminal_size(&app->term_width, &app->term_height);

    // Create canvas configuration
    app->canvas_config = chafa_canvas_config_new();
    if (!app->canvas_config) {
        return ERROR_CHAFA_INIT;
    }

    // Configure canvas for optimal terminal display
    chafa_canvas_config_set_geometry(app->canvas_config, app->term_width, app->term_height - 6);
    chafa_canvas_config_set_canvas_mode(app->canvas_config, CHAFA_CANVAS_MODE_TRUECOLOR);
    chafa_canvas_config_set_color_space(app->canvas_config, CHAFA_COLOR_SPACE_RGB);
    chafa_canvas_config_set_pixel_mode(app->canvas_config, CHAFA_PIXEL_MODE_SYMBOLS);

    // Create canvas
    app->canvas = chafa_canvas_new(app->canvas_config);
    if (!app->canvas) {
        return ERROR_CHAFA_INIT;
    }

    // Initialize data structures
    app->preload_queue = g_queue_new();
    if (!app->preload_queue) {
        return ERROR_MEMORY_ALLOC;
    }

    app->render_cache = g_hash_table_new_full(g_str_hash, g_str_equal, 
                                             g_free, (GDestroyNotify)gstring_destroy);
    if (!app->render_cache) {
        return ERROR_MEMORY_ALLOC;
    }

    return ERROR_NONE;
}

// Load directory with images
ErrorCode app_load_directory(PixelTermApp *app, const char *directory) {
    if (!app || !directory) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Cleanup existing file list
    if (app->image_files) {
        g_list_free_full(app->image_files, (GDestroyNotify)g_free);
        app->image_files = NULL;
    }

    g_free(app->current_directory);
    app->current_directory = g_strdup(directory);

    // Scan directory for image files
    FileBrowser *browser = browser_create();
    if (!browser) {
        return ERROR_MEMORY_ALLOC;
    }

    ErrorCode error = browser_scan_directory(browser, directory);
    if (error != ERROR_NONE) {
        browser_destroy(browser);
        return error;
    }

    // Copy file list to app
    GList *file_list = browser_get_all_files(browser);
    app->image_files = NULL;
    GList *current = file_list;
    while (current) {
        gchar *filepath = (gchar*)current->data;
        app->image_files = g_list_prepend(app->image_files, g_strdup(filepath));
        current = g_list_next(current);
    }
    app->image_files = g_list_reverse(app->image_files);
    app->total_images = browser_get_total_files(browser);
    app->current_index = 0;

    browser_destroy(browser);

    // Note: We return ERROR_NONE even if no images are found
    // The caller should check app_has_images() to handle this case

    // Start preloading if enabled
    if (app->preload_enabled) {
        app->preloader = preloader_create();
        if (app->preloader) {
            preloader_initialize(app->preloader);
            
            // Set terminal dimensions for preloader
            preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);
            
            preloader_start(app->preloader);
            
            // Add initial preload tasks for current directory
            preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index);
        }
    }

    return ERROR_NONE;
}

// Load single image file
ErrorCode app_load_single_file(PixelTermApp *app, const char *filepath) {
    if (!app || !filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    if (!file_exists(filepath)) {
        return ERROR_FILE_NOT_FOUND;
    }

    if (!is_image_file(filepath)) {
        return ERROR_INVALID_IMAGE;
    }

    // Get directory from file path
    gchar *directory = g_path_get_dirname(filepath);
    if (!directory) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Load the directory
    ErrorCode error = app_load_directory(app, directory);
    g_free(directory); // Free early to avoid memory leak
    
    if (error != ERROR_NONE) {
        return error;
    }

    // Find the specific file in the list
    gchar *target_basename = g_path_get_basename(filepath);
    gboolean found = FALSE;
    
    for (gint i = 0; i < app->total_images; i++) {
        gchar *current_file = (gchar*)g_list_nth_data(app->image_files, i);
        gchar *current_basename = g_path_get_basename(current_file);
        
        if (g_strcmp0(current_basename, target_basename) == 0) {
            app->current_index = i;
            found = TRUE;
            g_free(current_basename);
            break;
        }
        
        g_free(current_basename);
    }

    g_free(target_basename);
    return found ? ERROR_NONE : ERROR_FILE_NOT_FOUND;
}

// Navigate to next image
ErrorCode app_next_image(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    if (app->current_index < app->total_images - 1) {
        app->current_index++;
    } else {
        // Wrap around to first image
        app->current_index = 0;
    }
    
    app->needs_redraw = TRUE;
    app->info_visible = FALSE;  // Reset info visibility when switching images

    // Update preload tasks for new position
    if (app->preloader && app->preload_enabled) {
        preloader_clear_queue(app->preloader);
        preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index);
    }

    return ERROR_NONE;
}

// Navigate to previous image
ErrorCode app_previous_image(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    if (app->current_index > 0) {
        app->current_index--;
    } else {
        // Wrap around to last image
        app->current_index = app->total_images - 1;
    }
    
    app->needs_redraw = TRUE;
    app->info_visible = FALSE;  // Reset info visibility when switching images

    // Update preload tasks for new position
    if (app->preloader && app->preload_enabled) {
        preloader_clear_queue(app->preloader);
        preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index);
    }

    return ERROR_NONE;
}

// Go to specific image index
ErrorCode app_goto_image(PixelTermApp *app, gint index) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    if (index >= 0 && index < app->total_images) {
        app->current_index = index;
        app->needs_redraw = TRUE;
        app->info_visible = FALSE;  // Reset info visibility when switching images
        
        // Update preload tasks for new position
        if (app->preloader && app->preload_enabled) {
            preloader_clear_queue(app->preloader);
            preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index);
        }
        
        return ERROR_NONE;
    }

    return ERROR_INVALID_IMAGE;
}

// Render current image
ErrorCode app_render_current_image(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }
    
    // Reset info visibility when rendering image
    app->info_visible = FALSE;

    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Check if image is already cached
    GString *rendered = NULL;
    gboolean from_cache = FALSE;
    gint image_width, image_height;
    
    if (app->preloader && app->preload_enabled) {
        rendered = preloader_get_cached_image(app->preloader, filepath);
        if (rendered) {
            from_cache = TRUE;
        }
    }

    // If not in cache, render it normally
    if (!rendered) {
        // Create renderer
        ImageRenderer *renderer = renderer_create();
        if (!renderer) {
            return ERROR_MEMORY_ALLOC;
        }

        // Configure renderer
        RendererConfig config = {
            .max_width = app->term_width,
            .max_height = app->info_visible ? app->term_height - 10 : app->term_height - 1, // Normal: use almost full height, Info: reserve space
            .preserve_aspect_ratio = TRUE,
            .dither = TRUE,
            .color_space = CHAFA_COLOR_SPACE_RGB,
            .pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS,
            .work_factor = 1
        };

        ErrorCode error = renderer_initialize(renderer, &config);
        if (error != ERROR_NONE) {
            renderer_destroy(renderer);
            return error;
        }

        // Render image
        rendered = renderer_render_image_file(renderer, filepath);
        if (!rendered) {
            renderer_destroy(renderer);
            return ERROR_INVALID_IMAGE;
        }

        // Get rendered image dimensions
        renderer_get_rendered_dimensions(renderer, &image_width, &image_height);
        
        // Add to cache if preloader is available
        if (app->preloader && app->preload_enabled) {
            preloader_cache_add(app->preloader, filepath, rendered);
        }

        renderer_destroy(renderer);
    } else {
        // For cached images, get the actual dimensions from cache
        if (!preloader_get_cached_image_dimensions(app->preloader, filepath, &image_width, &image_height)) {
            // Fallback to estimation if dimensions not available
            image_width = app->term_width;
            image_height = app->term_height - 2; // Leave space for filename
        }
    }
    
    // Clear screen and reset terminal state
    printf("\033[2J\033[H\033[0m"); // Clear screen, move to top-left, and reset attributes
    printf("%s", rendered->str);
    
    // Calculate filename position relative to image center
    if (filepath) {
        gchar *basename = g_path_get_basename(filepath);
        if (basename) {
            gint filename_len = strlen(basename);
            // Center filename relative to image width, but ensure it stays within terminal bounds
            gint image_center_col = image_width / 2;
            gint filename_start_col = image_center_col - filename_len / 2;
            
            // Ensure filename doesn't go beyond terminal bounds
            if (filename_start_col < 0) filename_start_col = 0;
            if (filename_start_col + filename_len > app->term_width) {
                filename_start_col = app->term_width - filename_len;
            }
            
            // Move cursor to position just below the image and center the filename
            printf("\033[%d;%dH", image_height + 1, filename_start_col + 1);
            printf("\033[34m%s\033[0m", basename); // Blue filename with reset
            g_free(basename);
        }
    }
    
    fflush(stdout);

    // Cleanup resources - only free if not from cache
    if (!from_cache) {
        g_string_free(rendered, TRUE);
    }
    
    return ERROR_NONE;
}

// Display image information (toggle mode)
ErrorCode app_display_image_info(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }
    
    // If info is already visible, clear it by redrawing the image
    if (app->info_visible) {
        app->info_visible = FALSE;
        return app_render_current_image(app);
    }
    
    app->info_visible = TRUE;

    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Get image dimensions
    gint width, height;
    ErrorCode error = renderer_get_image_dimensions(filepath, &width, &height);
    if (error != ERROR_NONE) {
        return error;
    }

    // Get file information
    gchar *basename = g_path_get_basename(filepath);
    gchar *dirname = g_path_get_dirname(filepath);
    gint64 file_size = get_file_size(filepath);
    const char *ext = get_file_extension(filepath);

    // Calculate display values
    gdouble file_size_mb = file_size / (1024.0 * 1024.0);
    gdouble aspect_ratio = (height > 0) ? (gdouble)width / height : 1.0;
    gint index = app_get_current_index(app) + 1; // Convert to 1-based
    gint total = app_get_total_images(app);

    // Move to next line and ensure cursor is at the beginning
    printf("\n\033[G"); // New line and move cursor to column 1
    
    // Display information in Python style
    for (gint i = 0; i < 60; i++) printf("=");
    printf("\n\033[G");
    printf("📸 Image Details");
    printf("\n\033[G");
    for (gint i = 0; i < 60; i++) printf("=");
    printf("\n\033[G");
    printf("📁 Filename: %s\n\033[G", basename);
    printf("📂 Path: %s\n\033[G", dirname);
    printf("📄 Index: %d/%d\n\033[G", index, total);
    printf("💾 File size: %.1f MB\n\033[G", file_size_mb);
    printf("📐 Dimensions: %d x %d pixels\n\033[G", width, height);
    printf("🎨 Format: %s\n\033[G", ext ? ext + 1 : "unknown"); // Skip the dot
    printf("🎭 Color mode: RGB\n\033[G");
    printf("📏 Aspect ratio: %.2f\n\033[G", aspect_ratio);
    for (gint i = 0; i < 60; i++) printf("=");
    printf("\n\033[G");
    
    // Reset terminal attributes to prevent interference with future rendering
    printf("\033[0m");  // Reset all attributes
    
    fflush(stdout);

    g_free(basename);
    g_free(dirname);

    return ERROR_NONE;
}

// Refresh the display
ErrorCode app_refresh_display(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }

    // Update terminal size
    get_terminal_size(&app->term_width, &app->term_height);
    
    // Update preloader with new terminal dimensions
    if (app->preloader && app->preload_enabled) {
        preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);
    }

    return app_render_current_image(app);
}



// Toggle preloading
void app_toggle_preload(PixelTermApp *app) {
    if (app) {
        app->preload_enabled = !app->preload_enabled;
        
        if (app->preload_enabled) {
            // Enable preloading
            if (!app->preloader) {
                app->preloader = preloader_create();
                if (app->preloader) {
                    preloader_initialize(app->preloader);
                    
                    // Set terminal dimensions for preloader
                    preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);
                    
                    preloader_start(app->preloader);
                    
                    // Add initial preload tasks
                    preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index);
                }
            } else {
                // Update terminal dimensions before enabling
                preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);
                preloader_enable(app->preloader);
                preloader_resume(app->preloader);
            }
        } else {
            // Disable preloading
            if (app->preloader) {
                preloader_disable(app->preloader);
                preloader_clear_queue(app->preloader);
            }
        }
    }
}

// Check if application should exit
gboolean app_should_exit(const PixelTermApp *app) {
    return !app || !app->running;
}

// Delete current image
ErrorCode app_delete_current_image(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    const gchar *filepath = app_get_current_filepath(app);
    if (!filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Remove file
    if (unlink(filepath) != 0) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Remove from file list
    GList *current_link = g_list_nth(app->image_files, app->current_index);
    if (current_link) {
        // Remove from preload cache
        if (app->preloader && app->preload_enabled) {
            preloader_cache_remove(app->preloader, (const char*)current_link->data);
        }
        
        g_free(current_link->data);
        app->image_files = g_list_delete_link(app->image_files, current_link);
        app->total_images--;
        
        // Adjust index if necessary
        if (app->current_index >= app->total_images && app->current_index > 0) {
            app->current_index--;
        }
        
        // Update preload tasks after deletion
        if (app->preloader && app->preload_enabled && app_has_images(app)) {
            preloader_clear_queue(app->preloader);
            preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index);
        }
    }

    app->needs_redraw = TRUE;
    return ERROR_NONE;
}

// Get current image index
gint app_get_current_index(const PixelTermApp *app) {
    return app ? app->current_index : -1;
}

// Get total number of images
gint app_get_total_images(const PixelTermApp *app) {
    return app ? app->total_images : 0;
}

// Get current file path
const gchar* app_get_current_filepath(const PixelTermApp *app) {
    if (!app || !app->image_files || app->current_index < 0) {
        return NULL;
    }

    return (const gchar*)g_list_nth_data(app->image_files, app->current_index);
}

// Check if application has images
gboolean app_has_images(const PixelTermApp *app) {
    return app && app->image_files && app->total_images > 0;
}