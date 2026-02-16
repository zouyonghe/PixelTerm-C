#include "app.h"
#include "text_utils.h"
#include "ui_render_utils.h"

static void app_book_toc_layout(const PixelTermApp *app,
                                gint *content_rows,
                                gint *list_top_row,
                                gint *list_bottom_row,
                                gint *header_lines,
                                gint *footer_lines) {
    gint rows = (app && app->term_height > 0) ? app->term_height : 1;
    gint header = 4;
    gint footer = 4;

    gint top = 5;
    gint bottom = rows - 4;
    if (bottom < top) {
        bottom = top;
    }
    gint available = bottom - top + 1;
    if (available < 1) {
        available = 1;
    }

    if (content_rows) {
        *content_rows = available;
    }
    if (list_top_row) {
        *list_top_row = top;
    }
    if (list_bottom_row) {
        *list_bottom_row = bottom;
    }
    if (header_lines) {
        *header_lines = header;
    }
    if (footer_lines) {
        *footer_lines = footer;
    }
}

static void app_book_toc_adjust_scroll(PixelTermApp *app, gint visible_rows) {
    if (!app || !app->book.toc) {
        return;
    }

    gint total = app->book.toc->count;
    if (total <= 0) {
        app->book.toc_selected = 0;
        app->book.toc_scroll = 0;
        return;
    }

    if (visible_rows < 1) {
        visible_rows = 1;
    }

    if (app->book.toc_selected < 0) {
        app->book.toc_selected = 0;
    } else if (app->book.toc_selected >= total) {
        app->book.toc_selected = total - 1;
    }

    if (total <= visible_rows) {
        app->book.toc_scroll = 0;
        return;
    }

    gint target_row = visible_rows / 2;
    gint desired_offset = app->book.toc_selected - target_row;
    gint max_offset = MAX(0, total - 1 - target_row);

    if (desired_offset < 0) desired_offset = 0;
    if (desired_offset > max_offset) desired_offset = max_offset;

    if (app->book.toc_scroll != desired_offset) {
        app->book.toc_scroll = desired_offset;
    }
}

static BookTocItem* app_book_toc_item_at(BookToc *toc, gint index) {
    if (!toc || index < 0) {
        return NULL;
    }
    BookTocItem *item = toc->items;
    gint idx = 0;
    while (item && idx < index) {
        item = item->next;
        idx++;
    }
    return item;
}

typedef struct {
    gint total_entries;
    gint start_row;
    gint end_row;
    gint rows_to_render;
    gint top_padding;
} BookTocViewport;

static BookTocViewport app_book_toc_compute_viewport(PixelTermApp *app, gint visible_rows) {
    BookTocViewport viewport = {0};
    if (!app || !app->book.toc) {
        return viewport;
    }

    viewport.total_entries = app->book.toc->count;
    gint available_rows = visible_rows;
    if (available_rows < 0) {
        available_rows = 0;
    }

    gint max_offset = MAX(0, viewport.total_entries - 1);
    gint scroll_offset = app->book.toc_scroll;
    if (scroll_offset > max_offset) {
        scroll_offset = max_offset;
    }
    if (scroll_offset < 0) {
        scroll_offset = 0;
    }

    gint start_row = scroll_offset;
    if (viewport.total_entries <= 0) {
        start_row = 0;
    } else if (start_row >= viewport.total_entries) {
        start_row = viewport.total_entries - 1;
    }

    gint end_row = MIN(start_row + available_rows, viewport.total_entries);
    gint rows_to_render = end_row - start_row;
    if (rows_to_render < 0) {
        rows_to_render = 0;
    }

    gint selected_row = app->book.toc_selected;
    if (selected_row < 0) {
        selected_row = 0;
    } else if (viewport.total_entries > 0 && selected_row >= viewport.total_entries) {
        selected_row = viewport.total_entries - 1;
    } else if (viewport.total_entries == 0) {
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
        gint more_rows_below = MAX(0, viewport.total_entries - end_row);
        gint scroll_shift = MIN(-top_padding, more_rows_below);
        if (scroll_shift > 0) {
            start_row += scroll_shift;
            end_row = MIN(start_row + available_rows, viewport.total_entries);
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
        end_row = MIN(viewport.total_entries, start_row + visible_space);
        rows_to_render = end_row - start_row;
        if (rows_to_render < 0) {
            rows_to_render = 0;
        }
    }

    viewport.start_row = start_row;
    viewport.end_row = end_row;
    viewport.rows_to_render = rows_to_render;
    viewport.top_padding = top_padding;
    return viewport;
}

static gboolean app_book_toc_hit_test(PixelTermApp *app,
                                      gint mouse_x,
                                      gint mouse_y,
                                      gint *out_index) {
    (void)mouse_x;
    if (!app || !app->book.toc || !app->book.toc_visible) {
        return FALSE;
    }

    get_terminal_size(&app->term_width, &app->term_height);
    gint content_rows = 1;
    gint list_top_row = 1;
    gint list_bottom_row = app->term_height;
    app_book_toc_layout(app, &content_rows, &list_top_row, &list_bottom_row, NULL, NULL);

    if (mouse_y < list_top_row || mouse_y > list_bottom_row) {
        return FALSE;
    }

    gint row_idx = mouse_y - list_top_row;
    if (row_idx < 0 || row_idx >= content_rows) {
        return FALSE;
    }

    BookTocViewport viewport = app_book_toc_compute_viewport(app, content_rows);
    if (row_idx < viewport.top_padding) {
        return FALSE;
    }

    gint relative_row = row_idx - viewport.top_padding;
    if (relative_row < 0 || relative_row >= viewport.rows_to_render) {
        return FALSE;
    }

    gint absolute_row = viewport.start_row + relative_row;
    if (absolute_row < 0 || absolute_row >= viewport.total_entries) {
        return FALSE;
    }

    if (out_index) {
        *out_index = absolute_row;
    }
    return TRUE;
}

ErrorCode app_book_toc_move_selection(PixelTermApp *app, gint delta) {
    if (!app || !app->book.toc) {
        return ERROR_MEMORY_ALLOC;
    }
    gint total = app->book.toc->count;
    if (total <= 0) {
        app->book.toc_selected = 0;
        app->book.toc_scroll = 0;
        return ERROR_NONE;
    }
    gint visible_rows = 1;
    app_book_toc_layout(app, &visible_rows, NULL, NULL, NULL, NULL);
    if (visible_rows < 1) {
        visible_rows = 1;
    }

    gint next = app->book.toc_selected + delta;
    next %= total;
    if (next < 0) {
        next += total;
    }
    app->book.toc_selected = next;
    app_book_toc_adjust_scroll(app, visible_rows);
    return ERROR_NONE;
}

ErrorCode app_book_toc_page_move(PixelTermApp *app, gint direction) {
    if (!app || !app->book.toc) {
        return ERROR_MEMORY_ALLOC;
    }
    gint visible_rows = 1;
    app_book_toc_layout(app, &visible_rows, NULL, NULL, NULL, NULL);
    if (visible_rows < 1) {
        visible_rows = 1;
    }

    gint delta = (direction >= 0) ? visible_rows : -visible_rows;
    return app_book_toc_move_selection(app, delta);
}

ErrorCode app_book_toc_sync_to_page(PixelTermApp *app, gint page_index) {
    if (!app || !app->book.toc) {
        return ERROR_MEMORY_ALLOC;
    }
    gint total = app->book.toc->count;
    if (total <= 0) {
        app->book.toc_selected = 0;
        app->book.toc_scroll = 0;
        return ERROR_NONE;
    }

    gint selected = 0;
    BookTocItem *item = app->book.toc->items;
    gint index = 0;
    while (item) {
        if (item->page <= page_index) {
            selected = index;
        } else {
            break;
        }
        item = item->next;
        index++;
    }
    app->book.toc_selected = selected;

    gint visible_rows = 1;
    app_book_toc_layout(app, &visible_rows, NULL, NULL, NULL, NULL);
    if (visible_rows < 1) {
        visible_rows = 1;
    }
    app_book_toc_adjust_scroll(app, visible_rows);
    return ERROR_NONE;
}

gint app_book_toc_get_selected_page(PixelTermApp *app) {
    if (!app || !app->book.toc) {
        return -1;
    }
    BookTocItem *item = app_book_toc_item_at(app->book.toc, app->book.toc_selected);
    if (!item) {
        return -1;
    }
    return item->page;
}

ErrorCode app_handle_mouse_click_book_toc(PixelTermApp *app,
                                          gint mouse_x,
                                          gint mouse_y,
                                          gboolean *redraw_needed,
                                          gboolean *out_hit) {
    if (redraw_needed) {
        *redraw_needed = FALSE;
    }
    if (out_hit) {
        *out_hit = FALSE;
    }
    if (!app || !app->book.toc_visible || !app->book.toc) {
        return ERROR_MEMORY_ALLOC;
    }

    gint index = -1;
    if (!app_book_toc_hit_test(app, mouse_x, mouse_y, &index)) {
        return ERROR_NONE;
    }

    if (out_hit) {
        *out_hit = TRUE;
    }

    gint old_selected = app->book.toc_selected;
    gint old_scroll = app->book.toc_scroll;
    app->book.toc_selected = index;

    gint visible_rows = 1;
    app_book_toc_layout(app, &visible_rows, NULL, NULL, NULL, NULL);
    if (visible_rows < 1) {
        visible_rows = 1;
    }
    app_book_toc_adjust_scroll(app, visible_rows);

    if (redraw_needed &&
        (app->book.toc_selected != old_selected || app->book.toc_scroll != old_scroll)) {
        *redraw_needed = TRUE;
    }
    return ERROR_NONE;
}

// Render book table of contents
ErrorCode app_render_book_toc(PixelTermApp *app) {
    if (!app || !app->book.toc) {
        return ERROR_MEMORY_ALLOC;
    }

    get_terminal_size(&app->term_width, &app->term_height);
    ui_begin_sync_update();
    ui_clear_kitty_images(app);
    ui_clear_screen_for_refresh(app);

    gint rows = app->term_height;
    gint cols = app->term_width;
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    gint content_rows = 1;
    gint list_top_row = 1;
    gint list_bottom_row = rows;
    gint footer_lines = 0;
    app_book_toc_layout(app, &content_rows, &list_top_row, &list_bottom_row, NULL, &footer_lines);

    app_book_toc_adjust_scroll(app, content_rows);
    BookTocViewport viewport = app_book_toc_compute_viewport(app, content_rows);
    app->book.toc_scroll = viewport.start_row;

    const char *header_title = "Table of Contents";
    gint title_len = (gint)strlen(header_title);
    gint title_pad = (cols > title_len) ? (cols - title_len) / 2 : 0;
    printf("\033[1;1H\033[2K");
    for (gint i = 0; i < title_pad; i++) putchar(' ');
    printf("%s", header_title);
    printf("\033[2;1H\033[2K");

    gchar *base = app->book.path ? g_path_get_basename(app->book.path) : g_strdup("");
    if (base && base[0]) {
        char *dot = strrchr(base, '.');
        if (dot && dot != base) {
            *dot = '\0';
        }
    }
    gchar *safe_name = sanitize_for_terminal(base);
    gchar *display_name = truncate_utf8_for_display(safe_name, cols > 8 ? cols - 8 : cols);
    gint name_len = utf8_display_width(display_name);
    gint name_pad = (cols > name_len) ? (cols - name_len) / 2 : 0;
    printf("\033[3;1H\033[2K");
    for (gint i = 0; i < name_pad; i++) putchar(' ');
    printf("%s", display_name);
    printf("\033[4;1H\033[2K");
    g_free(display_name);
    g_free(safe_name);
    g_free(base);

    for (gint row = list_top_row; row <= list_bottom_row; row++) {
        printf("\033[%d;1H\033[2K", row);
    }

    gint total_entries = app->book.toc->count;
    if (total_entries <= 0 || !app->book.toc->items) {
        if (rows > 0) {
            const char *empty_msg = "(No contents)";
            gint msg_len = utf8_display_width(empty_msg);
            gint center_pad = (cols > msg_len) ? (cols - msg_len) / 2 : 0;
            gint target_row = list_top_row + (content_rows / 2);
            if (target_row < 1) target_row = 1;
            if (target_row > rows) target_row = rows;
            printf("\033[%d;1H\033[2K", target_row);
            for (gint i = 0; i < center_pad; i++) putchar(' ');
            printf("\033[33m%s\033[0m", empty_msg);
        }
    } else {
        gint page_digits = 1;
        gint pages = app->book.page_count > 0 ? app->book.page_count : 1;
        while (pages >= 10) {
            page_digits++;
            pages /= 10;
        }
        gint page_width = MAX(3, page_digits);
        const gint prefix_width = 2;
        const gint gap_width = 2;
        gint max_line_width = cols;

        gint visible_start = viewport.start_row;
        gint visible_end = viewport.end_row;

        gint line_content_width = 0;
        BookTocItem *scan = app->book.toc->items;
        while (scan) {
            gint indent = scan->level * 2;
            gint max_indent = cols / 4;
            if (indent > max_indent) indent = max_indent;

            gint title_max = max_line_width - prefix_width - indent - gap_width - page_width;
            if (title_max < 1) title_max = 1;

            gchar *safe_title = sanitize_for_terminal(scan->title ? scan->title : "Untitled");
            gchar *display_title = truncate_utf8_for_display(safe_title, title_max);
            gint title_len = utf8_display_width(display_title);

            gint line_width = prefix_width + indent + title_len + gap_width + page_width;
            if (line_width > line_content_width) {
                line_content_width = line_width;
            }

            g_free(display_title);
            g_free(safe_title);
            scan = scan->next;
        }
        if (line_content_width < 1) {
            line_content_width = MIN(max_line_width, prefix_width + gap_width + page_width + 1);
        }
        if (line_content_width > max_line_width) {
            line_content_width = max_line_width;
        }
        gint line_pad = (cols > line_content_width) ? (cols - line_content_width) / 2 : 0;

        BookTocItem *item = app_book_toc_item_at(app->book.toc, visible_start);
        gint index = visible_start;
        gint display_row = list_top_row + viewport.top_padding;

        while (item && index < visible_end) {
            if (display_row > list_bottom_row) {
                break;
            }

                gboolean is_selected = (index == app->book.toc_selected);
                gint indent = item->level * 2;
                gint max_indent = cols / 4;
                if (indent > max_indent) indent = max_indent;

                gint title_max = line_content_width - prefix_width - indent - gap_width - page_width;
                if (title_max < 1) title_max = 1;

                gchar *safe_title = sanitize_for_terminal(item->title ? item->title : "Untitled");
                gchar *display_title = truncate_utf8_for_display(safe_title, title_max);
                gint title_len = utf8_display_width(display_title);

                printf("\033[%d;1H", display_row);
                for (gint i = 0; i < line_pad; i++) putchar(' ');
                if (is_selected) {
                    printf("\033[47;30m");
                }
                printf("  ");
                for (gint i = 0; i < indent; i++) putchar(' ');
                printf("%s", display_title);

                gint fill = line_content_width - (prefix_width + indent + title_len + gap_width + page_width);
                if (fill < 0) fill = 0;
                for (gint i = 0; i < gap_width + fill; i++) putchar(' ');
                printf("%*d", page_width, item->page + 1);

                if (is_selected) {
                    printf("\033[0m");
                }

                g_free(display_title);
                g_free(safe_title);
            item = item->next;
            index++;
            display_row++;
        }
    }

    for (gint y = MAX(1, rows - 3); y <= rows - 1; y++) {
        printf("\033[%d;1H\033[2K", y);
    }

    if (footer_lines > 0) {
        const HelpSegment segments[] = {
            {"↑/↓", "Move"},
            {"PgUp/PgDn", "Page"},
            {"Home/End", "Top/Bottom"},
            {"Enter", "Open"},
            {"T/ESC", "Close"},
        };
        ui_print_centered_help_line(rows, cols, segments, G_N_ELEMENTS(segments));
    }

    ui_end_sync_update();
    fflush(stdout);
    return ERROR_NONE;
}
