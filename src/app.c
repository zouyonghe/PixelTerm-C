#include "app.h"
#include "browser.h"
#include "renderer.h"
#include "input.h"
#include "preloader.h"
#include <sys/ioctl.h>
#include <unistd.h>

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
    app->file_manager_mode = FALSE;
    app->term_width = 80;
    app->term_height = 24;
    app->last_error = ERROR_NONE;
    app->file_manager_directory = NULL;
    app->directory_entries = NULL;
    app->selected_entry = 0;
    app->scroll_offset = 0;

    return app;
}

// Replace control characters to avoid terminal escape injection when printing paths
static gchar* sanitize_for_terminal(const gchar *text) {
    if (!text) {
        return g_strdup("");
    }

    gchar *safe = g_strdup(text);
    for (gchar *p = safe; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f || c == '\033') {
            *p = '?';
        }
    }
    return safe;
}

// Helpers for file manager layout
static gchar* app_file_manager_display_name(const PixelTermApp *app, const gchar *entry, gboolean *is_directory) {
    (void)app; // currently unused but kept for potential future context
    if (is_directory) *is_directory = FALSE;

    gboolean is_dir = g_file_test(entry, G_FILE_TEST_IS_DIR);
    if (is_directory) *is_directory = is_dir;

    gchar *display_name = NULL;
    if (is_dir) {
        gchar *basename = g_path_get_basename(entry);
        display_name = g_strdup_printf("%s/", basename);
        g_free(basename);
    } else {
        display_name = g_path_get_basename(entry);
    }

    return display_name;
}

static gint app_file_manager_max_display_len(const PixelTermApp *app) {
    gint max_len = 0;
    for (GList *cur = app->directory_entries; cur; cur = cur->next) {
        gchar *entry = (gchar*)cur->data;
        gboolean is_dir = FALSE;
        gchar *name = app_file_manager_display_name(app, entry, &is_dir);
        if (name) {
            gint len = strlen(name);
            if (len > max_len) {
                max_len = len;
            }
            g_free(name);
        }
    }
    return max_len > 0 ? max_len : 12;
}

static void app_file_manager_layout(const PixelTermApp *app, gint *col_width, gint *cols, gint *visible_rows, gint *total_rows) {
    gint max_len = app_file_manager_max_display_len(app);
    gint width = MAX(app->term_width, max_len + 2);

    gint column_count = 1;

    // Header (title + path) + footer help line
    gint header_lines = 2;
    gint help_lines = 1;
    gint vis_rows = app->term_height - header_lines - help_lines;
    if (vis_rows < 1) {
        vis_rows = 1;
    }

    gint total_entries = g_list_length(app->directory_entries);
    gint rows = (total_entries + column_count - 1) / column_count;
    if (rows < 1) rows = 1;

    if (col_width) *col_width = width;
    if (cols) *cols = column_count;
    if (visible_rows) *visible_rows = vis_rows;
    if (total_rows) *total_rows = rows;
}

static void app_file_manager_adjust_scroll(PixelTermApp *app, gint cols, gint visible_rows) {
    gint total_entries = g_list_length(app->directory_entries);
    gint total_rows = (total_entries + cols - 1) / cols;
    if (total_rows < 1) total_rows = 1;

    gint row = app->selected_entry / cols;
    gint target_row = visible_rows / 2; // try to keep selection centered
    gint desired_offset = row - target_row;

    gint max_offset = MAX(0, total_rows - visible_rows);
    if (desired_offset < 0) desired_offset = 0;
    if (desired_offset > max_offset) desired_offset = max_offset;

    app->scroll_offset = desired_offset;
}

// Try to highlight the currently viewed image when opening file manager
static void app_file_manager_select_current_image(PixelTermApp *app) {
    if (!app || !app->current_directory || !app->file_manager_directory) {
        return;
    }

    if (g_strcmp0(app->current_directory, app->file_manager_directory) != 0) {
        return;
    }

    const gchar *current_file = app_get_current_filepath(app);
    if (!current_file) {
        return;
    }

    gchar *current_norm = g_canonicalize_filename(current_file, NULL);
    if (!current_norm) {
        return;
    }

    gint idx = 0;
    for (GList *cur = app->directory_entries; cur; cur = cur->next, idx++) {
        gchar *path = (gchar*)cur->data;
        if (g_strcmp0(path, current_norm) == 0) {
            app->selected_entry = idx;
            break;
        }
    }

    g_free(current_norm);

    // Adjust scroll so selection is visible
    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);
    app_file_manager_adjust_scroll(app, cols, visible_rows);
}

// Jump to next entry starting with a letter
ErrorCode app_file_manager_jump_to_letter(PixelTermApp *app, char letter) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    gint total_entries = g_list_length(app->directory_entries);
    if (total_entries == 0) {
        return ERROR_NONE;
    }

    gchar target = g_ascii_tolower(letter);
    gint start = (app->selected_entry + 1) % total_entries;
    gint idx = start;

    do {
        gchar *entry = (gchar*)g_list_nth_data(app->directory_entries, idx);
        if (entry) {
            gchar *base = g_path_get_basename(entry);
            if (base && base[0]) {
                gchar first = g_ascii_tolower(base[0]);
                if (first == target) {
                    app->selected_entry = idx;
                    g_free(base);
                    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
                    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);

                    // Center selection vertically when possible
                    gint row = app->selected_entry / cols;
                    gint offset = row - visible_rows / 2;
                    if (offset < 0) offset = 0;
                    gint max_offset = MAX(0, total_rows - visible_rows);
                    if (offset > max_offset) offset = max_offset;
                    app->scroll_offset = offset;

                    app_file_manager_adjust_scroll(app, cols, visible_rows);
                    return ERROR_NONE;
                }
            }
            g_free(base);
        }
        idx = (idx + 1) % total_entries;
    } while (idx != start);

    return ERROR_NONE;
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

    // Cleanup file manager entries
    if (app->directory_entries) {
        g_list_free_full(app->directory_entries, (GDestroyNotify)g_free);
    }
    g_free(app->file_manager_directory);

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
    // Reset preloader state to avoid leaking threads or stale cache
    if (app->preloader) {
        preloader_stop(app->preloader);
        preloader_destroy(app->preloader);
        app->preloader = NULL;
    }

    g_free(app->current_directory);
    // Normalize path to remove trailing slashes and resolve relative components
    gchar *normalized_dir = g_canonicalize_filename(directory, NULL);
    app->current_directory = normalized_dir ? normalized_dir : g_strdup(directory);

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
    
    // If file was found and loaded successfully, mark for redraw
    if (found) {
        app->needs_redraw = TRUE;
        app->info_visible = FALSE;
    }
    
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
    gint image_width, image_height;
    
    if (app->preloader && app->preload_enabled) {
        rendered = preloader_get_cached_image(app->preloader, filepath);
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
            preloader_cache_add(app->preloader, filepath, rendered, image_width, image_height);
        }

        renderer_destroy(renderer);
    } else {
        // For cached images, get the actual dimensions from cache
        if (!preloader_get_cached_image_dimensions(app->preloader, filepath, &image_width, &image_height)) {
            // Fallback: count lines in the rendered output to get actual height
            image_width = app->term_width;
            image_height = 1; // Start with 1 for first line
            
            for (gsize i = 0; i < rendered->len; i++) {
                if (rendered->str[i] == '\n') {
                    image_height++;
                }
            }
        }
    }
    
    // Clear screen and reset terminal state
    printf("\033[2J\033[H\033[0m"); // Clear screen, move to top-left, and reset attributes
    printf("%s", rendered->str);
    
    // Calculate filename position relative to image center
    if (filepath) {
        gchar *basename = g_path_get_basename(filepath);
        gchar *safe_basename = sanitize_for_terminal(basename);
        if (safe_basename) {
            gint filename_len = strlen(safe_basename);
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
            printf("\033[34m%s\033[0m", safe_basename); // Blue filename with reset
            g_free(safe_basename);
            g_free(basename);
        }
    }
    
    fflush(stdout);

    g_string_free(rendered, TRUE);
    
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
    gchar *safe_basename = sanitize_for_terminal(basename);
    gchar *safe_dirname = sanitize_for_terminal(dirname);
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
    printf("📁 Filename: %s\n\033[G", safe_basename);
    printf("📂 Path: %s\n\033[G", safe_dirname);
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

    g_free(safe_basename);
    g_free(safe_dirname);
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
    
    if (app->file_manager_mode) {
        return app_render_file_manager(app);
    }

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

// Enter file manager mode
ErrorCode app_enter_file_manager(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }

    app->file_manager_mode = TRUE;
    app->selected_entry = 0;
    app->scroll_offset = 0;
    
    if (app->file_manager_directory) {
        g_free(app->file_manager_directory);
        app->file_manager_directory = NULL;
    }
    // Start browsing from current image directory if available, else current working dir
    if (app->current_directory) {
        app->file_manager_directory = g_strdup(app->current_directory);
    } else {
        app->file_manager_directory = g_get_current_dir();
    }
    
    return app_file_manager_refresh(app);
}

// Exit file manager mode
ErrorCode app_exit_file_manager(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }

    app->file_manager_mode = FALSE;
    
    // Cleanup directory entries
    if (app->directory_entries) {
        g_list_free_full(app->directory_entries, (GDestroyNotify)g_free);
        app->directory_entries = NULL;
    }
    g_clear_pointer(&app->file_manager_directory, g_free);
    
    // Reset info visibility to ensure proper display
    app->info_visible = FALSE;
    app->needs_redraw = TRUE;
    
    return ERROR_NONE;
}

// Move selection up in file manager
ErrorCode app_file_manager_up(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);
    gint total_entries = g_list_length(app->directory_entries);
    if (total_entries <= 0) {
        return ERROR_NONE;
    }

    if (app->selected_entry >= cols) {
        app->selected_entry -= cols;
    } else {
        app->selected_entry = 0;
    }
    app_file_manager_adjust_scroll(app, cols, visible_rows);

    return ERROR_NONE;
}

// Move selection down in file manager
ErrorCode app_file_manager_down(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);

    gint total_entries = g_list_length(app->directory_entries);
    if (total_entries <= 0) {
        return ERROR_NONE;
    }
    gint target = app->selected_entry + cols;
    if (target < total_entries) {
        app->selected_entry = target;
    } else {
        app->selected_entry = total_entries - 1;
    }
    app_file_manager_adjust_scroll(app, cols, visible_rows);

    return ERROR_NONE;
}

// Move selection left in file manager
ErrorCode app_file_manager_left(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    const gchar *current_dir = app->file_manager_directory;
    gboolean free_dir = FALSE;
    if (!current_dir) {
        current_dir = app->current_directory;
    }
    if (!current_dir) {
        current_dir = g_get_current_dir();
        free_dir = TRUE;
    }

    // Remember the directory we came from so we can highlight it in the parent view
    gchar *child_dir = g_strdup(current_dir);

    gchar *parent = g_path_get_dirname(current_dir);
    if (parent && g_strcmp0(parent, current_dir) != 0) {
        g_free(app->file_manager_directory);
        app->file_manager_directory = g_canonicalize_filename(parent, NULL);
        if (!app->file_manager_directory) {
            g_free(parent);
            g_free(child_dir);
            if (free_dir) g_free((gchar*)current_dir);
            return ERROR_FILE_NOT_FOUND;
        }
        // Refresh file manager with new directory
        ErrorCode err = app_file_manager_refresh(app);

        // Highlight the directory we just came from in the parent listing
        if (err == ERROR_NONE && child_dir) {
            gint idx = 0;
            for (GList *cur = app->directory_entries; cur; cur = cur->next, idx++) {
                gchar *path = (gchar*)cur->data;
                if (g_strcmp0(path, child_dir) == 0) {
                    app->selected_entry = idx;
                    // Recalculate scroll to keep selection visible
                    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
                    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);
                    app_file_manager_adjust_scroll(app, cols, visible_rows);
                    break;
                }
            }
        }

        g_free(parent);
        g_free(child_dir);
        if (free_dir) g_free((gchar*)current_dir);
        return err;
    }

    g_free(parent);
    g_free(child_dir);
    if (free_dir) g_free((gchar*)current_dir);
    return ERROR_NONE;
}

// Move selection right in file manager
ErrorCode app_file_manager_right(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    return app_file_manager_enter(app);
}

// Enter selected directory or open file in file manager
ErrorCode app_file_manager_enter(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    if (app->selected_entry >= g_list_length(app->directory_entries)) {
        return ERROR_INVALID_IMAGE;
    }

    gchar *selected_path = (gchar*)g_list_nth_data(app->directory_entries, app->selected_entry);
    if (!selected_path) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Check if it's a directory
    if (g_file_test(selected_path, G_FILE_TEST_IS_DIR)) {
        // Load the selected directory
        g_free(app->file_manager_directory);
        app->file_manager_directory = g_canonicalize_filename(selected_path, NULL);
        if (!app->file_manager_directory) {
            return ERROR_FILE_NOT_FOUND;
        }
        // Reset selection to first entry when changing directory
        app->selected_entry = 0;
        app->scroll_offset = 0;
        // Refresh file manager with new directory
        return app_file_manager_refresh(app);
    } else {
        // It's a file, load the file first
        ErrorCode error = app_load_single_file(app, selected_path);
        
        // Only exit file manager if file was loaded successfully
        if (error == ERROR_NONE) {
            // Clear screen before exiting file manager
            printf("\033[2J\033[H\033[0m");
            fflush(stdout);
            
            // Exit file manager mode
            app->file_manager_mode = FALSE;
            // Cleanup directory entries
            if (app->directory_entries) {
                g_list_free_full(app->directory_entries, (GDestroyNotify)g_free);
                app->directory_entries = NULL;
            }
            g_clear_pointer(&app->file_manager_directory, g_free);
            // Reset info visibility to ensure proper display
            app->info_visible = FALSE;
            app->needs_redraw = TRUE;
            // Force immediate rendering
            app_render_current_image(app);
            fflush(stdout);
        }
        return error;
    }
}

// Refresh file manager directory listing
ErrorCode app_file_manager_refresh(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    // Clear existing entries
    if (app->directory_entries) {
        g_list_free_full(app->directory_entries, (GDestroyNotify)g_free);
        app->directory_entries = NULL;
    }

    // Resolve directory to display: prefer file manager dir, then viewer dir, then cwd
    gchar *base_dir_dup = NULL;
    if (app->file_manager_directory) {
        base_dir_dup = g_strdup(app->file_manager_directory);
    } else if (app->current_directory) {
        base_dir_dup = g_strdup(app->current_directory);
    } else {
        base_dir_dup = g_get_current_dir();
    }

    gchar *current_dir = g_canonicalize_filename(base_dir_dup, NULL);
    g_free(base_dir_dup);
    if (!current_dir) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Persist canonical directory for consistent rendering/navigation
    g_free(app->file_manager_directory);
    app->file_manager_directory = current_dir;

    // Open directory
    GDir *dir = g_dir_open(current_dir, 0, NULL);
    if (!dir) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Collect all entries
    GList *entries = NULL;
    const gchar *name;

    // Add directories and files (skip current directory entry if encountered)
    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *full_path = g_build_filename(current_dir, name, NULL);
        
        if (g_strcmp0(full_path, current_dir) == 0) {
            g_free(full_path);
            continue;
        }
        
        if (g_file_test(full_path, G_FILE_TEST_IS_DIR) || g_file_test(full_path, G_FILE_TEST_IS_REGULAR)) {
            entries = g_list_append(entries, full_path);  // Use full path
        } else {
            g_free(full_path);
        }
    }

    g_dir_close(dir);

    // Sort entries: directories first, then files; each group alphabetical
    GList *dirs = NULL;
    GList *files = NULL;
    for (GList *cur = entries; cur != NULL; cur = cur->next) {
        gchar *path = (gchar*)cur->data;
        if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
            dirs = g_list_prepend(dirs, path);
        } else {
            files = g_list_prepend(files, path);
        }
    }
    dirs = g_list_sort(dirs, (GCompareFunc)g_strcmp0);
    files = g_list_sort(files, (GCompareFunc)g_strcmp0);
    app->directory_entries = g_list_concat(dirs, files);
    g_list_free(entries); // pointers moved into dirs/files concatenated list
    app->selected_entry = 0;
    app->scroll_offset = 0;
    app_file_manager_select_current_image(app);

    return ERROR_NONE;
}

// Render file manager interface
ErrorCode app_render_file_manager(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    // Update terminal dimensions before layout
    get_terminal_size(&app->term_width, &app->term_height);

    // Clear screen
    printf("\033[2J\033[H\033[0m");
    
    // Get current directory
    const gchar *current_dir = app->file_manager_directory;
    gboolean free_dir = FALSE;
    if (!current_dir) {
        current_dir = app->current_directory;
        if (!current_dir) {
            current_dir = g_get_current_dir();
            free_dir = TRUE;
        }
    }
    gchar *safe_current_dir = sanitize_for_terminal(current_dir);

    // Header centered: first line app name, second line current directory
    const char *header_title = "PixelTerm File Manager";
    gint title_len = strlen(header_title);
    gint title_pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
    for (gint i = 0; i < title_pad; i++) printf(" ");
    printf("%s\n", header_title);

    gint dir_len = strlen(safe_current_dir);
    gint dir_pad = (app->term_width > dir_len) ? (app->term_width - dir_len) / 2 : 0;
    for (gint i = 0; i < dir_pad; i++) printf(" ");
    printf("%s\n", safe_current_dir);

    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);

    gint total_entries = g_list_length(app->directory_entries);
    const char *help_text = "↑/↓ Move   ← Parent   →/Enter Open   TAB Toggle   ESC Exit";
    // 2 header lines + 1 footer line -> remaining rows for content
    gint available_rows = app->term_height - 3;
    gint target_row = available_rows / 2;

    // Clamp scroll_offset to valid range
    gint max_offset = MAX(0, total_rows - available_rows);
    if (app->scroll_offset > max_offset) {
        app->scroll_offset = max_offset;
    }
    if (app->scroll_offset < 0) {
        app->scroll_offset = 0;
    }

    gint start_row = app->scroll_offset;
    gint end_row = MIN(start_row + available_rows, total_rows);
    gint rows_to_render = end_row - start_row;
    if (rows_to_render < 0) rows_to_render = 0;

    // Calculate padding to keep the selected entry roughly centered
    gint selected_row = app->selected_entry; // single column
    gint selected_pos = selected_row - start_row; // position within rendered block
    if (selected_pos < 0) selected_pos = 0;
    if (selected_pos >= rows_to_render) selected_pos = rows_to_render - 1;

    gint top_padding = target_row - selected_pos;
    if (top_padding < 0) top_padding = 0;

    // Ensure we don't render past the available rows when centering
    gint visible_space = MAX(0, available_rows - top_padding);
    if (rows_to_render > visible_space) {
        end_row = MIN(total_rows, start_row + visible_space);
        rows_to_render = end_row - start_row;
    }

    gint bottom_padding = available_rows - rows_to_render - top_padding;
    if (bottom_padding < 0) bottom_padding = 0;

    // Top padding to keep highlight centered
    for (gint i = 0; i < top_padding; i++) {
        printf("\n");
    }

    for (gint row = start_row; row < end_row && rows_to_render > 0; row++, rows_to_render--) {
        gint idx = row; // single column
        if (idx >= total_entries) {
            break;
        }

        gchar *entry = (gchar*)g_list_nth_data(app->directory_entries, idx);
        gboolean is_dir = FALSE;
        gchar *display_name = app_file_manager_display_name(app, entry, &is_dir);
        gchar *print_name = sanitize_for_terminal(display_name);
        gint name_len = strlen(print_name);
        gboolean is_image = (!is_dir && is_image_file(entry));
        if (name_len > app->term_width) {
            // truncate aggressively but keep center alignment
            gchar *shortened = g_strndup(print_name, MAX(0, app->term_width - 3));
            g_free(print_name);
            print_name = g_strdup_printf("%s...", shortened);
            g_free(shortened);
            name_len = strlen(print_name);
        }

        gint pad = (app->term_width > name_len) ? (app->term_width - name_len) / 2 : 0;
        for (gint i = 0; i < pad; i++) printf(" ");

        gboolean selected = (idx == app->selected_entry);
        if (selected) {
            printf("\033[47;30m%s\033[0m", print_name);
        } else if (is_dir) {
            printf("\033[34m%s\033[0m", print_name);
        } else if (is_image) {
            printf("\033[32m%s\033[0m", print_name);
        } else {
            printf("%s", print_name);
        }
        printf("\n");

        g_free(print_name);
        g_free(display_name);
    }

    // Bottom padding to keep layout stable
    for (gint i = 0; i < bottom_padding; i++) {
        printf("\n");
    }

    // Footer/help centered on last line; keep cursor at line end
    gint help_len = strlen(help_text);
    gint help_pad = (app->term_width > help_len) ? (app->term_width - help_len) / 2 : 0;
    for (gint i = 0; i < help_pad; i++) printf(" ");
    printf("%s", help_text);

    fflush(stdout);

    g_free(safe_current_dir);
    if (free_dir) {
        g_free((gchar*)current_dir);
    }
    return ERROR_NONE;
}
