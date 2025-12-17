#define _GNU_SOURCE

#include "app.h"
#include "browser.h"
#include "renderer.h"
#include "input.h"
#include "preloader.h"
#include <sys/ioctl.h>
#include <sys/stat.h>
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
    app->gif_player = NULL;
    app->gerror = NULL;

    // Set default state
    app->current_index = 0;
    app->total_images = 0;
    app->running = TRUE;
    app->show_info = FALSE;
    app->info_visible = FALSE;
    app->preload_enabled = TRUE;
    app->dither_enabled = FALSE;
    app->needs_redraw = TRUE;
    app->file_manager_mode = FALSE;
    app->show_hidden_files = FALSE;
    app->preview_mode = FALSE;
    app->preview_zoom = 0; // 0 indicates uninitialized target cell width
    app->return_to_mode = -1; // -1 indicates no return mode set
    app->term_width = 80;
    app->term_height = 24;
    app->last_error = ERROR_NONE;
    app->file_manager_directory = NULL;
    app->directory_entries = NULL;
    app->selected_entry = 0;
    app->scroll_offset = 0;
    app->preview_selected = 0;
    app->preview_scroll = 0;
    app->needs_screen_clear = FALSE;
    app->pending_single_click = FALSE;
    app->pending_click_time = 0;
    app->pending_grid_single_click = FALSE;
    app->pending_grid_click_time = 0;
    app->pending_grid_click_x = 0;
    app->pending_grid_click_y = 0;
    
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

// Calculate display width of a UTF-8 string for proper centering
static gint utf8_display_width(const gchar *text) {
    if (!text) {
        return 0;
    }

    gint width = 0;
    const gchar *p = text;
    while (*p) {
        gunichar ch = g_utf8_get_char_validated(p, -1);
        if (ch == (gunichar)-1 || ch == (gunichar)-2) {
            // Invalid sequence: treat current byte as a single-width placeholder
            width++;
            p++;
            continue;
        }

        if (!g_unichar_iszerowidth(ch)) {
            width += g_unichar_iswide(ch) ? 2 : 1;
        }
        p = g_utf8_next_char(p);
    }

    return width;
}

// Check if a directory contains image files
static gboolean directory_contains_images(const gchar *dir_path) {
    if (!dir_path || !g_file_test(dir_path, G_FILE_TEST_IS_DIR)) {
        return FALSE;
    }

    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (!dir) {
        return FALSE;
    }

    const gchar *filename;
    while ((filename = g_dir_read_name(dir))) {
        gchar *full_path = g_build_filename(dir_path, filename, NULL);
        if (g_file_test(full_path, G_FILE_TEST_IS_REGULAR) && is_image_file(full_path)) {
            g_free(full_path);
            g_dir_close(dir);
            return TRUE;
        }
        g_free(full_path);
    }

    g_dir_close(dir);
    return FALSE;
}

static void app_get_image_target_dimensions(const PixelTermApp *app, gint *max_width, gint *max_height) {
    if (!max_width || !max_height) {
        return;
    }
    gint width = (app && app->term_width > 0) ? app->term_width : 80;
    gint height = (app && app->term_height > 0) ? app->term_height : 24;
    if (app && app->info_visible) {
        height -= 10;
    } else {
        height -= 1;
    }
    if (height < 1) {
        height = 1;
    }
    if (width < 1) {
        width = 1;
    }
    *max_width = width;
    *max_height = height;
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

// Sort file manager entries in AaBb order (case-insensitive with uppercase first on ties)
static gint app_file_manager_compare_names(gconstpointer a, gconstpointer b) {
    const gchar *path_a = (const gchar*)a;
    const gchar *path_b = (const gchar*)b;
    if (!path_a || !path_b) {
        return path_a ? 1 : (path_b ? -1 : 0);
    }

    // Compare basenames using an AaBb… ordering: uppercase letters first,
    // then lowercase letters of the same alphabet, while still sorting by
    // alphabetical order within each case group.
    gchar *base_a = g_path_get_basename(path_a);
    gchar *base_b = g_path_get_basename(path_b);

    const gchar *pa = base_a;
    const gchar *pb = base_b;
    while (*pa && *pb) {
        gboolean a_is_alpha = g_ascii_isalpha(*pa);
        gboolean b_is_alpha = g_ascii_isalpha(*pb);

        gint weight_a;
        gint weight_b;

        if (a_is_alpha) {
            weight_a = (g_ascii_tolower(*pa) - 'a') * 2 + (g_ascii_isupper(*pa) ? 0 : 1);
        } else {
            weight_a = 1000 + (guchar)(*pa); // Non-letters sort after letters
        }

        if (b_is_alpha) {
            weight_b = (g_ascii_tolower(*pb) - 'a') * 2 + (g_ascii_isupper(*pb) ? 0 : 1);
        } else {
            weight_b = 1000 + (guchar)(*pb);
        }

        if (weight_a != weight_b) {
            gint diff = weight_a - weight_b;
            g_free(base_a);
            g_free(base_b);
            return diff;
        }

        pa++;
        pb++;
    }

    // All compared characters matched; shorter string wins
    gint result = (gint)(*pa) - (gint)(*pb);

    g_free(base_a);
    g_free(base_b);
    return result;
}

// Hide system-like entries that start with special prefixes (e.g., $RECYCLE.BIN)
static gboolean app_file_manager_is_special_entry(const gchar *name) {
    if (!name || !name[0]) {
        return FALSE;
    }
    return name[0] == '$';
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

typedef struct {
    gint col_width;
    gint cols;
    gint visible_rows;
    gint total_rows;
    gint total_entries;
    gint start_row;
    gint end_row;
    gint rows_to_render;
    gint top_padding;
    gint bottom_padding;
} FileManagerViewport;

static FileManagerViewport app_file_manager_compute_viewport(PixelTermApp *app) {
    FileManagerViewport viewport = {0};
    if (!app) {
        return viewport;
    }

    app_file_manager_layout(app, &viewport.col_width, &viewport.cols,
                            &viewport.visible_rows, &viewport.total_rows);
    viewport.total_entries = g_list_length(app->directory_entries);

    gint available_rows = viewport.visible_rows;
    if (available_rows < 0) {
        available_rows = 0;
    }

    gint max_offset = MAX(0, viewport.total_rows - 1);
    gint scroll_offset = app->scroll_offset;
    if (scroll_offset > max_offset) {
        scroll_offset = max_offset;
    }
    if (scroll_offset < 0) {
        scroll_offset = 0;
    }

    gint start_row = scroll_offset;
    if (viewport.total_rows <= 0) {
        start_row = 0;
    } else if (start_row >= viewport.total_rows) {
        start_row = viewport.total_rows - 1;
    }

    gint end_row = MIN(start_row + available_rows, viewport.total_rows);
    gint rows_to_render = end_row - start_row;
    if (rows_to_render < 0) {
        rows_to_render = 0;
    }

    gint selected_row = app->selected_entry;
    if (selected_row < 0) {
        selected_row = 0;
    } else if (viewport.total_rows > 0 && selected_row >= viewport.total_rows) {
        selected_row = viewport.total_rows - 1;
    } else if (viewport.total_rows == 0) {
        selected_row = 0;
    }

    gint selected_pos = selected_row - start_row;
    if (selected_pos < 0) {
        selected_pos = 0;
    }
    if (rows_to_render > 0 && selected_pos >= rows_to_render) {
        selected_pos = rows_to_render - 1;
    }

    gint target_row = available_rows / 2;
    gint top_padding = target_row - selected_pos;
    if (top_padding < 0) {
        gint more_rows_below = MAX(0, viewport.total_rows - end_row);
        gint scroll_shift = MIN(-top_padding, more_rows_below);
        if (scroll_shift > 0) {
            start_row += scroll_shift;
            end_row = MIN(start_row + available_rows, viewport.total_rows);
            rows_to_render = end_row - start_row;
            if (rows_to_render < 0) {
                rows_to_render = 0;
            }
            selected_pos = selected_row - start_row;
            if (selected_pos < 0) {
                selected_pos = 0;
            }
            if (rows_to_render > 0 && selected_pos >= rows_to_render) {
                selected_pos = rows_to_render - 1;
            }
            top_padding = target_row - selected_pos;
        }
        if (top_padding < 0) {
            top_padding = 0;
        }
    }

    gint visible_space = MAX(0, available_rows - top_padding);
    if (rows_to_render > visible_space) {
        end_row = MIN(viewport.total_rows, start_row + visible_space);
        rows_to_render = end_row - start_row;
        if (rows_to_render < 0) {
            rows_to_render = 0;
        }
    }

    gint bottom_padding = available_rows - rows_to_render - top_padding;
    if (bottom_padding < 0) {
        bottom_padding = 0;
    }

    viewport.start_row = start_row;
    viewport.end_row = end_row;
    viewport.rows_to_render = rows_to_render;
    viewport.top_padding = top_padding;
    viewport.bottom_padding = bottom_padding;
    return viewport;
}

static gboolean app_file_manager_hit_test(PixelTermApp *app, gint mouse_x, gint mouse_y, gint *out_index) {
    (void)mouse_x; // Currently unused because layout is single column
    if (!app || !app->file_manager_mode) {
        return FALSE;
    }

    get_terminal_size(&app->term_width, &app->term_height);
    FileManagerViewport viewport = app_file_manager_compute_viewport(app);

    // Header (title + path) occupies two rows
    gint header_lines = 2;
    if (mouse_y <= header_lines) {
        return FALSE;
    }

    gint row_idx = mouse_y - header_lines - 1;
    if (row_idx < 0 || row_idx >= viewport.visible_rows) {
        return FALSE;
    }

    if (row_idx < viewport.top_padding) {
        return FALSE;
    }

    gint relative_row = row_idx - viewport.top_padding;
    if (relative_row >= viewport.rows_to_render) {
        return FALSE;
    }

    gint absolute_row = viewport.start_row + relative_row;
    gint selected_idx = absolute_row * viewport.cols;
    if (selected_idx < 0 || selected_idx >= viewport.total_entries) {
        return FALSE;
    }

    if (out_index) {
        *out_index = selected_idx;
    }
    return TRUE;
}

static void app_file_manager_adjust_scroll(PixelTermApp *app, gint cols, gint visible_rows) {
    gint total_entries = g_list_length(app->directory_entries);
    gint total_rows = (total_entries + cols - 1) / cols;
    if (total_rows < 1) total_rows = 1;

    gint row = app->selected_entry / cols;
    
    // Always aim to center the selection
    gint target_row = visible_rows / 2;
    gint desired_offset = row - target_row;

    // Limit the range of scroll offset
    // Allow scrolling past the bottom to keep the last item centered
    gint max_offset = MAX(0, total_rows - 1 - (visible_rows / 2));
    
    if (desired_offset < 0) desired_offset = 0;
    if (desired_offset > max_offset) desired_offset = max_offset;

    // Only update scroll offset when necessary to avoid frequent changes
    if (app->scroll_offset != desired_offset) {
        app->scroll_offset = desired_offset;
    }
}

// Select the current image in file manager (only when appropriate)
static void app_file_manager_select_current_image(PixelTermApp *app) {
    // Only select current image if we're returning from image view (return_to_mode != -1)
    // For initial file manager entry (return_to_mode == -1), keep selected_entry = 0
    // Also select when returning from single image view (return_to_mode == 0)
    if (app->return_to_mode == -1) {
        return;
    }

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
    
    // Stop and destroy GIF player
    if (app->gif_player) {
        gif_player_stop(app->gif_player);
        gif_player_destroy(app->gif_player);
        app->gif_player = NULL;
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

    // Cleanup file manager entries
    if (app->directory_entries) {
        g_list_free_full(app->directory_entries, (GDestroyNotify)g_free);
    }
    g_free(app->file_manager_directory);

    // Cleanup error
    if (app->gerror) {
        g_error_free(app->gerror);
    }

    g_free(app);
}

// Initialize application
ErrorCode app_initialize(PixelTermApp *app, gboolean dither_enabled) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    app->dither_enabled = dither_enabled;

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

    // Initialize GIF player
    app->gif_player = gif_player_new();
    if (!app->gif_player) {
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

    GList *file_list_from_browser = browser_get_all_files(browser);

    // Clear existing app->image_files and prepare for new entries
    if (app->image_files) {
        g_list_free_full(app->image_files, (GDestroyNotify)g_free);
        app->image_files = NULL;
    }
    app->total_images = 0; // Reset count

    // Copy and duplicate file paths from browser's list to app's list
    for (GList *current_node = file_list_from_browser; current_node; current_node = g_list_next(current_node)) {
        gchar *filepath = (gchar*)current_node->data;
        app->image_files = g_list_append(app->image_files, g_strdup(filepath));
    }
    
    // Now sort app->image_files using the custom comparison function
    app->image_files = g_list_sort(app->image_files, (GCompareFunc)app_file_manager_compare_names);
    app->total_images = g_list_length(app->image_files);
    app->current_index = 0;

    browser_destroy(browser);

    // Note: We return ERROR_NONE even if no images are found
    // The caller should check app_has_images() to handle this case

    // Start preloading if enabled
    if (app->preload_enabled) {
        app->preloader = preloader_create();
        if (app->preloader) {
            preloader_initialize(app->preloader, app->dither_enabled);
            
            // Set terminal dimensions for preloader
            preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);
            gint target_width = 0, target_height = 0;
            app_get_image_target_dimensions(app, &target_width, &target_height);
            
            ErrorCode preload_err = preloader_start(app->preloader);
            if (preload_err == ERROR_NONE) {
                // Add initial preload tasks for current directory
                preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
            } else {
                preloader_destroy(app->preloader);
                app->preloader = NULL;
                app->preload_enabled = FALSE;
            }
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

    // Additionally check if the file is a valid image file
    if (!is_valid_image_file(filepath)) {
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

    // Find the specific file in the list (single pass)
    gchar *target_basename = g_path_get_basename(filepath);
    gboolean found = FALSE;
    gint idx = 0;
    for (GList *cur = app->image_files; cur; cur = cur->next, idx++) {
        gchar *current_basename = g_path_get_basename((gchar*)cur->data);
        if (g_strcmp0(current_basename, target_basename) == 0) {
            app->current_index = idx;
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
        app->return_to_mode = 0; // Actual selection
    }
    
    return found ? ERROR_NONE : ERROR_FILE_NOT_FOUND;
}

// Navigate to next image
ErrorCode app_next_image(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Store the old index to check if it actually changes
    gint old_index = app->current_index;

    if (app->current_index < app->total_images - 1) {
        app->current_index++;
    } else {
        // Wrap around to first image
        app->current_index = 0;
    }
    
    // Only redraw if the index actually changed
    if (old_index != app->current_index) {
        app->needs_redraw = TRUE;
        app->info_visible = FALSE;  // Reset info visibility when switching images

        // Update preload tasks for new position
        if (app->preloader && app->preload_enabled) {
            gint target_width = 0, target_height = 0;
            app_get_image_target_dimensions(app, &target_width, &target_height);
            preloader_clear_queue(app->preloader);
            preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
        }
    }

    return ERROR_NONE;
}

// Navigate to previous image
ErrorCode app_previous_image(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Store the old index to check if it actually changes
    gint old_index = app->current_index;

    if (app->current_index > 0) {
        app->current_index--;
    } else {
        // Wrap around to last image
        app->current_index = app->total_images - 1;
    }
    
    // Only redraw if the index actually changed
    if (old_index != app->current_index) {
        app->needs_redraw = TRUE;
        app->info_visible = FALSE;  // Reset info visibility when switching images

        // Update preload tasks for new position
        if (app->preloader && app->preload_enabled) {
            gint target_width = 0, target_height = 0;
            app_get_image_target_dimensions(app, &target_width, &target_height);
            preloader_clear_queue(app->preloader);
            preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
        }
    }

    return ERROR_NONE;
}

// Go to specific image index
ErrorCode app_goto_image(PixelTermApp *app, gint index) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    if (index >= 0 && index < app->total_images) {
        // Only redraw if the index actually changes
        if (app->current_index != index) {
            app->current_index = index;
            app->needs_redraw = TRUE;
            app->info_visible = FALSE;  // Reset info visibility when switching images
            
            // Update preload tasks for new position
            if (app->preloader && app->preload_enabled) {
                gint target_width = 0, target_height = 0;
                app_get_image_target_dimensions(app, &target_width, &target_height);
                preloader_clear_queue(app->preloader);
                preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
            }
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

    // Check if it's a GIF file and handle animation
    const char *ext = get_file_extension(filepath);
    gboolean is_gif = (ext && g_ascii_strcasecmp(ext, ".gif") == 0) ? TRUE : FALSE;
    
    if (is_gif && app->gif_player) {
        // First, check if we need to load the GIF animation
        if (!app->gif_player->filepath || g_strcmp0(app->gif_player->filepath, filepath) != 0) {
            ErrorCode load_result = gif_player_load(app->gif_player, filepath);
            if (load_result != ERROR_NONE) {
                // If GIF loading fails, just treat it as a regular image
                is_gif = FALSE;
            }
        }
    }

    // Check if image is already cached
    GString *rendered = NULL;
    gint image_width, image_height;
    gint target_width = 0, target_height = 0;
    app_get_image_target_dimensions(app, &target_width, &target_height);
    
    if (app->preloader && app->preload_enabled) {
        rendered = preloader_get_cached_image(app->preloader, filepath, target_width, target_height);
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
            .max_width = target_width,
            .max_height = target_height, // Normal: use almost full height, Info: reserve space
            .preserve_aspect_ratio = TRUE,
            .dither = app->dither_enabled,
            .color_space = CHAFA_COLOR_SPACE_RGB,
            .pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS,
            .work_factor = 1,
            .dither_mode = app->dither_enabled ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
            .color_extractor = CHAFA_COLOR_EXTRACTOR_MEDIAN
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
            preloader_cache_add(app->preloader, filepath, rendered, image_width, image_height, target_width, target_height);
        }

        renderer_destroy(renderer);
    } else {
        // For cached images, get the actual dimensions from cache
        if (!preloader_get_cached_image_dimensions(app->preloader, filepath, target_width, target_height, &image_width, &image_height)) {
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
    
    // Determine effective width for centering
    gint effective_width = image_width > 0 ? image_width : target_width;
    if (effective_width > app->term_width) {
        effective_width = app->term_width;
    }
    if (effective_width < 0) {
        effective_width = 0;
    }
    gint left_pad = (app->term_width > effective_width) ? (app->term_width - effective_width) / 2 : 0;
    if (left_pad < 0) left_pad = 0;

    // Clear screen and reset terminal state
    printf("\033[2J\033[H\033[0m"); // Clear screen, move to top-left, and reset attributes

    gchar *pad_buffer = NULL;
    if (left_pad > 0) {
        pad_buffer = g_malloc(left_pad);
        memset(pad_buffer, ' ', left_pad);
    }

    const gchar *line_ptr = rendered->str;
    gint row = 1;
    while (line_ptr && *line_ptr) {
        const gchar *newline = strchr(line_ptr, '\n');
        gint line_len = newline ? (gint)(newline - line_ptr) : (gint)strlen(line_ptr);
        printf("\033[%d;1H", row);
        if (left_pad > 0) {
            fwrite(pad_buffer, 1, left_pad, stdout);
        }
        if (line_len > 0) {
            fwrite(line_ptr, 1, line_len, stdout);
        }
        if (!newline) {
            break;
        }
        line_ptr = newline + 1;
        row++;
    }
    g_free(pad_buffer);
    
    // Calculate filename position relative to image center
    if (filepath) {
        gchar *basename = g_path_get_basename(filepath);
        gchar *safe_basename = sanitize_for_terminal(basename);
        if (safe_basename) {
            gint filename_len = strlen(safe_basename);
            // Center filename relative to image width, but ensure it stays within terminal bounds
            gint image_center_col = effective_width / 2;
            gint filename_start_col = left_pad + image_center_col - filename_len / 2;
            
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
    
    // If it's a GIF and player is available, start playing if animated
    if (is_gif && app->gif_player && gif_player_is_animated(app->gif_player)) {
        // For first render, just show the first frame, then start animation
        gif_player_play(app->gif_player);
        // Indicate that we are currently displaying an animated GIF
        app->needs_redraw = FALSE; // Don't immediately redraw since animation will handle updates
    } else {
        // For non-animated images, stop any existing animation
        if (app->gif_player) {
            gif_player_stop(app->gif_player);
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
    
    // Display information with colored labels
    for (gint i = 0; i < 60; i++) printf("=");
    printf("\n\033[G");
    printf("\033[36m📸 Image Details\033[0m");
    printf("\n\033[G");
    for (gint i = 0; i < 60; i++) printf("=");
    printf("\n\033[G");
    printf("\033[36m📁 Filename:\033[0m %s\n\033[G", safe_basename);
    printf("\033[36m📂 Path:\033[0m %s\n\033[G", safe_dirname);
    printf("\033[36m📄 Index:\033[0m %d/%d\n\033[G", index, total);
    printf("\033[36m💾 File size:\033[0m %.1f MB\n\033[G", file_size_mb);
    printf("\033[36m📐 Dimensions:\033[0m %d x %d pixels\n\033[G", width, height);
    printf("\033[36m🎨 Format:\033[0m %s\n\033[G", ext ? ext + 1 : "unknown"); // Skip the dot
    printf("\033[36m🎭 Color mode:\033[0m RGB\n\033[G");
    printf("\033[36m📏 Aspect ratio:\033[0m %.2f\n\033[G", aspect_ratio);
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
    
    if (app->preview_mode) {
        return app_render_preview_grid(app);
    }
    if (app->file_manager_mode) {
        return app_render_file_manager(app);
    }

    // Update preloader with new terminal dimensions
    if (app->preloader && app->preload_enabled) {
        preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);
    }
    
    // Update GIF player terminal size if active
    if (app->gif_player) {
        gif_player_update_terminal_size(app->gif_player);
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
                    preloader_initialize(app->preloader, app->dither_enabled);
                    
                    // Set terminal dimensions for preloader
                    preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);
                    gint target_width = 0, target_height = 0;
                    app_get_image_target_dimensions(app, &target_width, &target_height);
                    
                    ErrorCode preload_err = preloader_start(app->preloader);
                    if (preload_err == ERROR_NONE) {
                        // Add initial preload tasks
                        preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
                    } else {
                        preloader_destroy(app->preloader);
                        app->preloader = NULL;
                        app->preload_enabled = FALSE;
                    }
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

    // Safety guard: refuse to delete symlinks or files outside current directory
    struct stat st;
    if (lstat(filepath, &st) != 0) {
        return ERROR_FILE_NOT_FOUND;
    }
    if (S_ISLNK(st.st_mode)) {
        return ERROR_INVALID_IMAGE;
    }
    gchar *canonical_file = g_canonicalize_filename(filepath, NULL);
    gchar *canonical_dir = app->current_directory ? g_canonicalize_filename(app->current_directory, NULL)
                                                  : g_get_current_dir();
    gboolean allowed = FALSE;
    if (canonical_file && canonical_dir) {
        size_t dir_len = strlen(canonical_dir);
        if (dir_len > 0 && canonical_file && strncmp(canonical_file, canonical_dir, dir_len) == 0) {
            // Ensure boundary: either exact dir or subpath separator
            if (canonical_file[dir_len] == '\0' || canonical_file[dir_len] == '/') {
                allowed = TRUE;
            }
        }
    }
    g_free(canonical_file);
    g_free(canonical_dir);
    if (!allowed) {
        return ERROR_INVALID_IMAGE;
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
            gint target_width = 0, target_height = 0;
            app_get_image_target_dimensions(app, &target_width, &target_height);
            preloader_clear_queue(app->preloader);
            preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
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

    // Stop GIF playback if active
    if (app->gif_player) {
        gif_player_stop(app->gif_player);
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

    // Always start with hidden files hidden
    app->show_hidden_files = FALSE;
    
    // Clear screen on mode entry to avoid ghosting
    printf("\033[2J\033[H\033[0m");
    fflush(stdout);
    
    return app_file_manager_refresh(app);
}

// Exit file manager mode
ErrorCode app_exit_file_manager(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }

    app->file_manager_mode = FALSE;
    app->pending_file_manager_single_click = FALSE;
    
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
        // Jump to last entry when at the top and pressing up
        app->selected_entry = total_entries - 1;
    }
    
    // Adjust scroll using the improved algorithm
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
        // Jump to first entry when at the bottom and pressing down
        app->selected_entry = 0;
    }
    
    // Adjust scroll using the improved algorithm
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
        // It's a file, check if it's a valid image file before loading
        if (!is_valid_image_file(selected_path)) {
            // If it's not a valid image file, don't try to load it
            return ERROR_INVALID_IMAGE;
        }
        
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
    gchar *parent_entry = NULL;
    gchar *parent_dir = g_path_get_dirname(current_dir);
    if (parent_dir) {
        if (g_strcmp0(parent_dir, current_dir) != 0) {
            parent_entry = g_build_filename(current_dir, "..", NULL);
            entries = g_list_append(entries, parent_entry);
        }
        g_free(parent_dir);
    }
    const gchar *name;

    // Add directories and files (skip current directory entry if encountered)
    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *full_path = g_build_filename(current_dir, name, NULL);
        
        if (g_strcmp0(full_path, current_dir) == 0) {
            g_free(full_path);
            continue;
        }

        // Respect hidden-file visibility toggle
        if (!app->show_hidden_files && name[0] == '.') {
            g_free(full_path);
            continue;
        }

        // Skip system-like entries that start with special symbols (e.g., $RECYCLE.BIN)
        if (app_file_manager_is_special_entry(name)) {
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
    dirs = g_list_sort(dirs, (GCompareFunc)app_file_manager_compare_names);
    if (parent_entry) {
        dirs = g_list_remove(dirs, parent_entry);
        dirs = g_list_prepend(dirs, parent_entry);
    }
    files = g_list_sort(files, (GCompareFunc)app_file_manager_compare_names);
    app->directory_entries = g_list_concat(dirs, files);
    g_list_free(entries); // pointers moved into dirs/files concatenated list
    app->selected_entry = 0;
    app->scroll_offset = 0;
    app_file_manager_select_current_image(app);

    return ERROR_NONE;
}

// Check if current file manager directory contains any images
gboolean app_file_manager_has_images(PixelTermApp *app) {
    if (!app || !app->directory_entries) {
        return FALSE;
    }

    // Check if any entry in the current directory listing is an image file
    for (GList *cur = app->directory_entries; cur; cur = cur->next) {
        gchar *path = (gchar*)cur->data;
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR) && is_valid_image_file(path)) {
            return TRUE;
        }
    }

    return FALSE;
}

// Check if current file manager selection is an image file
gboolean app_file_manager_selection_is_image(PixelTermApp *app) {
    if (!app || !app->file_manager_mode || !app->directory_entries) {
        return FALSE;
    }

    if (app->selected_entry < 0 || app->selected_entry >= g_list_length(app->directory_entries)) {
        return FALSE;
    }

    gchar *path = (gchar*)g_list_nth_data(app->directory_entries, app->selected_entry);
    if (!path) {
        return FALSE;
    }

    return g_file_test(path, G_FILE_TEST_IS_REGULAR) && is_valid_image_file(path);
}

// Get the image index of the currently selected file in file manager
gint app_file_manager_get_selected_image_index(PixelTermApp *app) {
    if (!app || !app->file_manager_mode || !app->directory_entries || !app->image_files) {
        return -1;
    }

    if (app->selected_entry < 0 || app->selected_entry >= g_list_length(app->directory_entries)) {
        return -1;
    }

    gchar *selected_path = (gchar*)g_list_nth_data(app->directory_entries, app->selected_entry);
    if (!selected_path) {
        return -1;
    }

    // Find the index of this file in the image list
    gint image_index = 0;
    for (GList *cur = app->image_files; cur; cur = cur->next, image_index++) {
        const gchar *image_path = (const gchar*)cur->data;
        if (g_strcmp0(selected_path, image_path) == 0) {
            return image_index;
        }
    }

    return -1; // Not found
}

// Toggle hidden files visibility while preserving selection when possible
ErrorCode app_file_manager_toggle_hidden(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    // Remember current selection path
    gchar *prev_selected = NULL;
    if (app->selected_entry >= 0 && app->selected_entry < g_list_length(app->directory_entries)) {
        gchar *path = (gchar*)g_list_nth_data(app->directory_entries, app->selected_entry);
        if (path) {
            prev_selected = g_strdup(path);
        }
    }

    app->show_hidden_files = !app->show_hidden_files;
    ErrorCode err = app_file_manager_refresh(app);
    if (err != ERROR_NONE) {
        g_free(prev_selected);
        return err;
    }

    // Restore selection to the same path if still visible
    if (prev_selected) {
        gint idx = 0;
        for (GList *cur = app->directory_entries; cur; cur = cur->next, idx++) {
            gchar *path = (gchar*)cur->data;
            if (g_strcmp0(path, prev_selected) == 0) {
                app->selected_entry = idx;
                break;
            }
        }
        g_free(prev_selected);
    }

    // Ensure scroll offset keeps selection visible
    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);
    app_file_manager_adjust_scroll(app, cols, visible_rows);

    return ERROR_NONE;
}

// Handle mouse click in file manager
ErrorCode app_handle_mouse_file_manager(PixelTermApp *app, gint mouse_x, gint mouse_y) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    gint hit_index = -1;
    if (!app_file_manager_hit_test(app, mouse_x, mouse_y, &hit_index)) {
        return ERROR_NONE;
    }

    app->selected_entry = hit_index;

    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, &col_width, &cols, &visible_rows, &total_rows);
    app_file_manager_adjust_scroll(app, cols, visible_rows);

    return ERROR_NONE;
}

ErrorCode app_file_manager_enter_at_position(PixelTermApp *app, gint mouse_x, gint mouse_y) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    gint hit_index = -1;
    if (!app_file_manager_hit_test(app, mouse_x, mouse_y, &hit_index)) {
        return ERROR_INVALID_IMAGE;
    }

    gint prev_selected = app->selected_entry;
    gint prev_scroll = app->scroll_offset;
    app->selected_entry = hit_index;
    ErrorCode err = app_file_manager_enter(app);

    if (err != ERROR_NONE && app->file_manager_mode) {
        app->selected_entry = prev_selected;
        app->scroll_offset = prev_scroll;
    }

    return err;
}

// ----- Preview grid helpers -----
typedef struct {
    gint cols;
    gint rows;
    gint cell_width;
    gint cell_height;
    gint header_lines;
    gint visible_rows;
} PreviewLayout;

// Calculate preview grid layout using preview_zoom as target cell width
static PreviewLayout app_preview_calculate_layout(PixelTermApp *app) {
    PreviewLayout layout = {1, 1, app ? app->term_width : 80, 10, 3, 1};
    if (!app || app->total_images <= 0) {
        return layout;
    }

    const gint header_lines = 3;
    gint usable_width = app->term_width > 0 ? app->term_width : 80;
    gint usable_height = app->term_height > header_lines ? app->term_height - header_lines : 6;

    // If preview_zoom (target cell width) is uninitialized, default to ~30 chars/col
    if (app->preview_zoom <= 0) {
        app->preview_zoom = 30;
    }

    // Calculate columns based on target width
    gint cols = usable_width / app->preview_zoom;

    // Enforce minimum of 2 columns as requested
    if (cols < 2) cols = 2;

    // Enforce reasonable minimum width per column (e.g. 4 chars) to prevent garbage
    if (usable_width / cols < 4) {
        cols = usable_width / 4;
        if (cols < 2) cols = 2; // Priority to min 2 columns if width allows
    }

    gint cell_width = usable_width / cols;

    // Determine cell height based on aspect ratio (approx 2:1 char aspect)
    // We add 1 to compensate for the border padding (2 chars) which affects height proportionately more than width
    // Target: (w-2)/(h-2) = 2  =>  w-2 = 2h-4  =>  2h = w+2  =>  h = w/2 + 1
    gint cell_height = cell_width / 2 + 1;
    if (cell_height < 4) cell_height = 4;

    // Calculate rows
    gint rows = (app->total_images + cols - 1) / cols;
    if (rows < 1) rows = 1;

    gint visible_rows = usable_height / cell_height;
    if (visible_rows < 1) visible_rows = 1;

    layout.cols = cols;
    layout.rows = rows;
    layout.cell_width = cell_width;
    layout.cell_height = cell_height;
    layout.header_lines = header_lines;
    layout.visible_rows = visible_rows;
    return layout;
}

static void app_preview_adjust_scroll(PixelTermApp *app, const PreviewLayout *layout) {
    if (!app || !layout) {
        return;
    }

    gint total_rows = layout->rows;
    gint visible_rows = layout->visible_rows;
    if (visible_rows < 1) visible_rows = 1;

    // Clamp scroll to valid range
    // Allow scrolling until the last row is at the top (pagination style)
    gint max_offset = MAX(0, total_rows - 1);
    if (app->preview_scroll > max_offset) {
        app->preview_scroll = max_offset;
    }
    if (app->preview_scroll < 0) {
        app->preview_scroll = 0;
    }

    // Ensure selection is visible
    gint row = app->preview_selected / layout->cols;
    if (row < app->preview_scroll) {
        app->preview_scroll = row;
    } else if (row >= app->preview_scroll + visible_rows) {
        app->preview_scroll = row - visible_rows + 1;
    }
}

// Queue preload tasks for currently visible (and adjacent) preview cells
static void app_preview_queue_preloads(PixelTermApp *app, const PreviewLayout *layout) {
    if (!app || !layout || !app->preloader || !app->preload_enabled) {
        return;
    }

    gint content_width = MAX(2, layout->cell_width - 2);
    gint content_height = MAX(2, layout->cell_height - 2);

    // Preload current screen plus one row of lookahead/behind to smooth paging
    gint start_row = MAX(0, app->preview_scroll - 1);
    gint end_row = MIN(layout->rows, app->preview_scroll + layout->visible_rows + 1);
    gint start_index = start_row * layout->cols;
    GList *cursor = g_list_nth(app->image_files, start_index);

    for (gint row = start_row; row < end_row && cursor; row++) {
        for (gint col = 0; col < layout->cols && cursor; col++) {
            gint idx = row * layout->cols + col;
            if (idx >= app->total_images) {
                cursor = NULL;
                break;
            }
            const gchar *filepath = (const gchar*)cursor->data;
            gint distance = ABS(idx - app->preview_selected);
            gint priority = (distance == 0) ? 0 : (distance <= layout->cols ? 1 : 5 + distance);
            preloader_add_task(app->preloader, filepath, priority, content_width, content_height);
            cursor = cursor->next;
        }
    }
}

// Print brief info for the currently selected preview item on the status line
ErrorCode app_preview_print_info(PixelTermApp *app) {
    if (!app || !app->preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Use current selection (preview) or current image index if available
    const gchar *filepath = NULL;
    gint display_index = 0;
    if (app->preview_mode) {
        filepath = (const gchar*)g_list_nth_data(app->image_files, app->preview_selected);
        display_index = app->preview_selected;
    } else {
        filepath = app_get_current_filepath(app);
        display_index = app_get_current_index(app);
    }
    if (!filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    gchar *basename = g_path_get_basename(filepath);
    gchar *dirname = g_path_get_dirname(filepath);
    gchar *safe_basename = sanitize_for_terminal(basename);
    gchar *safe_dirname = sanitize_for_terminal(dirname);
    gint width = 0, height = 0;
    renderer_get_image_dimensions(filepath, &width, &height);
    gint64 file_size = get_file_size(filepath);
    gdouble file_size_mb = file_size > 0 ? file_size / (1024.0 * 1024.0) : 0.0;
    const char *ext = get_file_extension(filepath);
    gdouble aspect = (height > 0) ? (gdouble)width / height : 0.0;

    // Refresh terminal size in case of resize
    get_terminal_size(&app->term_width, &app->term_height);

    gint start_row = app->term_height - 8;
    if (start_row < 1) start_row = 1;

    const char *labels[7] = {
        "📁 Filename:",
        "📂 Path:",
        "📄 Index:",
        "💾 File size:",
        "📐 Dimensions:",
        "🎨 Format:",
        "📏 Aspect ratio:"
    };

    char values[7][256];
    g_snprintf(values[0], sizeof(values[0]), "%s", safe_basename ? safe_basename : "");
    g_snprintf(values[1], sizeof(values[1]), "%s", safe_dirname ? safe_dirname : "");
    g_snprintf(values[2], sizeof(values[2]), "%d/%d", display_index + 1, app->total_images);
    g_snprintf(values[3], sizeof(values[3]), "%.1f MB", file_size_mb);
    g_snprintf(values[4], sizeof(values[4]), "%d x %d pixels", width, height);
    g_snprintf(values[5], sizeof(values[5]), "%s", ext ? ext + 1 : "unknown");
    g_snprintf(values[6], sizeof(values[6]), "%.2f", aspect);

    // Clear area and print info block with colored labels
    for (gint i = 0; i < 7; i++) {
        gint row = start_row + i;
        printf("\033[%d;1H", row);
        for (gint c = 0; c < app->term_width; c++) putchar(' ');
        printf("\033[%d;1H\033[36m%s\033[0m %s", row, labels[i], values[i]);
    }
    printf("\033[0m");
    fflush(stdout);

    g_free(safe_basename);
    g_free(safe_dirname);
    g_free(basename);
    g_free(dirname);
    return ERROR_NONE;
}

// Compute visible width of a line ignoring ANSI escape sequences
static gint app_preview_visible_width(const char *str, gint len) {
    if (!str || len <= 0) {
        return 0;
    }
    gint width = 0;
    for (gint i = 0; i < len; i++) {
        if (str[i] == '\033' && i + 1 < len && str[i + 1] == '[') {
            // Skip CSI sequence
            i += 2;
            while (i < len && str[i] != 'm' && (str[i] < 'A' || str[i] > 'z')) {
                i++;
            }
            continue;
        }
        width++;
    }
    return width;
}

// Move selection inside preview grid
ErrorCode app_preview_move_selection(PixelTermApp *app, gint delta_row, gint delta_col) {
    if (!app || !app->preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    PreviewLayout layout = app_preview_calculate_layout(app);
    gint cols = layout.cols;
    gint rows = layout.rows;

    gint old_scroll = app->preview_scroll;

    gint row = app->preview_selected / cols;
    gint col = app->preview_selected % cols;

    // Page-aware movement: if moving past the visible window, jump by a full page and select first item
    row += delta_row;
    col += delta_col;

    // Horizontal wrap within current row
    if (delta_col < 0 && col < 0) {
        col = cols - 1;
    } else if (delta_col > 0 && col >= cols) {
        col = 0;
    }

    // Wrap vertically across pages
    if (delta_row > 0 && row >= rows) {
        row = 0;
        app->preview_scroll = 0;
    } else if (delta_row < 0 && row < 0) {
        gint visible_rows = layout.visible_rows > 0 ? layout.visible_rows : 1;
        gint last_page_scroll = 0;
        if (rows > 0) {
            last_page_scroll = ((rows - 1) / visible_rows) * visible_rows;
            if (last_page_scroll < 0) {
                last_page_scroll = 0;
            } else if (last_page_scroll > rows - 1) {
                last_page_scroll = rows - 1;
            }
        }
        row = rows - 1;
        app->preview_scroll = last_page_scroll;
    } else if (delta_row > 0 && row >= app->preview_scroll + layout.visible_rows) {
        gint new_scroll = MIN(app->preview_scroll + layout.visible_rows, MAX(rows - 1, 0));
        app->preview_scroll = new_scroll;
        row = new_scroll; // first row of next page, keep column
    } else if (delta_row < 0 && row < app->preview_scroll) {
        gint new_scroll = MAX(app->preview_scroll - layout.visible_rows, 0);
        app->preview_scroll = new_scroll;
        row = MIN(new_scroll + layout.visible_rows - 1, rows - 1); // last row of prev page, keep column
    }

    if (row < 0) row = 0;
    if (row >= rows) row = rows - 1;
    if (col < 0) col = 0;
    if (col >= cols) col = cols - 1;

    gint new_index = row * cols + col;
    // Keep selection inside current row bounds when wrapping on incomplete rows
    gint row_start = row * cols;
    gint row_end = MIN(app->total_images - 1, row_start + cols - 1);
    if (new_index < row_start) new_index = row_start;
    if (new_index > row_end) new_index = row_end;
    if (new_index >= app->total_images) {
        new_index = app->total_images - 1;
    }
    app->preview_selected = new_index;

    app_preview_adjust_scroll(app, &layout);
    if (app->preview_scroll != old_scroll) {
        app->needs_screen_clear = TRUE;
    }
    return ERROR_NONE;
}

// Jump a page up/down based on visible rows
ErrorCode app_preview_page_move(PixelTermApp *app, gint direction) {
    if (!app || !app->preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    PreviewLayout layout = app_preview_calculate_layout(app);
    gint delta = direction >= 0 ? layout.visible_rows : -layout.visible_rows;
    if (delta == 0) {
        delta = direction >= 0 ? 1 : -1;
    }
    ErrorCode result = app_preview_move_selection(app, delta, 0);
    if (result == ERROR_NONE) {
        app->needs_screen_clear = TRUE;
    }
    return result;
}

// Change preview zoom (target cell width) by stepping column count
ErrorCode app_preview_change_zoom(PixelTermApp *app, gint delta) {
    if (!app || !app->preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    gint usable_width = app->term_width > 0 ? app->term_width : 80;

    // Initialize if needed (default to 4 columns)
    if (app->preview_zoom <= 0) {
        app->preview_zoom = usable_width / 4; // Start with 4 columns
    }

    // Calculate current implied columns with proper rounding
    gint current_cols = (gint)(usable_width / app->preview_zoom + 0.5f);
    if (current_cols < 2) current_cols = 2;
    if (current_cols > 12) current_cols = 12;

    // Apply delta to columns.
    // +delta means Zoom In -> Larger Images -> Fewer Columns
    // -delta means Zoom Out -> Smaller Images -> More Columns
    gint new_cols = current_cols - delta;

    // Apply limits
    if (new_cols < 2) new_cols = 2;   // Minimum 2 columns
    if (new_cols > 12) new_cols = 12; // Maximum 12 columns

    // Check if we're already at limits and trying to go further
    if (new_cols == current_cols) {
        // Already at zoom limit, don't refresh
        return ERROR_NONE;
    }

    // Update target width based on new column count
    app->preview_zoom = (gdouble)usable_width / new_cols;
    if (app->preview_zoom < 1) app->preview_zoom = 1;

    // Mark for screen clear since layout changed
    app->needs_screen_clear = TRUE;

    // Only refresh if zoom actually changed
    return app_render_preview_grid(app);
}

// Handle mouse click in preview grid mode
ErrorCode app_handle_mouse_click_preview(PixelTermApp *app, gint mouse_x, gint mouse_y, gboolean *redraw_needed) {
    if (!app || !app->preview_mode) {
        if (redraw_needed) *redraw_needed = FALSE;
        return ERROR_MEMORY_ALLOC;
    }
    if (redraw_needed) *redraw_needed = FALSE; // Default to no redraw

    PreviewLayout layout = app_preview_calculate_layout(app);
    gint start_row = app->preview_scroll;

    // Check if click is in header area
    if (mouse_y < layout.header_lines + 1) {
        return ERROR_NONE; // Ignore clicks in header
    }

    // Calculate clicked cell position
    gint col = (mouse_x - 1) / layout.cell_width;
    gint row_in_visible = (mouse_y - layout.header_lines - 1) / layout.cell_height;
    gint absolute_row = start_row + row_in_visible;

    // Check bounds
    if (col < 0 || col >= layout.cols || row_in_visible < 0 || row_in_visible >= layout.visible_rows) {
        return ERROR_NONE; // Out of bounds
    }

    // Calculate image index
    gint index = absolute_row * layout.cols + col;

    // Check if index is valid
    if (index >= 0 && index < app->total_images) {
        if (app->preview_selected != index) { // Check if selection actually changed
            app->preview_selected = index;
            app->current_index = index; // Also update current index for consistency
            if (redraw_needed) *redraw_needed = TRUE;
        }
    }

    return ERROR_NONE;
}

// Enter preview grid mode
ErrorCode app_enter_preview(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Filter out invalid images before entering preview mode
    GList *valid_images = NULL;
    gint valid_count = 0;
    gint new_current_index = app->current_index;
    gint valid_current_index = -1;
    
    GList *current = app->image_files;
    gint original_index = 0;
    while (current) {
        const gchar *filepath = (const gchar*)current->data;
        if (is_valid_image_file(filepath)) {
            valid_images = g_list_append(valid_images, g_strdup(filepath));
            if (original_index == app->current_index) {
                valid_current_index = valid_count;
            }
            valid_count++;
        }
        current = g_list_next(current);
        original_index++;
    }
    
    // If we found valid images, replace the image list
    if (valid_images && valid_count > 0) {
        // Free the old image list
        if (app->image_files) {
            g_list_free_full(app->image_files, (GDestroyNotify)g_free);
        }
        
        app->image_files = valid_images;
        app->total_images = valid_count;
        
        // Update current index to the valid image that was selected
        if (valid_current_index >= 0) {
            app->current_index = valid_current_index;
        } else if (app->current_index >= app->total_images) {
            app->current_index = 0; // fallback to first image
        }
    } else {
        // If no valid images remain, return an error
        g_list_free_full(valid_images, (GDestroyNotify)g_free);
        return ERROR_INVALID_IMAGE;
    }

    // Stop GIF playback if active
    if (app->gif_player) {
        gif_player_stop(app->gif_player);
    }

    app->preview_mode = TRUE;
    app->file_manager_mode = FALSE; // ensure we are not in file manager
    app->preview_selected = app->current_index >= 0 ? app->current_index : 0;

    // For yellow border mode (return_to_mode == 2), always select first image
    if (app->return_to_mode == 2) {
        app->preview_selected = 0;
    }

    app->info_visible = FALSE;
    app->needs_redraw = TRUE;
    
    // Clear screen on mode entry to avoid ghosting
    printf("\033[2J\033[H\033[0m");
    fflush(stdout);

    if (app->preloader && app->preload_enabled) {
        preloader_clear_queue(app->preloader);
    }
    return ERROR_NONE;
}

// Exit preview grid mode
ErrorCode app_exit_preview(PixelTermApp *app, gboolean open_selected) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app->preview_mode) {
        return ERROR_NONE;
    }

    if (open_selected && app_has_images(app)) {
        if (app->preview_selected >= 0 && app->preview_selected < app->total_images) {
            app->current_index = app->preview_selected;
        }
    }

    app->preview_mode = FALSE;
    app->info_visible = FALSE;
    app->needs_redraw = TRUE;
    if (app->preloader && app->preload_enabled && app_has_images(app)) {
        gint target_width = 0, target_height = 0;
        app_get_image_target_dimensions(app, &target_width, &target_height);
        preloader_clear_queue(app->preloader);
        preloader_add_tasks_for_directory(app->preloader, app->image_files, app->current_index, target_width, target_height);
    }
    return ERROR_NONE;
}

// Render preview grid of images
ErrorCode app_render_preview_grid(PixelTermApp *app) {
    if (!app || !app->preview_mode) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Update terminal dimensions and force full clear if size changed
    gint prev_width = app->term_width;
    gint prev_height = app->term_height;
    get_terminal_size(&app->term_width, &app->term_height);
    if ((prev_width > 0 && prev_width != app->term_width) ||
        (prev_height > 0 && prev_height != app->term_height)) {
        app->needs_screen_clear = TRUE;
    }

    PreviewLayout layout = app_preview_calculate_layout(app);
    app_preview_adjust_scroll(app, &layout);
    app_preview_queue_preloads(app, &layout);

    if (app->needs_screen_clear) {
        printf("\033[2J\033[H\033[0m"); // Clear screen and move cursor to top-left
        app->needs_screen_clear = FALSE;
    } else {
        printf("\033[H\033[0m"); // Move cursor to top-left (don't clear screen to avoid flicker)
    }

    // Renderer reused for all cells to avoid repeated init/decode overhead
    gint content_width = layout.cell_width - 2;
    gint content_height = layout.cell_height - 2;
    if (content_width < 1) content_width = 1;
    if (content_height < 1) content_height = 1;
    RendererConfig config = {
        .max_width = MAX(2, content_width),
        .max_height = MAX(2, content_height),
        .preserve_aspect_ratio = TRUE,
        .dither = app->dither_enabled,
        .color_space = CHAFA_COLOR_SPACE_RGB,
        .pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS,
        .work_factor = 1,
        .dither_mode = app->dither_enabled ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
        .color_extractor = CHAFA_COLOR_EXTRACTOR_MEDIAN
    };
    ImageRenderer *renderer = renderer_create();
    if (!renderer) {
        return ERROR_MEMORY_ALLOC;
    }
    if (renderer_initialize(renderer, &config) != ERROR_NONE) {
        renderer_destroy(renderer);
        return ERROR_CHAFA_INIT;
    }

    // Header: title; controls are shown in footer
    const char *title = "Preview Grid";
    gint title_len = strlen(title);
    gint pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
    for (gint i = 0; i < pad; i++) printf(" ");
    printf("%s\n", title);

    printf("[%d/%d]\n\n", app->preview_selected + 1, app->total_images);

    gint start_row = app->preview_scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint start_index = start_row * layout.cols;
    GList *cursor = g_list_nth(app->image_files, start_index);

    for (gint row = start_row; row < end_row; row++) {
        for (gint col = 0; col < layout.cols; col++) {
            gint idx = row * layout.cols + col;
            if (idx >= app->total_images || !cursor) {
                // Skip remaining columns in this row if we've run out of images
                break;
            }

            gint cell_x = col * layout.cell_width + 1;
            gint cell_y = layout.header_lines + (row - start_row) * layout.cell_height + 1;

            const gchar *filepath = (const gchar*)cursor->data;
            cursor = cursor->next;
            gboolean selected = (idx == app->preview_selected);
            gboolean use_border = selected &&
                                  layout.cell_width >= 4 &&
                                  layout.cell_height >= 4;

            // Keep content area constant so selection doesn't change available image space
            gint content_x = cell_x + 1;
            gint content_y = cell_y + 1;

            // Clear cell and draw border without occupying content area
            const char *border_style = (app->return_to_mode == 2) ? "\033[33;1m" : "\033[34;1m"; // yellow for virtual selection, blue for actual
            for (gint line = 0; line < layout.cell_height; line++) {
                gint y = cell_y + line;
                printf("\033[%d;%dH", y, cell_x);
                for (gint c = 0; c < layout.cell_width; c++) {
                    putchar(' ');
                }

                if (use_border) {
                    if (line == 0 || line == layout.cell_height - 1) {
                        printf("\033[%d;%dH%s+", y, cell_x, border_style);
                        for (gint c = 0; c < layout.cell_width - 2; c++) putchar('-');
                        printf("+\033[0m");
                    } else {
                        printf("\033[%d;%dH%s|\033[0m", y, cell_x, border_style);
                        printf("\033[%d;%dH%s|\033[0m", y, cell_x + layout.cell_width - 1, border_style);
                    }
                }
            }

            gboolean rendered_from_preload = FALSE;
            gboolean rendered_from_renderer_cache = FALSE;

            GString *rendered = NULL;
            if (app->preloader && app->preload_enabled) {
                rendered = preloader_get_cached_image(app->preloader, filepath, content_width, content_height);
                rendered_from_preload = (rendered != NULL);
            }
            if (!rendered) {
                rendered = renderer_render_image_file(renderer, filepath);
                if (rendered) {
                    GString *cached_entry = renderer_cache_get(renderer, filepath);
                    rendered_from_renderer_cache = (cached_entry == rendered);
                }
            }
            if (!rendered) {
                continue;
            }

            if (!rendered_from_preload && app->preloader && app->preload_enabled) {
                gint rendered_w = 0, rendered_h = 0;
                renderer_get_rendered_dimensions(renderer, &rendered_w, &rendered_h);
                preloader_cache_add(app->preloader, filepath, rendered, rendered_w, rendered_h, content_width, content_height);
            }

            // Draw image lines within the cell bounds, horizontally centered
            gint line_no = 0;
            char *cursor = rendered->str;
            while (cursor && line_no < content_height) {
                char *newline = strchr(cursor, '\n');
                gint line_len = newline ? (gint)(newline - cursor) : (gint)strlen(cursor);

                gint visible_len = app_preview_visible_width(cursor, line_len);
                gint pad_left = 0;
                if (content_width > visible_len) {
                    pad_left = (content_width - visible_len) / 2;
                }

                printf("\033[%d;%dH", content_y + line_no, content_x + pad_left);
                fwrite(cursor, 1, line_len, stdout);

                if (!newline) {
                    break;
                }
                cursor = newline + 1;
                line_no++;
            }

            // Ensure attributes reset after each cell
            printf("\033[0m");

            // Free when we own the buffer (renderer output or preloader copy).
            if (!rendered_from_renderer_cache) {
                g_string_free(rendered, TRUE);
            }
        }
    }

    // Centered filename on the second-to-last line
    if (app->term_height >= 2) {
        const gchar *sel_path = (const gchar*)g_list_nth_data(app->image_files, app->preview_selected);
        if (sel_path) {
            gchar *base = g_path_get_basename(sel_path);
            gchar *safe = sanitize_for_terminal(base);
            gint row = app->term_height - 1;
            gint name_len = strlen(safe);
            if (name_len > app->term_width) {
                if (app->term_width > 3) {
                    safe[app->term_width - 3] = '.';
                    safe[app->term_width - 2] = '.';
                    safe[app->term_width - 1] = '\0';
                    name_len = app->term_width;
                } else {
                    safe[0] = '\0';
                    name_len = 0;
                }
            }
            gint pad = (app->term_width > name_len) ? (app->term_width - name_len) / 2 : 0;
            printf("\033[%d;1H", row);
            for (gint i = 0; i < app->term_width; i++) putchar(' ');
            if (name_len > 0) {
                printf("\033[%d;%dH\033[34m%s\033[0m", row, pad + 1, safe);
            }
            g_free(safe);
            g_free(base);
        }
    }

    // Footer with quick hints and page indicator at the bottom
    if (app->term_height > 0) {
        const char *help_text = "←/→/↑/↓ Move  PgUp/PgDn Page  Enter Open  TAB Toggle  +/- Zoom  ESC Exit";
        printf("\033[%d;1H", app->term_height);
        printf("\033[36m←/→/↑/↓\033[0m Move  ");
        printf("\033[36mPgUp/PgDn\033[0m Page  ");
        printf("\033[36mEnter\033[0m Open  ");
        printf("\033[36mTAB\033[0m Toggle  ");
        printf("\033[36m+/-\033[0m Zoom  ");
        printf("\033[36mESC\033[0m Exit");

        // Page indicator at bottom-right based on items per page
        gint rows_per_page = layout.visible_rows > 0 ? layout.visible_rows : 1;
        gint total_pages = (layout.rows + rows_per_page - 1) / rows_per_page;
        if (total_pages < 1) total_pages = 1;
        gint current_page = (app->preview_scroll + rows_per_page - 1) / rows_per_page + 1; // ceil division so last page reachable
        if (current_page < 1) current_page = 1;
        if (current_page > total_pages) current_page = total_pages;
        char page_text[32];
        g_snprintf(page_text, sizeof(page_text), "%d/%d", current_page, total_pages);
        gint page_len = strlen("Page ") + strlen(page_text);
        gint start_col = app->term_width - page_len + 1;
        if (start_col < (gint)strlen(help_text) + 2) {
            start_col = strlen(help_text) + 2;
        }
        if (start_col < 1) start_col = 1;
        printf("\033[%d;%dH\033[36mPage \033[0m%s", app->term_height, start_col, page_text);
    }

    fflush(stdout);
    renderer_destroy(renderer);
    return ERROR_NONE;
}

// Render file manager interface
ErrorCode app_render_file_manager(PixelTermApp *app) {
    if (!app || !app->file_manager_mode) {
        return ERROR_MEMORY_ALLOC;
    }

    // Update terminal dimensions before layout
    get_terminal_size(&app->term_width, &app->term_height);

    // Clear screen and reset cursor to top-left
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
    printf("\033[2K\033[1G"); // Clear line and move to beginning
    for (gint i = 0; i < title_pad; i++) printf(" "); // Use spaces for centering
    printf("%s\n", header_title);

    gint dir_byte_len = strlen(safe_current_dir);
    gint dir_len = utf8_display_width(safe_current_dir);
    gchar *display_dir = safe_current_dir;
    
    // Truncate directory path if it's too long
    if (dir_len > app->term_width - 8) { // More conservative threshold
        gint max_dir_display = app->term_width - 11; // Reserve space for "..." and more padding
        if (max_dir_display > 20) { // Higher minimum for directory paths
            gint start_len = (max_dir_display * 2) / 3; // Show more of the beginning
            gint end_len = max_dir_display - start_len;
            
            // Adjust start_len to avoid cutting UTF-8 characters
            while (start_len > 0 && (safe_current_dir[start_len] & 0xC0) == 0x80) {
                start_len--;
            }
            
            // Find proper start position for end part
            gint end_start = dir_byte_len - end_len;
            while (end_start < dir_byte_len && (safe_current_dir[end_start] & 0xC0) == 0x80) {
                end_start++;
            }
            
            gchar *start_part = g_strndup(safe_current_dir, start_len);
            gchar *end_part = g_strdup(safe_current_dir + end_start);
            
            display_dir = g_strdup_printf("%s...%s", start_part, end_part);
            g_free(start_part);
            g_free(end_part);
        } else {
            gint truncate_len = max_dir_display;
            // Adjust to avoid cutting UTF-8 characters
            while (truncate_len > 0 && (safe_current_dir[truncate_len] & 0xC0) == 0x80) {
                truncate_len--;
            }
            gchar *shortened = g_strndup(safe_current_dir, MAX(0, truncate_len));
            display_dir = g_strdup_printf("%s...", shortened);
            g_free(shortened);
        }
        dir_len = utf8_display_width(display_dir);
    }
    
    // Better centering calculation for directory path
    gint dir_pad = (app->term_width > dir_len) ? (app->term_width - dir_len) / 2 : 0;
    // Ensure the path doesn't exceed terminal bounds
    if (dir_pad + dir_len > app->term_width) {
        dir_pad = MAX(0, app->term_width - dir_len);
    }
    // Print the centered directory path
    printf("\033[2K\033[1G"); // Clear line and move to beginning
    for (gint i = 0; i < dir_pad; i++) printf(" "); // Use spaces for centering
    printf("%s\n", display_dir);
    
    // Free the truncated directory path if it was created
    if (display_dir != safe_current_dir) {
        g_free(display_dir);
    }

    FileManagerViewport viewport = app_file_manager_compute_viewport(app);
    app->scroll_offset = viewport.start_row;
    gint total_entries = viewport.total_entries;
    const char *help_text = "↑/↓ Move   ← Parent   →/Enter Open   TAB Toggle   Ctrl+H Hidden   ESC Exit";
    gint start_row = viewport.start_row;
    gint end_row = viewport.end_row;
    gint rows_to_render = viewport.rows_to_render;
    gint top_padding = viewport.top_padding;
    gint bottom_padding = viewport.bottom_padding;

    // Top padding to keep highlight centered
    for (gint i = 0; i < top_padding; i++) {
        printf("\033[2K\n");
    }

    if (total_entries == 0) {
        // Empty directory: show centered message
        const char *empty_msg = "（No items）";
        gint msg_len = strlen(empty_msg);
        gint center_pad = (app->term_width > msg_len) ? (app->term_width - msg_len) / 2 : 0;
        printf("\033[2K\033[1G"); // Clear line and move to beginning
        for (gint i = 0; i < center_pad; i++) printf(" ");
        printf("\033[33m%s\033[0m", empty_msg); // Yellow text for visibility
        printf("\n");
    } else {
        for (gint row = start_row; row < end_row && rows_to_render > 0; row++, rows_to_render--) {
            gint idx = row; // single column
            if (idx >= total_entries) {
                break;
            }

            gchar *entry = (gchar*)g_list_nth_data(app->directory_entries, idx);
            gboolean is_dir = FALSE;
            gchar *display_name = app_file_manager_display_name(app, entry, &is_dir);
            gchar *print_name = sanitize_for_terminal(display_name);
            gint name_len = utf8_display_width(print_name);  // Use display width instead of byte length
            gboolean is_image = (!is_dir && is_image_file(entry));
            gboolean is_dir_with_images = is_dir && directory_contains_images(entry);
            // Calculate maximum display width considering terminal boundaries
            // Use a more conservative width to ensure proper display
            gint max_display_width = (app->term_width / 2) - 2; // Use half terminal width for better centering
            if (max_display_width < 15) max_display_width = 15; // Minimum width
            
            if (name_len > max_display_width) {
                // Smart truncation: show beginning and end of filename
                gint max_display = max_display_width - 3; // Reserve space for "..."
                if (max_display > 8) { // Only use smart truncation if we have enough space
                    gint start_len = max_display / 2;
                    gint end_len = max_display - start_len;
                    
                    // For UTF-8 strings, we need to count characters, not bytes
                    gint char_count = 0;
                    const gchar *p = print_name;
                    while (*p) {
                        gunichar ch = g_utf8_get_char_validated(p, -1);
                        if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                            // Invalid sequence: treat current byte as a single character
                            char_count++;
                            p++;
                        } else {
                            char_count++;
                            p = g_utf8_next_char(p);
                        }
                    }
                    
                    // Calculate character positions for truncation
                    gint start_chars = start_len;
                    gint end_chars = end_len;
                    
                    // Find byte positions for character positions
                    gint start_byte = 0;
                    gint current_char = 0;
                    p = print_name;
                    while (*p && current_char < start_chars) {
                        gunichar ch = g_utf8_get_char_validated(p, -1);
                        if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                            p++;
                        } else {
                            p = g_utf8_next_char(p);
                        }
                        current_char++;
                    }
                    start_byte = p - print_name;
                    
                    // Find end position
                    current_char = 0;
                    const gchar *end_p = print_name;
                    while (*end_p) {
                        gunichar ch = g_utf8_get_char_validated(end_p, -1);
                        if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                            end_p++;
                        } else {
                            end_p = g_utf8_next_char(end_p);
                        }
                        current_char++;
                    }
                    
                    // Move backwards from end to find end_chars characters
                    gint end_byte = current_char;
                    current_char = 0;
                    end_p = print_name;
                    while (*end_p && current_char < char_count - end_chars) {
                        gunichar ch = g_utf8_get_char_validated(end_p, -1);
                        if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                            end_p++;
                        } else {
                            end_p = g_utf8_next_char(end_p);
                        }
                        current_char++;
                    }
                    end_byte = end_p - print_name;
                    
                    gchar *start_part = g_strndup(print_name, start_byte);
                    gchar *end_part = g_strdup(print_name + end_byte);
                    
                    g_free(print_name);
                    print_name = g_strdup_printf("%s...%s", start_part, end_part);
                    g_free(start_part);
                    g_free(end_part);
                } else {
                    // Fallback to simple truncation for very short display areas
                    gint truncate_len = max_display;
                    // For UTF-8, we need to truncate by character display width, not bytes
                    gint display_width = 0;
                    const gchar *p = print_name;
                    const gchar *truncate_pos = print_name;
                    
                    while (*p && display_width < truncate_len) {
                        gunichar ch = g_utf8_get_char_validated(p, -1);
                        if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                            display_width++;
                            truncate_pos = p + 1;
                            p++;
                        } else {
                            gint char_width = g_unichar_iswide(ch) ? 2 : 1;
                            if (display_width + char_width > truncate_len) {
                                break;
                            }
                            display_width += char_width;
                            truncate_pos = g_utf8_next_char(p);
                            p = truncate_pos;
                        }
                    }
                    
                    gchar *shortened = g_strndup(print_name, truncate_pos - print_name);
                    g_free(print_name);
                    print_name = g_strdup_printf("%s...", shortened);
                    g_free(shortened);
                }
                name_len = utf8_display_width(print_name);  // Recalculate display width
            }

            gint pad = (app->term_width > name_len) ? (app->term_width - name_len) / 2 : 0;
            // Ensure the total width (pad + name_len) doesn't exceed terminal width
            if (pad + name_len > app->term_width) {
                pad = MAX(0, app->term_width - name_len);
            }
            printf("\033[2K\033[1G"); // Clear line and move to beginning for consistent positioning
            for (gint i = 0; i < pad; i++) printf(" ");

            gboolean is_valid_image = (!is_dir && is_valid_image_file(entry));
            gboolean selected = (idx == app->selected_entry);
            if (is_image && !is_valid_image) {
                // Invalid image files (e.g., 0KB files) are shown with "Invalid" label
                if (selected) {
                    // Selected invalid file: only filename has highlight, [Invalid] stays as red text
                    printf("\033[47;30m%s\033[0m\033[31m [Invalid]\033[0m", print_name);
                } else {
                    // Non-selected invalid file: red text
                    printf("\033[31m%s [Invalid]\033[0m", print_name);
                }
            } else if (selected) {
                printf("\033[47;30m%s\033[0m", print_name);
            } else if (is_dir_with_images) {
                printf("\033[33m%s\033[0m", print_name); // Yellow for directories with images
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
    }

    // Bottom padding to keep layout stable
    for (gint i = 0; i < bottom_padding; i++) {
        printf("\033[2K\n");
    }

    // Footer/help centered on last line; keep cursor at line end (color keys only)
    gint help_len = strlen(help_text);
    gint help_pad = (app->term_width > help_len) ? (app->term_width - help_len) / 2 : 0;
    printf("\033[2K\033[1G"); // Clear line and move to beginning
    for (gint i = 0; i < help_pad; i++) printf(" ");
    printf("\033[36m↑/↓\033[0m Move   ");
    printf("\033[36m←\033[0m Parent   ");
    printf("\033[36m→/Enter\033[0m Open   ");
    printf("\033[36mTAB\033[0m Toggle   ");
    printf("\033[36mCtrl+H\033[0m Hidden   ");
    printf("\033[36mESC\033[0m Exit");

    fflush(stdout);

    g_free(safe_current_dir);
    if (free_dir) {
        g_free((gchar*)current_dir);
    }
    return ERROR_NONE;
}
