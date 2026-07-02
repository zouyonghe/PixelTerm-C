#include "ui_render_utils.h"

#include "text_utils.h"

void ui_render_centered_row(gint row, gint term_width, const char *text, const char *style) {
    if (row <= 0 || term_width <= 0) {
        return;
    }

    gchar *safe = sanitize_for_terminal(text ? text : "");
    gchar *display = truncate_utf8_for_display(safe, term_width);
    gint text_width = utf8_display_width(display);
    gint pad = (term_width > text_width) ? (term_width - text_width) / 2 : 0;

    printf("\033[%d;1H\033[2K", row);
    for (gint i = 0; i < pad; i++) {
        putchar(' ');
    }
    if (display[0] != '\0') {
        if (style) {
            printf("%s%s\033[0m", style, display);
        } else {
            printf("%s", display);
        }
    }

    g_free(display);
    g_free(safe);
}

gint ui_filename_max_width(const PixelTermApp *app) {
    if (!app || app->term_width <= 0) {
        return 0;
    }
    gint limit = (app->term_width * 4) / 5;
    if (limit < 1) {
        limit = app->term_width;
    }
    if (limit < 1) {
        limit = 1;
    }
    return limit;
}

gint ui_single_view_content_top_row(const PixelTermApp *app) {
    (void)app;
    return 4;
}

gint ui_single_view_bottom_reserved_lines(const PixelTermApp *app) {
    (void)app;
    return 3;
}

gint ui_preview_header_lines(const PixelTermApp *app) {
    return (app && app->ui_text_hidden) ? 0 : 3;
}

static gint ui_help_segments_visible_width(const HelpSegment *segments, gsize n) {
    if (!segments || n == 0) {
        return 0;
    }
    gint width = 0;
    for (gsize i = 0; i < n; i++) {
        width += utf8_display_width(segments[i].key);
        width += 1;
        width += utf8_display_width(segments[i].label);
        if (i + 1 < n) {
            width += 2;
        }
    }
    return width;
}

static gint ui_help_segments_visible_width_with_help(const HelpSegment *segments,
                                                     gsize prefix_n,
                                                     gssize help_index) {
    gint width = ui_help_segments_visible_width(segments, prefix_n);
    if (help_index >= 0) {
        if (prefix_n > 0) {
            width += 2;
        }
        width += utf8_display_width(segments[help_index].key);
        width += 1;
        width += utf8_display_width(segments[help_index].label);
    }
    return width;
}

void ui_print_centered_help_line(gint row, gint term_width, const HelpSegment *segments, gsize n) {
    if (term_width <= 0 || row <= 0) {
        return;
    }
    printf("\033[%d;1H\033[2K", row);

    if (!segments || n == 0) {
        return;
    }

    gsize visible_n = 0;
    for (gsize i = 1; i <= n; i++) {
        if (ui_help_segments_visible_width(segments, i) <= term_width) {
            visible_n = i;
        } else {
            break;
        }
    }

    gssize help_index = -1;
    for (gsize i = 0; i < n; i++) {
        if (g_strcmp0(segments[i].key, "?") == 0) {
            help_index = (gssize)i;
            break;
        }
    }

    gboolean append_help = FALSE;
    if (visible_n < n && help_index >= 0 && (gsize)help_index >= visible_n) {
        while (visible_n > 0 &&
               ui_help_segments_visible_width_with_help(segments, visible_n, help_index) > term_width) {
            visible_n--;
        }
        if (ui_help_segments_visible_width_with_help(segments, visible_n, help_index) <= term_width) {
            append_help = TRUE;
        }
    }

    if (visible_n == 0 && !append_help) {
        return;
    }

    gint help_w = append_help
                     ? ui_help_segments_visible_width_with_help(segments, visible_n, help_index)
                     : ui_help_segments_visible_width(segments, visible_n);
    gint pad = (help_w > 0 && term_width > help_w) ? (term_width - help_w) / 2 : 0;
    for (gint i = 0; i < pad; i++) {
        putchar(' ');
    }

    gint col = 1 + pad;
    for (gsize i = 0; i < visible_n; i++) {
        gint seg_w = utf8_display_width(segments[i].key) + 1 + utf8_display_width(segments[i].label);
        gint trailing = (i + 1 < visible_n || append_help) ? 2 : 0;
        if (col + seg_w + trailing - 1 > term_width) {
            break;
        }
        printf("\033[36m%s\033[0m %s", segments[i].key, segments[i].label);
        col += seg_w;
        if (i + 1 < visible_n || append_help) {
            printf("  ");
            col += 2;
        }
    }

    if (append_help && help_index >= 0) {
        gint seg_w = utf8_display_width(segments[help_index].key) + 1 + utf8_display_width(segments[help_index].label);
        if (col + seg_w - 1 <= term_width) {
            printf("\033[36m%s\033[0m %s", segments[help_index].key, segments[help_index].label);
        }
    }
}

void ui_begin_sync_update(void) {
    // Use terminal synchronized output to reduce flicker during full-frame draws.
    printf("\033[?2026h");
}

void ui_end_sync_update(void) {
    printf("\033[?2026l");
}

void ui_clear_screen_for_refresh(const PixelTermApp *app) {
    if (!app) {
        printf("\033[2J\033[H\033[0m");
        return;
    }

    if (!app->clear_workaround_enabled || app->term_height <= 0) {
        printf("\033[2J\033[H\033[0m");
        return;
    }

    // Some terminals can leave stale content artifacts after clears/mode switches.
    // Optional workaround: clear -> print blank lines at bottom -> clear again.
    printf("\033[2J\033[H\033[0m");
    const gint blank_lines = 10;
    printf("\033[%d;1H", app->term_height);
    for (gint i = 0; i < blank_lines; i++) {
        printf("\033[2K\n");
    }
    printf("\033[2J\033[H\033[0m");
}

void ui_clear_kitty_images(const PixelTermApp *app) {
    if (!app || !app->force_kitty) {
        return;
    }
    // Delete all kitty image placements (quiet) so old images don't linger.
    printf("\033_Ga=d,q=2\033\\");
}

void ui_clear_single_view_lines(const PixelTermApp *app) {
    if (!app || app->term_height <= 0) {
        return;
    }

    const gint top_rows[] = {1, 2, 3};
    for (gsize i = 0; i < G_N_ELEMENTS(top_rows); i++) {
        gint row = top_rows[i];
        if (row > app->term_height) {
            continue;
        }
        printf("\033[%d;1H\033[2K", row);
    }

    for (gint row = app->term_height - 2; row <= app->term_height; row++) {
        if (row < 1) {
            continue;
        }
        printf("\033[%d;1H\033[2K", row);
    }
}

void ui_clear_area(const PixelTermApp *app, gint top_row, gint height) {
    if (!app || app->term_height <= 0 || height <= 0) {
        return;
    }

    gint start_row = MAX(1, top_row);
    gint end_row = MIN(app->term_height, top_row + height - 1);
    for (gint row = start_row; row <= end_row; row++) {
        printf("\033[%d;1H\033[2K", row);
    }
}

static void ui_panel_border(gint row, gint start_col, gint inner_width) {
    printf("\033[%d;%dH\033[97;48;5;236m+", row, start_col);
    for (gint i = 0; i < inner_width; i++) {
        putchar('-');
    }
    printf("+\033[0m");
}

static void ui_panel_blank_row(gint row, gint start_col, gint inner_width) {
    printf("\033[%d;%dH\033[97;48;5;236m|", row, start_col);
    for (gint i = 0; i < inner_width; i++) {
        putchar(' ');
    }
    printf("|\033[0m");
}

static void ui_panel_text_row(gint row,
                              gint start_col,
                              gint inner_width,
                              const char *text,
                              gboolean centered,
                              const char *style) {
    gchar *truncated = truncate_utf8_for_display(text ? text : "", inner_width);
    gint text_width = utf8_display_width(truncated);
    gint offset = centered && inner_width > text_width ? (inner_width - text_width) / 2 : 1;
    if (offset < 1) {
        offset = 1;
    }
    ui_panel_blank_row(row, start_col, inner_width);
    printf("\033[%d;%dH%s%s\033[0m", row, start_col + offset, style ? style : "\033[97;48;5;236m", truncated);
    g_free(truncated);
}

static void ui_panel_pair_row(gint row,
                              gint start_col,
                              gint inner_width,
                              gint left_width,
                              const UIPanelRow *panel_row) {
    gint max_left_width = inner_width - 6;
    if (max_left_width < 1) {
        max_left_width = 1;
    }
    if (left_width > max_left_width) {
        left_width = max_left_width;
    }

    gint right_width = inner_width - left_width - 4;
    if (right_width < 1) {
        right_width = 1;
    }
    gchar *left = truncate_utf8_for_display(panel_row && panel_row->left ? panel_row->left : "", left_width);
    gchar *right = truncate_utf8_middle_keep_suffix(panel_row && panel_row->right ? panel_row->right : "", right_width);
    ui_panel_blank_row(row, start_col, inner_width);
    printf("\033[%d;%dH\033[36;48;5;236m%s\033[0m", row, start_col + 2, left);
    printf("\033[%d;%dH\033[97;48;5;236m%s\033[0m", row, start_col + left_width + 5, right);
    g_free(right);
    g_free(left);
}

void ui_render_panel(gint term_width, gint term_height, const UIPanel *panel) {
    if (!panel || term_width < 8 || term_height < 4) {
        return;
    }

    const char *title = panel->title ? panel->title : "";
    gsize line_count = panel->lines ? panel->line_count : 0;
    gsize row_count = panel->rows ? panel->row_count : 0;

    gint left_width = 0;
    gint right_width = 0;
    for (gsize i = 0; i < row_count; i++) {
        left_width = MAX(left_width, utf8_display_width(panel->rows[i].left));
        right_width = MAX(right_width, utf8_display_width(panel->rows[i].right));
    }
    if (left_width > 18) {
        left_width = 18;
    }

    gint content_width = utf8_display_width(title);
    for (gsize i = 0; i < line_count; i++) {
        content_width = MAX(content_width, utf8_display_width(panel->lines[i]));
    }
    if (row_count > 0) {
        content_width = MAX(content_width, left_width + right_width + 7);
    }

    gint inner_width = content_width;
    if (panel->min_inner_width > 0) {
        inner_width = MAX(inner_width, panel->min_inner_width);
    }
    if (panel->max_inner_width > 0) {
        inner_width = MIN(inner_width, panel->max_inner_width);
    }
    inner_width = MIN(inner_width, term_width - 4);
    inner_width = MAX(inner_width, 4);

    gint panel_height = 3 + (gint)line_count + (gint)row_count;
    if (line_count > 0 && row_count > 0) {
        panel_height++;
    }
    panel_height = MIN(panel_height, term_height);

    gint panel_width = inner_width + 2;
    gint start_col = MAX(1, ((term_width - panel_width) / 2) + 1);
    gint start_row = MAX(1, ((term_height - panel_height) / 2) + 1);

    ui_panel_border(start_row, start_col, inner_width);
    ui_panel_text_row(start_row + 1, start_col, inner_width, title, TRUE, "\033[1;96;48;5;236m");

    gint row = start_row + 2;
    for (gsize i = 0; i < line_count && row < start_row + panel_height - 1; i++, row++) {
        ui_panel_text_row(row, start_col, inner_width, panel->lines[i], FALSE, "\033[97;48;5;236m");
    }
    if (line_count > 0 && row_count > 0 && row < start_row + panel_height - 1) {
        ui_panel_blank_row(row, start_col, inner_width);
        row++;
    }
    for (gsize i = 0; i < row_count && row < start_row + panel_height - 1; i++, row++) {
        ui_panel_pair_row(row, start_col, inner_width, left_width, &panel->rows[i]);
    }
    ui_panel_border(start_row + panel_height - 1, start_col, inner_width);
    fflush(stdout);
}
