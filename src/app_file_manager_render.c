#include "app.h"
#include "text_utils.h"
#include "app_file_manager_internal.h"

static gboolean directory_contains_media(const gchar *dir_path) {
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
        if (g_file_test(full_path, G_FILE_TEST_IS_REGULAR) && is_media_file(full_path)) {
            g_free(full_path);
            g_dir_close(dir);
            return TRUE;
        }
        g_free(full_path);
    }

    g_dir_close(dir);
    return FALSE;
}

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

static gboolean directory_contains_books(const gchar *dir_path) {
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
        if (g_file_test(full_path, G_FILE_TEST_IS_REGULAR) && is_book_file(full_path)) {
            g_free(full_path);
            g_dir_close(dir);
            return TRUE;
        }
        g_free(full_path);
    }

    g_dir_close(dir);
    return FALSE;
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

    viewport.total_entries = app->file_manager.entries_count;
    app_file_manager_layout(app, viewport.total_entries, &viewport.col_width, &viewport.cols,
                            &viewport.visible_rows, &viewport.total_rows);

    gint available_rows = viewport.visible_rows;
    if (available_rows < 0) {
        available_rows = 0;
    }

    gint max_offset = MAX(0, viewport.total_rows - 1);
    gint scroll_offset = app->file_manager.scroll_offset;
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

    gint selected_row = app->file_manager.selected_entry;
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
    if (!app_is_file_manager_mode(app)) {
        return FALSE;
    }

    get_terminal_size(&app->term_width, &app->term_height);
    FileManagerViewport viewport = app_file_manager_compute_viewport(app);

    // Header occupies four rows; list starts at row 5 and ends at row (term_height - 4)
    const gint header_lines = 4;
    const gint list_top_row = header_lines + 1;
    gint list_bottom_row = app->term_height - 4;
    if (list_bottom_row < list_top_row) {
        list_bottom_row = list_top_row;
    }
    if (mouse_y < list_top_row || mouse_y > list_bottom_row) {
        return FALSE;
    }

    gint row_idx = mouse_y - list_top_row;
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


ErrorCode app_handle_mouse_file_manager(PixelTermApp *app, gint mouse_x, gint mouse_y) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_file_manager_mode(app)) {
        return ERROR_INVALID_ARGS;
    }

    gint hit_index = -1;
    if (!app_file_manager_hit_test(app, mouse_x, mouse_y, &hit_index)) {
        return ERROR_NONE;
    }

    if (hit_index == app->file_manager.selected_entry) {
        return ERROR_NONE;
    }

    app->file_manager.selected_entry = hit_index;
    app_file_manager_invalidate_selection_cache(app);

    gint col_width = 0, cols = 0, visible_rows = 0, total_rows = 0;
    app_file_manager_layout(app, -1, &col_width, &cols, &visible_rows, &total_rows);
    app_file_manager_adjust_scroll(app, -1, cols, visible_rows);

    return ERROR_NONE;
}

ErrorCode app_file_manager_enter_at_position(PixelTermApp *app, gint mouse_x, gint mouse_y) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_file_manager_mode(app)) {
        return ERROR_INVALID_ARGS;
    }

    gint hit_index = -1;
    if (!app_file_manager_hit_test(app, mouse_x, mouse_y, &hit_index)) {
        return ERROR_INVALID_IMAGE;
    }

    gint prev_selected = app->file_manager.selected_entry;
    gint prev_scroll = app->file_manager.scroll_offset;
    app->file_manager.selected_entry = hit_index;
    app_file_manager_invalidate_selection_cache(app);
    ErrorCode err = app_file_manager_enter(app);

    if (err != ERROR_NONE && app_is_file_manager_mode(app)) {
        app->file_manager.selected_entry = prev_selected;
        app_file_manager_invalidate_selection_cache(app);
        app->file_manager.scroll_offset = prev_scroll;
    }

    return err;
}

ErrorCode app_render_file_manager(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_file_manager_mode(app)) {
        return ERROR_INVALID_ARGS;
    }

    // Update terminal dimensions before layout
    get_terminal_size(&app->term_width, &app->term_height);

    // Don't do a full-screen clear on every navigation step; we explicitly clear/redraw
    // the rows we touch to keep movement smooth and avoid extra terminal workarounds.
    printf("\033[H\033[0m");

    // Get current directory
    const gchar *current_dir = app->file_manager.directory;
    gboolean free_dir = FALSE;
    if (!current_dir) {
        current_dir = app->current_directory;
        if (!current_dir) {
            current_dir = g_get_current_dir();
            free_dir = TRUE;
        }
    }
    gchar *safe_current_dir = sanitize_for_terminal(current_dir);

    // Header centered: row 1 app name, row 3 current directory (row 2/4 blank)
    const char *header_title = "PixelTerm File Manager";
    gint title_len = strlen(header_title);
    gint title_pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
    printf("\033[1;1H\033[2K");
    for (gint i = 0; i < title_pad; i++) putchar(' ');
    printf("%s", header_title);
    printf("\033[2;1H\033[2K"); // blank line for symmetry

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
    // Print the centered directory path on row 3
    printf("\033[3;1H\033[2K");
    for (gint i = 0; i < dir_pad; i++) putchar(' ');
    printf("%s", display_dir);
    printf("\033[4;1H\033[2K"); // blank line for symmetry

    // Free the truncated directory path if it was created
    if (display_dir != safe_current_dir) {
        g_free(display_dir);
    }

    FileManagerViewport viewport = app_file_manager_compute_viewport(app);
    app->file_manager.scroll_offset = viewport.start_row;
    gint total_entries = viewport.total_entries;
    const char *help_text = "↑/↓ Move   ← Parent   →/Enter Open   TAB Toggle   Ctrl+H Hidden   ESC Exit";
    gint start_row = viewport.start_row;
    gint end_row = viewport.end_row;
    gint rows_to_render = viewport.rows_to_render;
    gint top_padding = viewport.top_padding;
    gint bottom_padding = viewport.bottom_padding;

    // Render list within fixed rows [5 .. term_height-4] to keep symmetry and avoid scrolling
    const gint list_top_row = 5;
    gint list_bottom_row = app->term_height - 4;
    if (list_bottom_row < list_top_row) {
        list_bottom_row = list_top_row;
    }
    gint list_visible_rows = list_bottom_row - list_top_row + 1;
    GList *render_cursor = NULL;
    if (total_entries > 0 && start_row >= 0) {
        render_cursor = app_file_manager_find_link_with_hint(app,
                                                             start_row,
                                                             app->file_manager.selected_link,
                                                             app->file_manager.selected_link_index);
    }

    for (gint i = 0; i < list_visible_rows; i++) {
        gint y = list_top_row + i;
        printf("\033[%d;1H\033[2K", y);

        if (total_entries == 0) {
            if (i == list_visible_rows / 2) {
                const char *empty_msg = "（No items）";
                gint msg_len = utf8_display_width(empty_msg);
                gint center_pad = (app->term_width > msg_len) ? (app->term_width - msg_len) / 2 : 0;
                for (gint s = 0; s < center_pad; s++) putchar(' ');
                printf("\033[33m%s\033[0m", empty_msg);
            }
            continue;
        }

        if (i < top_padding) {
            continue;
        }
        gint relative_row = i - top_padding;
        if (relative_row < 0 || relative_row >= (end_row - start_row)) {
            continue;
        }
        gint idx = start_row + relative_row; // single column
        if (idx < 0 || idx >= total_entries) {
            continue;
        }

        gchar *entry = render_cursor ? (gchar*)render_cursor->data : NULL;
        if (render_cursor) {
            render_cursor = render_cursor->next;
        }
        if (!entry) {
            continue;
        }
        gboolean is_dir = FALSE;
        gchar *display_name = app_file_manager_display_name(app, entry, &is_dir);
        gchar *print_name = sanitize_for_terminal(display_name);
        gint name_len = utf8_display_width(print_name);
        gboolean is_image = (!is_dir && is_image_file(entry));
        gboolean is_video = (!is_dir && is_video_file(entry));
        gboolean is_book = (!is_dir && is_book_file(entry));
        gboolean is_dir_with_images = is_dir && directory_contains_images(entry);
        gboolean is_dir_with_media = is_dir && directory_contains_media(entry);
        gboolean is_dir_with_books = is_dir && directory_contains_books(entry);

        gint max_display_width = (app->term_width / 2) - 2;
        if (max_display_width < 15) max_display_width = 15;

        if (name_len > max_display_width) {
            gint max_display = max_display_width - 3;
            if (max_display > 8) {
                gint start_len = max_display / 2;
                gint end_len = max_display - start_len;

                gint char_count = 0;
                const gchar *p = print_name;
                while (*p) {
                    gunichar ch = g_utf8_get_char_validated(p, -1);
                    if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                        char_count++;
                        p++;
                    } else {
                        char_count++;
                        p = g_utf8_next_char(p);
                    }
                }

                gint start_byte = 0;
                gint current_char = 0;
                p = print_name;
                while (*p && current_char < start_len) {
                    gunichar ch = g_utf8_get_char_validated(p, -1);
                    if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                        p++;
                    } else {
                        p = g_utf8_next_char(p);
                    }
                    current_char++;
                }
                start_byte = p - print_name;

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

                current_char = 0;
                end_p = print_name;
                while (*end_p && current_char < char_count - end_len) {
                    gunichar ch = g_utf8_get_char_validated(end_p, -1);
                    if (ch == (gunichar)-1 || ch == (gunichar)-2) {
                        end_p++;
                    } else {
                        end_p = g_utf8_next_char(end_p);
                    }
                    current_char++;
                }
                gint end_byte = end_p - print_name;

                gchar *start_part = g_strndup(print_name, start_byte);
                gchar *end_part = g_strdup(print_name + end_byte);
                g_free(print_name);
                print_name = g_strdup_printf("%s...%s", start_part, end_part);
                g_free(start_part);
                g_free(end_part);
            } else {
                gint truncate_len = max_display;
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
            name_len = utf8_display_width(print_name);
        }

        gint pad = (app->term_width > name_len) ? (app->term_width - name_len) / 2 : 0;
        if (pad + name_len > app->term_width) {
            pad = MAX(0, app->term_width - name_len);
        }
        for (gint s = 0; s < pad; s++) putchar(' ');

        gboolean is_valid_media = (!is_dir && is_valid_media_file(entry));
        gboolean is_valid_book = (!is_dir && is_valid_book_file(entry));
        gboolean selected = (idx == app->file_manager.selected_entry);
        if ((is_image || is_video) && !is_valid_media) {
            if (selected) {
                printf("\033[47;30m%s\033[0m\033[31m [Invalid]\033[0m", print_name);
            } else {
                printf("\033[31m%s [Invalid]\033[0m", print_name);
            }
        } else if (is_book && !is_valid_book) {
            if (selected) {
                printf("\033[47;30m%s\033[0m\033[31m [Invalid]\033[0m", print_name);
            } else {
                printf("\033[31m%s [Invalid]\033[0m", print_name);
            }
        } else if (selected) {
            printf("\033[47;30m%s\033[0m", print_name);
        } else if (is_dir_with_images || is_dir_with_media || is_dir_with_books) {
            printf("\033[33m%s\033[0m", print_name);
        } else if (is_dir) {
            printf("\033[34m%s\033[0m", print_name);
        } else if (is_image) {
            printf("\033[32m%s\033[0m", print_name);
        } else if (is_video) {
            printf("\033[35m%s\033[0m", print_name);
        } else if (is_book) {
            printf("\033[36m%s\033[0m", print_name);
        } else {
            printf("%s", print_name);
        }


        g_free(print_name);
        g_free(display_name);
    }

    // Footer area: keep symmetry (rows term_height-3 .. term_height-1 blank) and help on last line
    for (gint y = MAX(1, app->term_height - 3); y <= app->term_height - 1; y++) {
        printf("\033[%d;1H\033[2K", y);
    }

    gint help_len = strlen(help_text);
    gint help_pad = (app->term_width > help_len) ? (app->term_width - help_len) / 2 : 0;
    printf("\033[%d;1H\033[2K", app->term_height);
    for (gint i = 0; i < help_pad; i++) putchar(' ');
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
