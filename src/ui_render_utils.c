#include "ui_render_utils.h"

#include "text_utils.h"

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

void ui_print_centered_help_line(gint row, gint term_width, const HelpSegment *segments, gsize n) {
    if (term_width <= 0 || row <= 0) {
        return;
    }
    printf("\033[%d;1H\033[2K", row);

    gint help_w = ui_help_segments_visible_width(segments, n);
    gint pad = (help_w > 0 && term_width > help_w) ? (term_width - help_w) / 2 : 0;
    for (gint i = 0; i < pad; i++) {
        putchar(' ');
    }

    gint col = 1 + pad;
    for (gsize i = 0; i < n; i++) {
        gint seg_w = utf8_display_width(segments[i].key) + 1 + utf8_display_width(segments[i].label);
        gint trailing = (i + 1 < n) ? 2 : 0;
        if (col + seg_w + trailing - 1 > term_width) {
            break;
        }
        printf("\033[36m%s\033[0m %s", segments[i].key, segments[i].label);
        col += seg_w;
        if (i + 1 < n) {
            printf("  ");
            col += 2;
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
