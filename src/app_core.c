#define _GNU_SOURCE

#include "app.h"

#include "app_file_manager_internal.h"
#include "book.h"
#include "browser.h"
#include "preload_control.h"

#include <sys/stat.h>
#include <unistd.h>

static GList* app_get_current_image_link(const PixelTermApp *app) {
    if (!app || !app->image_files || app->current_index < 0) {
        return NULL;
    }
    if (app->total_images > 0 && app->current_index >= app->total_images) {
        return NULL;
    }

    GList *cursor = app->preview.selected_link;
    gint idx = app->preview.selected_link_index;
    if (!cursor || idx < 0) {
        cursor = app->image_files;
        idx = 0;
    }

    while (cursor && idx < app->current_index) {
        cursor = cursor->next;
        idx++;
    }
    while (cursor && idx > app->current_index) {
        cursor = cursor->prev;
        idx--;
    }
    if (cursor && idx == app->current_index) {
        return cursor;
    }

    cursor = app->image_files;
    idx = 0;
    while (cursor && idx < app->current_index) {
        cursor = cursor->next;
        idx++;
    }
    return (cursor && idx == app->current_index) ? cursor : NULL;
}

static gint app_find_file_index(GList *files, const char *filepath) {
    gchar *target_canonical = g_canonicalize_filename(filepath, NULL);
    gchar *target_basename = g_path_get_basename(filepath);
    gint found_index = -1;
    gint idx = 0;
    for (GList *cur = files; cur; cur = cur->next, idx++) {
        gchar *current_canonical = g_canonicalize_filename(cur->data, NULL);
        gboolean match = g_strcmp0(current_canonical, target_canonical) == 0;
        g_free(current_canonical);
        if (match) {
            found_index = idx;
            break;
        }
    }
    if (found_index < 0) {
        idx = 0;
        for (GList *cur = files; cur; cur = cur->next, idx++) {
            gchar *current_basename = g_path_get_basename(cur->data);
            gboolean match = g_strcmp0(current_basename, target_basename) == 0;
            g_free(current_basename);
            if (match) {
                found_index = idx;
                break;
            }
        }
    }
    g_free(target_canonical);
    g_free(target_basename);
    return found_index;
}

static ErrorCode app_load_directory_selected(PixelTermApp *app, const char *directory,
                                             const char *filepath) {
    if (!app || !directory) {
        return ERROR_FILE_NOT_FOUND;
    }

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
    gint browser_total = browser_get_total_files(browser);
    gchar *normalized_dir = g_canonicalize_filename(directory, NULL);
    GList *image_files = NULL;

    // Copy and duplicate file paths from browser's list to app's list
    for (GList *current_node = file_list_from_browser; current_node; current_node = g_list_next(current_node)) {
        gchar *filepath = (gchar*)current_node->data;
        image_files = g_list_prepend(image_files, g_strdup(filepath));
    }

    image_files = g_list_sort(image_files, (GCompareFunc)app_file_manager_compare_names);
    browser_destroy(browser);

    gint selected_index = filepath ? app_find_file_index(image_files, filepath) : 0;
    if (selected_index < 0) {
        g_list_free_full(image_files, g_free);
        g_free(normalized_dir);
        return ERROR_FILE_NOT_FOUND;
    }

    /* Commit only after scanning and copying succeed; directory may alias current_directory. */
    app_preloader_reset(app);
    g_list_free_full(app->image_files, g_free);
    g_free(app->current_directory);
    app->current_directory = normalized_dir;
    app->image_files = image_files;
    app->total_images = browser_total;
    app->current_index = selected_index;
    app->preview.selected_link = app->image_files;
    app->preview.selected_link_index = app->image_files ? 0 : -1;

    // Note: We return ERROR_NONE even if no images are found
    // The caller should check app_has_images() to handle this case

    // Start preloading if enabled
    if (app->preload_enabled) {
        (void)app_preloader_enable(app, TRUE);
    }

    return ERROR_NONE;
}

ErrorCode app_load_directory(PixelTermApp *app, const char *directory) {
    return app_load_directory_selected(app, directory, NULL);
}

ErrorCode app_load_single_file(PixelTermApp *app, const char *filepath) {
    if (!app || !filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    if (!file_exists(filepath)) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Check if the file is a valid image/video file
    if (!is_valid_media_file(filepath)) {
        return ERROR_INVALID_IMAGE;
    }

    // Get directory from file path
    gchar *directory = g_path_get_dirname(filepath);
    if (!directory) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Load the directory
    ErrorCode error = app_load_directory_selected(app, directory, filepath);
    g_free(directory); // Free early to avoid memory leak

    if (error != ERROR_NONE) {
        return error;
    }

    app->needs_redraw = TRUE;
    app->info_visible = FALSE;
    app->return_to_mode = RETURN_MODE_SINGLE;
    app->image_zoom = 1.0;
    app->image_pan_x = 0.0;
    app->image_pan_y = 0.0;

    return ERROR_NONE;
}

ErrorCode app_open_book(PixelTermApp *app, const char *filepath) {
    if (!app || !filepath) {
        return ERROR_FILE_NOT_FOUND;
    }
    if (!is_valid_book_file(filepath)) {
        return ERROR_INVALID_IMAGE;
    }

    app_close_book(app);

    ErrorCode open_err = ERROR_NONE;
    BookDocument *doc = book_open(filepath, &open_err);
    if (!doc) {
        return open_err != ERROR_NONE ? open_err : ERROR_INVALID_IMAGE;
    }

    app->book.doc = doc;
    app->book.path = g_strdup(filepath);
    app->book.page_count = book_get_page_count(doc);
    app->book.page = 0;
    app->book.preview_selected = 0;
    app->book.preview_scroll = 0;
    app->book.preview_zoom = 0;
    app->book.toc = book_load_toc(doc);
    app->book.toc_selected = 0;
    app->book.toc_scroll = 0;
    app->book.toc_visible = FALSE;

    g_free(app->current_directory);
    gchar *directory = g_path_get_dirname(filepath);
    app->current_directory = directory ? directory : g_strdup(filepath);

    return ERROR_NONE;
}

void app_close_book(PixelTermApp *app) {
    if (!app) {
        return;
    }
    if (app->book.doc) {
        book_close(app->book.doc);
        app->book.doc = NULL;
    }
    g_clear_pointer(&app->book.path, g_free);
    app->book.page = 0;
    app->book.page_count = 0;
    app->book.preview_selected = 0;
    app->book.preview_scroll = 0;
    app->book.preview_zoom = 0;
    app->book.jump_active = FALSE;
    app->book.jump_dirty = FALSE;
    app->book.jump_len = 0;
    app->book.jump_buf[0] = '\0';
    if (app->book.toc) {
        book_toc_free(app->book.toc);
        app->book.toc = NULL;
    }
    app->book.toc_selected = 0;
    app->book.toc_scroll = 0;
    app->book.toc_visible = FALSE;
    (void)app_transition_mode(app, APP_MODE_SINGLE);
}

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
        app->help_visible = FALSE;  // Reset help overlay so rendering returns to the active protocol
        app->image_zoom = 1.0;
        app->image_pan_x = 0.0;
        app->image_pan_y = 0.0;

        // Update preload tasks for new position
        app_preloader_queue_directory(app);
    }

    return ERROR_NONE;
}

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
        app->help_visible = FALSE;  // Reset help overlay so rendering returns to the active protocol
        app->image_zoom = 1.0;
        app->image_pan_x = 0.0;
        app->image_pan_y = 0.0;

        // Update preload tasks for new position
        app_preloader_queue_directory(app);
    }

    return ERROR_NONE;
}

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
            app->help_visible = FALSE;  // Reset help overlay so rendering returns to the active protocol
            app->image_zoom = 1.0;
            app->image_pan_x = 0.0;
            app->image_pan_y = 0.0;

            // Update preload tasks for new position
            app_preloader_queue_directory(app);
        }

        return ERROR_NONE;
    }

    return ERROR_INVALID_IMAGE;
}

ErrorCode app_delete_current_image(PixelTermApp *app) {
    if (!app || !app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    GList *current_link = app_get_current_image_link(app);
    const gchar *filepath = current_link ? (const gchar*)current_link->data : NULL;
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
    if (current_link) {
        // Remove from preload cache
        if (app->preloader && app->preload_enabled) {
            preloader_cache_remove(app->preloader, (const char*)current_link->data);
        }

        g_free(current_link->data);
        app->image_files = g_list_delete_link(app->image_files, current_link);
        app->total_images--;
        app->preview.selected_link = NULL;
        app->preview.selected_link_index = -1;

        // Adjust index if necessary
        if (app->current_index >= app->total_images && app->current_index > 0) {
            app->current_index--;
        }

        // Update preload tasks after deletion
        app_preloader_queue_directory(app);
    }

    app->needs_redraw = TRUE;
    return ERROR_NONE;
}

gint app_get_current_index(const PixelTermApp *app) {
    return app ? app->current_index : -1;
}

gint app_get_total_images(const PixelTermApp *app) {
    return app ? app->total_images : 0;
}

const gchar* app_get_current_filepath(const PixelTermApp *app) {
    GList *current_link = app_get_current_image_link(app);
    return current_link ? (const gchar*)current_link->data : NULL;
}

gboolean app_has_images(const PixelTermApp *app) {
    return app && app->image_files && app->total_images > 0;
}
