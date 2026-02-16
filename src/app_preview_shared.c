#include "app_preview_shared_internal.h"

gint app_preview_bottom_reserved_lines(const PixelTermApp *app) {
    if (app && app->ui_text_hidden) {
        return 0;
    }
    // Row -2: filename, Row -1: spacer, Row -0: footer hints.
    return 3;
}

gint app_preview_compute_vertical_offset(const PixelTermApp *app,
                                         const PreviewLayout *layout,
                                         gint start_row,
                                         gint end_row) {
    if (!app || !layout) {
        return 0;
    }
    gint bottom_reserved = app_preview_bottom_reserved_lines(app);
    gint available = app->term_height - layout->header_lines - bottom_reserved;
    if (available < 0) {
        available = 0;
    }
    gint rows_drawn = MAX(0, end_row - start_row);
    gint grid_h = rows_drawn * layout->cell_height;
    if (grid_h >= available) {
        return 0;
    }
    return (available - grid_h) / 2;
}

gboolean app_grid_get_cell_origin(const PreviewLayout *layout,
                                  gint index,
                                  gint total_items,
                                  gint start_row,
                                  gint vertical_offset,
                                  gint *cell_x,
                                  gint *cell_y) {
    if (!layout || !cell_x || !cell_y) {
        return FALSE;
    }
    if (index < 0 || index >= total_items) {
        return FALSE;
    }
    gint row = index / layout->cols;
    gint col = index % layout->cols;
    if (row < start_row || row >= start_row + layout->visible_rows) {
        return FALSE;
    }
    *cell_x = col * layout->cell_width + 1;
    *cell_y = layout->header_lines + vertical_offset + (row - start_row) * layout->cell_height + 1;
    return TRUE;
}

void app_grid_clear_cell_border(const PreviewLayout *layout, gint cell_x, gint cell_y) {
    if (!layout) {
        return;
    }
    if (layout->cell_width < 4 || layout->cell_height < 4) {
        return;
    }

    printf("\033[0m");
    printf("\033[%d;%dH", cell_y, cell_x);
    for (gint c = 0; c < layout->cell_width; c++) putchar(' ');

    gint bottom_y = cell_y + layout->cell_height - 1;
    printf("\033[%d;%dH", bottom_y, cell_x);
    for (gint c = 0; c < layout->cell_width; c++) putchar(' ');

    for (gint line = 1; line < layout->cell_height - 1; line++) {
        gint y = cell_y + line;
        printf("\033[%d;%dH ", y, cell_x);
        printf("\033[%d;%dH ", y, cell_x + layout->cell_width - 1);
    }
}

void app_grid_draw_cell_border(const PreviewLayout *layout,
                               gint cell_x,
                               gint cell_y,
                               const char *border_style) {
    if (!layout || !border_style) {
        return;
    }
    if (layout->cell_width < 4 || layout->cell_height < 4) {
        return;
    }

    printf("\033[%d;%dH%s+", cell_y, cell_x, border_style);
    for (gint c = 0; c < layout->cell_width - 2; c++) putchar('-');
    printf("+\033[0m");

    for (gint line = 1; line < layout->cell_height - 1; line++) {
        gint y = cell_y + line;
        printf("\033[%d;%dH%s|\033[0m", y, cell_x, border_style);
        printf("\033[%d;%dH%s|\033[0m", y, cell_x + layout->cell_width - 1, border_style);
    }

    gint bottom_y = cell_y + layout->cell_height - 1;
    printf("\033[%d;%dH%s+", bottom_y, cell_x, border_style);
    for (gint c = 0; c < layout->cell_width - 2; c++) putchar('-');
    printf("+\033[0m");
}

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

ImageRenderer* app_create_grid_renderer(const PixelTermApp *app,
                                        gint content_width,
                                        gint content_height,
                                        ErrorCode *out_error) {
    if (out_error) {
        *out_error = ERROR_NONE;
    }
    if (!app) {
        if (out_error) {
            *out_error = ERROR_MEMORY_ALLOC;
        }
        return NULL;
    }

    RendererConfig config = {
        .max_width = MAX(2, content_width),
        .max_height = MAX(2, content_height),
        .preserve_aspect_ratio = TRUE,
        .dither = app->dither_enabled,
        .color_space = CHAFA_COLOR_SPACE_RGB,
        .work_factor = app->render_work_factor,
        .force_text = app->force_text,
        .force_sixel = app->force_sixel,
        .force_kitty = app->force_kitty,
        .force_iterm2 = app->force_iterm2,
        .gamma = app->gamma,
        .dither_mode = app->dither_enabled ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
        .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
        .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
    };

    ImageRenderer *renderer = renderer_create();
    if (!renderer) {
        if (out_error) {
            *out_error = ERROR_MEMORY_ALLOC;
        }
        return NULL;
    }
    if (renderer_initialize(renderer, &config) != ERROR_NONE) {
        renderer_destroy(renderer);
        if (out_error) {
            *out_error = ERROR_CHAFA_INIT;
        }
        return NULL;
    }

    return renderer;
}

void app_draw_grid_cell_background(const PreviewLayout *layout,
                                   gint cell_x,
                                   gint cell_y,
                                   gboolean use_border,
                                   const char *border_style) {
    if (!layout) {
        return;
    }
    for (gint line = 0; line < layout->cell_height; line++) {
        gint y = cell_y + line;
        printf("\033[%d;%dH", y, cell_x);
        for (gint c = 0; c < layout->cell_width; c++) {
            putchar(' ');
        }

        if (use_border && border_style) {
            if (line == 0 || line == layout->cell_height - 1) {
                printf("\033[%d;%dH%s+", y, cell_x, border_style);
                for (gint c = 0; c < layout->cell_width - 2; c++) putchar('-');
                printf("+\033[0m");
            } else {
                printf("\033[%d;%dH%s|\033[0m", y, cell_x, border_style);
                printf("\033[%d;%dH%s|\033[0m", y, cell_x + layout->cell_width - 1, border_style);
            }
        }
    }
}

void app_draw_rendered_lines(gint content_x,
                             gint content_y,
                             gint content_width,
                             gint content_height,
                             const GString *rendered) {
    if (!rendered) {
        return;
    }
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
    printf("\033[0m");
}
