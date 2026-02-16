#include "app.h"
#include "app_file_manager_internal.h"

gint app_file_manager_compare_names(gconstpointer a, gconstpointer b) {
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

void app_file_manager_invalidate_selection_cache(PixelTermApp *app) {
    if (!app) {
        return;
    }
    app->file_manager.selected_link = NULL;
    app->file_manager.selected_link_index = -1;
}

GList* app_file_manager_find_link_with_hint(const PixelTermApp *app,
                                            gint target_index,
                                            GList *hint_link,
                                            gint hint_index) {
    if (!app || !app->file_manager.entries || target_index < 0) {
        return NULL;
    }
    if (app->file_manager.entries_count > 0 && target_index >= app->file_manager.entries_count) {
        return NULL;
    }

    GList *cursor = hint_link;
    gint idx = hint_index;
    if (!cursor || idx < 0) {
        cursor = app->file_manager.entries;
        idx = 0;
    }

    while (cursor && idx < target_index) {
        cursor = cursor->next;
        idx++;
    }
    while (cursor && idx > target_index) {
        cursor = cursor->prev;
        idx--;
    }
    if (cursor && idx == target_index) {
        return cursor;
    }

    cursor = app->file_manager.entries;
    idx = 0;
    while (cursor && idx < target_index) {
        cursor = cursor->next;
        idx++;
    }
    return (cursor && idx == target_index) ? cursor : NULL;
}

GList* app_file_manager_get_selected_node(PixelTermApp *app) {
    if (!app || !app->file_manager.entries || app->file_manager.selected_entry < 0) {
        return NULL;
    }
    if (app->file_manager.entries_count > 0 &&
        app->file_manager.selected_entry >= app->file_manager.entries_count) {
        return NULL;
    }

    GList *cursor = app_file_manager_find_link_with_hint(app,
                                                         app->file_manager.selected_entry,
                                                         app->file_manager.selected_link,
                                                         app->file_manager.selected_link_index);
    if (!cursor) {
        app_file_manager_invalidate_selection_cache(app);
        return NULL;
    }

    app->file_manager.selected_link = cursor;
    app->file_manager.selected_link_index = app->file_manager.selected_entry;
    return cursor;
}

gchar* app_file_manager_display_name(const PixelTermApp *app, const gchar *entry, gboolean *is_directory) {
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
    for (GList *cur = app->file_manager.entries; cur; cur = cur->next) {
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

void app_file_manager_layout(const PixelTermApp *app,
                             gint total_entries,
                             gint *col_width,
                             gint *cols,
                             gint *visible_rows,
                             gint *total_rows) {
    gint max_len = app_file_manager_max_display_len(app);
    gint width = MAX(app->term_width, max_len + 2);

    gint column_count = 1;

    // Layout:
    //   Row 1: title
    //   Row 2: blank
    //   Row 3: current directory
    //   Row 4: blank
    //   Rows 5 .. (term_height - 4): file list (centered)
    //   Last 4 rows: footer area (only last row used for help)
    gint header_lines = 4;
    gint help_lines = 4;
    gint vis_rows = app->term_height - header_lines - help_lines;
    if (vis_rows < 1) {
        vis_rows = 1;
    }

    if (total_entries < 0) {
        total_entries = app->file_manager.entries_count;
    }

    gint rows = (total_entries + column_count - 1) / column_count;
    if (rows < 1) rows = 1;

    if (col_width) *col_width = width;
    if (cols) *cols = column_count;
    if (visible_rows) *visible_rows = vis_rows;
    if (total_rows) *total_rows = rows;
}

void app_file_manager_adjust_scroll(PixelTermApp *app,
                                    gint total_entries,
                                    gint cols,
                                    gint visible_rows) {
    if (total_entries < 0) {
        total_entries = app->file_manager.entries_count;
    }
    gint total_rows = (total_entries + cols - 1) / cols;
    if (total_rows < 1) total_rows = 1;

    gint row = app->file_manager.selected_entry / cols;

    // Always aim to center the selection
    gint target_row = visible_rows / 2;
    gint desired_offset = row - target_row;

    // Limit the range of scroll offset
    // Allow scrolling past the bottom to keep the last item centered
    gint max_offset = MAX(0, total_rows - 1 - (visible_rows / 2));

    if (desired_offset < 0) desired_offset = 0;
    if (desired_offset > max_offset) desired_offset = max_offset;

    // Only update scroll offset when necessary to avoid frequent changes
    if (app->file_manager.scroll_offset != desired_offset) {
        app->file_manager.scroll_offset = desired_offset;
    }
}

// Select the current image in file manager (only when appropriate)
static void app_file_manager_select_current_image(PixelTermApp *app) {
    if (!app || !app->current_directory || !app->file_manager.directory) {
        return;
    }

    // Only select current image if we're returning from image view.
    // For initial file manager entry (RETURN_MODE_NONE), keep selected_entry = 0.
    if (app->return_to_mode == RETURN_MODE_NONE) {
        return;
    }

    if (g_strcmp0(app->current_directory, app->file_manager.directory) != 0) {
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
    for (GList *cur = app->file_manager.entries; cur; cur = cur->next, idx++) {
        gchar *path = (gchar*)cur->data;
        if (g_strcmp0(path, current_norm) == 0) {
            app->file_manager.selected_entry = idx;
            app->file_manager.selected_link = cur;
            app->file_manager.selected_link_index = idx;
            break;
        }
    }

    g_free(current_norm);

    // Adjust scroll so selection is visible
    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, -1, &col_width, &cols, &visible_rows, &total_rows);
    app_file_manager_adjust_scroll(app, -1, cols, visible_rows);
}

// Jump to next entry starting with a letter
ErrorCode app_file_manager_jump_to_letter(PixelTermApp *app, char letter) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_file_manager_mode(app)) {
        return ERROR_INVALID_ARGS;
    }

    gint total_entries = app->file_manager.entries_count;
    if (total_entries == 0) {
        return ERROR_NONE;
    }

    gchar target = g_ascii_tolower(letter);
    gint start = (app->file_manager.selected_entry + 1) % total_entries;
    GList *cursor = app_file_manager_find_link_with_hint(app,
                                                          start,
                                                          app->file_manager.selected_link,
                                                          app->file_manager.selected_link_index);
    if (!cursor) {
        cursor = app->file_manager.entries;
    }

    gint idx = start;
    for (gint visited = 0; visited < total_entries; visited++) {
        if (!cursor) {
            cursor = app->file_manager.entries;
            idx = 0;
        }
        gchar *entry = cursor ? (gchar*)cursor->data : NULL;
        if (entry) {
            gchar *base = g_path_get_basename(entry);
            if (base && base[0]) {
                gchar first = g_ascii_tolower(base[0]);
                if (first == target) {
                    app->file_manager.selected_entry = idx;
                    app->file_manager.selected_link = cursor;
                    app->file_manager.selected_link_index = idx;
                    g_free(base);
                    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
                    app_file_manager_layout(app, total_entries, &col_width, &cols, &visible_rows, &total_rows);

                    // Center selection vertically when possible
                    gint row = app->file_manager.selected_entry / cols;
                    gint offset = row - visible_rows / 2;
                    if (offset < 0) offset = 0;
                    gint max_offset = MAX(0, total_rows - visible_rows);
                    if (offset > max_offset) offset = max_offset;
                    app->file_manager.scroll_offset = offset;

                    app_file_manager_adjust_scroll(app, total_entries, cols, visible_rows);
                    return ERROR_NONE;
                }
            }
            g_free(base);
        }
        cursor = cursor ? cursor->next : NULL;
        idx++;
        if (idx >= total_entries) {
            idx = 0;
        }
    }

    return ERROR_NONE;
}

ErrorCode app_enter_file_manager(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }

    // Stop GIF playback if active
    if (app->gif_player) {
        gif_player_stop(app->gif_player);
    }
    if (app->video_player) {
        video_player_stop(app->video_player);
    }

    (void)app_transition_mode(app, APP_MODE_FILE_MANAGER);
    app->file_manager.selected_entry = 0;
    app_file_manager_invalidate_selection_cache(app);
    app->file_manager.scroll_offset = 0;

    if (app->file_manager.directory) {
        g_free(app->file_manager.directory);
        app->file_manager.directory = NULL;
    }
    // Start browsing from current image directory if available, else current working dir
    if (app->current_directory) {
        app->file_manager.directory = g_strdup(app->current_directory);
    } else {
        app->file_manager.directory = g_get_current_dir();
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

    (void)app_transition_mode(app, APP_MODE_SINGLE);
    app->input.file_manager_click.pending = FALSE;

    // Cleanup directory entries
    if (app->file_manager.entries) {
        g_list_free_full(app->file_manager.entries, (GDestroyNotify)g_free);
        app->file_manager.entries = NULL;
    }
    app->file_manager.entries_count = 0;
    app_file_manager_invalidate_selection_cache(app);
    g_clear_pointer(&app->file_manager.directory, g_free);

    // Reset info visibility to ensure proper display
    app->info_visible = FALSE;
    app->needs_redraw = TRUE;

    return ERROR_NONE;
}

// Move selection up in file manager
ErrorCode app_file_manager_up(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_file_manager_mode(app)) {
        return ERROR_INVALID_ARGS;
    }

    gint total_entries = app->file_manager.entries_count;
    if (total_entries <= 0) {
        return ERROR_NONE;
    }
    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, total_entries, &col_width, &cols, &visible_rows, &total_rows);

    if (app->file_manager.selected_entry >= cols) {
        app->file_manager.selected_entry -= cols;
    } else {
        // Jump to last entry when at the top and pressing up
        app->file_manager.selected_entry = total_entries - 1;
    }

    // Adjust scroll using the improved algorithm
    app_file_manager_adjust_scroll(app, total_entries, cols, visible_rows);

    return ERROR_NONE;
}

// Move selection down in file manager
ErrorCode app_file_manager_down(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_file_manager_mode(app)) {
        return ERROR_INVALID_ARGS;
    }

    gint total_entries = app->file_manager.entries_count;
    if (total_entries <= 0) {
        return ERROR_NONE;
    }
    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, total_entries, &col_width, &cols, &visible_rows, &total_rows);
    gint target = app->file_manager.selected_entry + cols;
    if (target < total_entries) {
        app->file_manager.selected_entry = target;
    } else {
        // Jump to first entry when at the bottom and pressing down
        app->file_manager.selected_entry = 0;
    }

    // Adjust scroll using the improved algorithm
    app_file_manager_adjust_scroll(app, total_entries, cols, visible_rows);

    return ERROR_NONE;
}

// Move selection left in file manager
ErrorCode app_file_manager_left(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_file_manager_mode(app)) {
        return ERROR_INVALID_ARGS;
    }

    const gchar *current_dir = app->file_manager.directory;
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
        g_free(app->file_manager.directory);
        app->file_manager.directory = g_canonicalize_filename(parent, NULL);
        if (!app->file_manager.directory) {
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
            for (GList *cur = app->file_manager.entries; cur; cur = cur->next, idx++) {
                gchar *path = (gchar*)cur->data;
                if (g_strcmp0(path, child_dir) == 0) {
                    app->file_manager.selected_entry = idx;
                    app->file_manager.selected_link = cur;
                    app->file_manager.selected_link_index = idx;
                    // Recalculate scroll to keep selection visible
                    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
                    app_file_manager_layout(app, -1, &col_width, &cols, &visible_rows, &total_rows);
                    app_file_manager_adjust_scroll(app, -1, cols, visible_rows);
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
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_file_manager_mode(app)) {
        return ERROR_INVALID_ARGS;
    }

    return app_file_manager_enter(app);
}

// Enter selected directory or open file in file manager
ErrorCode app_file_manager_enter(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_file_manager_mode(app)) {
        return ERROR_INVALID_ARGS;
    }

    if (app->file_manager.selected_entry < 0) {
        return ERROR_INVALID_IMAGE;
    }
    GList *selected_node = app_file_manager_get_selected_node(app);
    if (!selected_node) {
        return ERROR_INVALID_IMAGE;
    }

    gchar *selected_path = (gchar*)selected_node->data;
    if (!selected_path) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Check if it's a directory
    if (g_file_test(selected_path, G_FILE_TEST_IS_DIR)) {
        // Load the selected directory
        g_free(app->file_manager.directory);
        app->file_manager.directory = g_canonicalize_filename(selected_path, NULL);
        if (!app->file_manager.directory) {
            return ERROR_FILE_NOT_FOUND;
        }
        // Reset selection to first entry when changing directory
        app->file_manager.selected_entry = 0;
        app_file_manager_invalidate_selection_cache(app);
        app->file_manager.scroll_offset = 0;
        // Refresh file manager with new directory
        return app_file_manager_refresh(app);
    } else {
        if (is_valid_book_file(selected_path)) {
            ErrorCode error = app_open_book(app, selected_path);
            if (error != ERROR_NONE) {
                return error;
            }

            printf("\033[2J\033[H\033[0m");
            fflush(stdout);

            (void)app_transition_mode(app, APP_MODE_SINGLE);
            if (app->file_manager.entries) {
                g_list_free_full(app->file_manager.entries, (GDestroyNotify)g_free);
                app->file_manager.entries = NULL;
            }
            app->file_manager.entries_count = 0;
            app_file_manager_invalidate_selection_cache(app);
            g_clear_pointer(&app->file_manager.directory, g_free);
            app->info_visible = FALSE;
            app->needs_redraw = TRUE;

            app_enter_book_preview(app);
            app_render_book_preview(app);
            fflush(stdout);
            return ERROR_NONE;
        }

        // It's a file, check if it's a valid image file before loading
        if (!is_valid_media_file(selected_path)) {
            // If it's not a valid media file, don't try to load it
            return ERROR_INVALID_IMAGE;
        }

        ErrorCode error = app_load_single_file(app, selected_path);

        // Only exit file manager if file was loaded successfully
        if (error == ERROR_NONE) {
            // Clear screen before exiting file manager
            printf("\033[2J\033[H\033[0m");
            fflush(stdout);

            // Exit file manager mode
            (void)app_transition_mode(app, APP_MODE_SINGLE);
            // Cleanup directory entries
            if (app->file_manager.entries) {
                g_list_free_full(app->file_manager.entries, (GDestroyNotify)g_free);
                app->file_manager.entries = NULL;
            }
            app->file_manager.entries_count = 0;
            app_file_manager_invalidate_selection_cache(app);
            g_clear_pointer(&app->file_manager.directory, g_free);
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
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_file_manager_mode(app)) {
        return ERROR_INVALID_ARGS;
    }

    // Clear existing entries
    if (app->file_manager.entries) {
        g_list_free_full(app->file_manager.entries, (GDestroyNotify)g_free);
        app->file_manager.entries = NULL;
    }
    app->file_manager.entries_count = 0;
    app_file_manager_invalidate_selection_cache(app);

    // Resolve directory to display: prefer file manager dir, then viewer dir, then cwd
    gchar *base_dir_dup = NULL;
    if (app->file_manager.directory) {
        base_dir_dup = g_strdup(app->file_manager.directory);
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
    g_free(app->file_manager.directory);
    app->file_manager.directory = current_dir;

    // Open directory
    GDir *dir = g_dir_open(current_dir, 0, NULL);
    if (!dir) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Collect all entries
    GList *entries = NULL;
    gint entries_count = 0;
    gchar *parent_entry = NULL;
    gchar *parent_dir = g_path_get_dirname(current_dir);
    if (parent_dir) {
        if (g_strcmp0(parent_dir, current_dir) != 0) {
            parent_entry = g_build_filename(current_dir, "..", NULL);
            entries = g_list_prepend(entries, parent_entry);
            entries_count++;
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
            entries = g_list_prepend(entries, full_path);  // Order is normalized after sort
            entries_count++;
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
    app->file_manager.entries = g_list_concat(dirs, files);
    g_list_free(entries); // pointers moved into dirs/files concatenated list
    app->file_manager.entries_count = entries_count;
    app->file_manager.selected_entry = 0;
    app->file_manager.selected_link = app->file_manager.entries;
    app->file_manager.selected_link_index = app->file_manager.selected_link ? 0 : -1;
    app->file_manager.scroll_offset = 0;
    app_file_manager_select_current_image(app);

    // Default: avoid selecting ".." when entering a directory; pick the first real entry.
    if (app->file_manager.entries && app->file_manager.selected_entry == 0) {
        const gchar *first = (const gchar*)app->file_manager.entries->data;
        if (first && app->file_manager.entries->next) {
            gchar *base = g_path_get_basename(first);
            gboolean is_parent = (base && g_strcmp0(base, "..") == 0);
            g_free(base);
            if (is_parent) {
                app->file_manager.selected_entry = 1;
                app->file_manager.selected_link = app->file_manager.entries->next;
                app->file_manager.selected_link_index = app->file_manager.selected_link ? 1 : -1;
            }
        }
    }

    return ERROR_NONE;
}

ErrorCode app_file_manager_select_path(PixelTermApp *app, const char *path) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_file_manager_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (!path || !app->file_manager.entries) {
        return ERROR_FILE_NOT_FOUND;
    }

    gchar *target = g_canonicalize_filename(path, NULL);
    if (!target) {
        return ERROR_FILE_NOT_FOUND;
    }

    gboolean found = FALSE;
    gint idx = 0;
    for (GList *cur = app->file_manager.entries; cur; cur = cur->next, idx++) {
        gchar *entry = (gchar*)cur->data;
        if (g_strcmp0(entry, target) == 0) {
            app->file_manager.selected_entry = idx;
            app->file_manager.selected_link = cur;
            app->file_manager.selected_link_index = idx;
            found = TRUE;
            break;
        }
    }
    g_free(target);

    if (!found) {
        return ERROR_FILE_NOT_FOUND;
    }

    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, -1, &col_width, &cols, &visible_rows, &total_rows);
    app_file_manager_adjust_scroll(app, -1, cols, visible_rows);
    return ERROR_NONE;
}

// Check if current file manager directory contains any images
gboolean app_file_manager_has_images(PixelTermApp *app) {
    if (!app || !app->file_manager.entries) {
        return FALSE;
    }

    // Check if any entry in the current directory listing is an image file
    for (GList *cur = app->file_manager.entries; cur; cur = cur->next) {
        gchar *path = (gchar*)cur->data;
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR) && is_valid_media_file(path)) {
            return TRUE;
        }
    }

    return FALSE;
}

// Check if current file manager selection is an image file
gboolean app_file_manager_selection_is_image(PixelTermApp *app) {
    if (!app_is_file_manager_mode(app) || !app->file_manager.entries) {
        return FALSE;
    }

    if (app->file_manager.selected_entry < 0) {
        return FALSE;
    }

    GList *node = app_file_manager_get_selected_node(app);
    gchar *path = node ? (gchar*)node->data : NULL;
    if (!path) {
        return FALSE;
    }

    return g_file_test(path, G_FILE_TEST_IS_REGULAR) && is_valid_media_file(path);
}

// Get the image index of the currently selected file in file manager
gint app_file_manager_get_selected_image_index(PixelTermApp *app) {
    if (!app_is_file_manager_mode(app) || !app->file_manager.entries || !app->image_files) {
        return -1;
    }

    if (app->file_manager.selected_entry < 0) {
        return -1;
    }

    GList *selected_node = app_file_manager_get_selected_node(app);
    gchar *selected_path = selected_node ? (gchar*)selected_node->data : NULL;
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
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_file_manager_mode(app)) {
        return ERROR_INVALID_ARGS;
    }

    // Remember current selection path
    gchar *prev_selected = NULL;
    if (app->file_manager.selected_entry >= 0 && app->file_manager.entries) {
        GList *node = app_file_manager_get_selected_node(app);
        gchar *path = node ? (gchar*)node->data : NULL;
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
        for (GList *cur = app->file_manager.entries; cur; cur = cur->next, idx++) {
            gchar *path = (gchar*)cur->data;
            if (g_strcmp0(path, prev_selected) == 0) {
                app->file_manager.selected_entry = idx;
                app->file_manager.selected_link = cur;
                app->file_manager.selected_link_index = idx;
                break;
            }
        }
        g_free(prev_selected);
    }

    // Ensure scroll offset keeps selection visible
    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, -1, &col_width, &cols, &visible_rows, &total_rows);
    app_file_manager_adjust_scroll(app, -1, cols, visible_rows);

    return ERROR_NONE;
}

// Handle mouse click in file manager
