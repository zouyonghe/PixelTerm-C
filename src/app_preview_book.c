#include "app.h"
#include "text_utils.h"
#include "app_preview_shared_internal.h"
#include "grid_render.h"
#include "media_utils.h"
#include "preload_control.h"
#include "ui_render_utils.h"

static const gint k_book_spread_gutter_cols = 2;

// Calculate preview grid layout using preview_zoom as target cell width
typedef struct {
    PixelTermApp *app;
    ImageRenderer *renderer;
} BookPreviewRenderContext;

static GridRenderResult app_book_preview_render_cell(const GridRenderContext *context,
                                                     const GridRenderCell *cell,
                                                     void *userdata) {
    BookPreviewRenderContext *render_ctx = (BookPreviewRenderContext *)userdata;
    if (!render_ctx || !render_ctx->app || !render_ctx->renderer) {
        return GRID_RENDER_STOP_ALL;
    }

    PixelTermApp *app = render_ctx->app;

    app_draw_grid_cell_background(context->layout,
                                  cell->cell_x,
                                  cell->cell_y,
                                  cell->use_border,
                                  "\033[34;1m");

    BookPageImage page_image;
    ErrorCode page_err = book_render_page(app->book.doc,
                                          cell->index,
                                          context->content_width,
                                          context->content_height,
                                          &page_image);
    if (page_err != ERROR_NONE) {
        const char *label = "PAGE";
        gint label_len = (gint)strlen(label);
        gint label_row = cell->content_y + context->content_height / 2;
        gint label_col = cell->content_x + (context->content_width - label_len) / 2;
        if (label_row < cell->content_y) label_row = cell->content_y;
        if (label_col < cell->content_x) label_col = cell->content_x;
        printf("\033[%d;%dH\033[33m%s\033[0m", label_row, label_col, label);
        return GRID_RENDER_CONTINUE;
    }

    GString *rendered = renderer_render_image_data(render_ctx->renderer,
                                                   page_image.pixels,
                                                   page_image.width,
                                                   page_image.height,
                                                   page_image.stride,
                                                   page_image.channels);
    book_page_image_free(&page_image);

    if (!rendered) {
        const char *label = "PAGE";
        gint label_len = (gint)strlen(label);
        gint label_row = cell->content_y + context->content_height / 2;
        gint label_col = cell->content_x + (context->content_width - label_len) / 2;
        if (label_row < cell->content_y) label_row = cell->content_y;
        if (label_col < cell->content_x) label_col = cell->content_x;
        printf("\033[%d;%dH\033[33m%s\033[0m", label_row, label_col, label);
        return GRID_RENDER_CONTINUE;
    }

    app_draw_rendered_lines(cell->content_x,
                            cell->content_y,
                            context->content_width,
                            context->content_height,
                            rendered);
    g_string_free(rendered, TRUE);

    return GRID_RENDER_CONTINUE;
}
static PreviewLayout app_book_preview_calculate_layout(PixelTermApp *app) {
    PreviewLayout layout = {1, 1, app ? app->term_width : 80, 10, 3, 1};
    if (!app || app->book.page_count <= 0) {
        return layout;
    }

    const gint header_lines = app->ui_text_hidden ? 0 : 3;
    gint usable_width = app->term_width > 0 ? app->term_width : 80;
    gint bottom_reserved = app_preview_bottom_reserved_lines(app);
    gint usable_height = app->term_height > header_lines + bottom_reserved
                             ? app->term_height - header_lines - bottom_reserved
                             : 6;

    if (app->book.preview_zoom <= 0) {
        app->book.preview_zoom = 30;
    }

    gint cols = usable_width / app->book.preview_zoom;
    if (cols < 2) cols = 2;
    if (usable_width / cols < 4) {
        cols = usable_width / 4;
        if (cols < 2) cols = 2;
    }

    gint cell_width = usable_width / cols;
    gint cell_height = cell_width / 2 + 1;
    if (cell_height < 4) cell_height = 4;

    gint rows = (app->book.page_count + cols - 1) / cols;
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

static void app_book_preview_adjust_scroll(PixelTermApp *app, const PreviewLayout *layout) {
    if (!app || !layout) {
        return;
    }

    gint total_rows = layout->rows;
    gint visible_rows = layout->visible_rows;
    if (visible_rows < 1) visible_rows = 1;

    gint max_offset = MAX(0, total_rows - 1);
    if (app->book.preview_scroll > max_offset) {
        app->book.preview_scroll = max_offset;
    }
    if (app->book.preview_scroll < 0) {
        app->book.preview_scroll = 0;
    }

    gint row = app->book.preview_selected / layout->cols;
    if (row < app->book.preview_scroll) {
        app->book.preview_scroll = row;
    } else if (row >= app->book.preview_scroll + visible_rows) {
        app->book.preview_scroll = row - visible_rows + 1;
    }
}

static void app_book_preview_render_page_indicator(PixelTermApp *app) {
    if (!app || app->ui_text_hidden || app->term_height < 3) {
        return;
    }
    if (app->book.page_count <= 0) {
        return;
    }

    gint total_pages = app->book.page_count > 0 ? app->book.page_count : 1;
    gint page_display = app->book.preview_selected + 1;
    if (page_display < 1) page_display = 1;
    if (page_display > total_pages) page_display = total_pages;

    char page_text[32];
    g_snprintf(page_text, sizeof(page_text), "%d/%d", page_display, total_pages);
    gint page_len = (gint)strlen(page_text);
    gint page_pad = (app->term_width > page_len) ? (app->term_width - page_len) / 2 : 0;
    printf("\033[3;1H\033[2K");
    for (gint i = 0; i < page_pad; i++) putchar(' ');
    printf("%s", page_text);
}

static void app_book_preview_render_selected_info(PixelTermApp *app) {
    if (!app || app->ui_text_hidden || app->term_height < 3) {
        return;
    }
    if (!app->book.path || app->book.page_count <= 0) {
        return;
    }

    gchar *base = g_path_get_basename(app->book.path);
    if (base) {
        char *dot = strrchr(base, '.');
        if (dot && dot != base) {
            *dot = '\0';
        }
    }
    gchar *safe = sanitize_for_terminal(base);
    gint max_width = ui_filename_max_width(app);
    if (max_width <= 0) {
        max_width = app->term_width;
    }

    gchar *display_name = truncate_utf8_middle_keep_suffix(safe, max_width);
    gint row = app->term_height - 2;
    gint name_len = utf8_display_width(display_name);
    gint pad = (app->term_width > name_len) ? (app->term_width - name_len) / 2 : 0;
    printf("\033[%d;1H", row);
    for (gint i = 0; i < app->term_width; i++) putchar(' ');
    if (name_len > 0) {
        printf("\033[%d;%dH\033[34m%s\033[0m", row, pad + 1, display_name);
    }

    g_free(display_name);
    g_free(safe);
    g_free(base);
}

static void app_book_render_page_indicator(const PixelTermApp *app) {
    if (!app || app->ui_text_hidden || app->term_height <= 0) {
        return;
    }

    gint current = app->book.page + 1;
    gint total = app->book.page_count;
    if (current < 1) current = 1;
    if (total < 1) total = 1;

    gint idx_row = (app->term_height >= 2) ? (app->term_height - 2) : 1;
    printf("\033[%d;1H\033[2K", idx_row);

    gboolean double_page = app_book_use_double_page(app);
    if (!double_page) {
        char idx_text[32];
        g_snprintf(idx_text, sizeof(idx_text), "%d/%d", current, total);
        gint idx_len = (gint)strlen(idx_text);
        gint idx_pad = (app->term_width > idx_len) ? (app->term_width - idx_len) / 2 : 0;
        for (gint i = 0; i < idx_pad; i++) putchar(' ');
        printf("%s", idx_text);
        return;
    }

    gint target_width = 0;
    gint target_height = 0;
    app_get_image_target_dimensions(app, &target_width, &target_height);
    (void)target_height;
    gint gutter_cols = k_book_spread_gutter_cols;
    gint per_page_cols = (target_width - gutter_cols) / 2;
    if (per_page_cols < 1) {
        char idx_text[32];
        g_snprintf(idx_text, sizeof(idx_text), "%d/%d", current, total);
        gint idx_len = (gint)strlen(idx_text);
        gint idx_pad = (app->term_width > idx_len) ? (app->term_width - idx_len) / 2 : 0;
        for (gint i = 0; i < idx_pad; i++) putchar(' ');
        printf("%s", idx_text);
        return;
    }

    gint spread_cols = per_page_cols * 2 + gutter_cols;
    gint spread_left_col = 1;
    if (spread_cols > 0 && app->term_width > spread_cols) {
        spread_left_col = (app->term_width - spread_cols) / 2 + 1;
    }
    gint left_half_start = spread_left_col;
    gint right_half_start = spread_left_col + per_page_cols + gutter_cols;

    char left_text[32];
    g_snprintf(left_text, sizeof(left_text), "%d/%d", current, total);
    gint left_len = (gint)strlen(left_text);
    gint left_col = left_half_start;
    if (left_len > 0 && left_len < per_page_cols) {
        left_col += (per_page_cols - left_len) / 2;
    }
    printf("\033[%d;%dH%s", idx_row, left_col, left_text);

    gint right_page = current + 1;
    if (right_page <= total) {
        char right_text[32];
        g_snprintf(right_text, sizeof(right_text), "%d/%d", right_page, total);
        gint right_len = (gint)strlen(right_text);
        gint right_col = right_half_start;
        if (right_len > 0 && right_len < per_page_cols) {
            right_col += (per_page_cols - right_len) / 2;
        }
        printf("\033[%d;%dH%s", idx_row, right_col, right_text);
    }
}

void app_book_jump_render_prompt(PixelTermApp *app) {
    if (!app || !app->book.jump_active) {
        return;
    }
    if (!app_is_book_mode(app) && !app_is_book_preview_mode(app)) {
        return;
    }

    gint total_pages = app->book.page_count > 0 ? app->book.page_count : 1;
    gint total_len = 1;
    for (gint tmp = total_pages; tmp >= 10; tmp /= 10) {
        total_len++;
    }
    const char *buf = app->book.jump_buf;
    gint buf_len = app->book.jump_len;
    gint field_width = MIN(total_len, (gint)sizeof(app->book.jump_buf) - 1);
    if (field_width < 1) field_width = 1;

    const char *label = "Jump:";
    const gint label_gap = 1;
    gint label_len = (gint)strlen(label);
    gint layout_width = label_len + label_gap + field_width;

    gint term_h = app->term_height > 0 ? app->term_height : 24;
    gint term_w = app->term_width > 0 ? app->term_width : 80;
    gint input_row = app_is_book_preview_mode(app) ? term_h - 3 : term_h - 2;
    if (input_row < 1) input_row = 1;

    printf("\033[%d;1H\033[2K", input_row);
    gint input_pad = (term_w > layout_width) ? (term_w - layout_width) / 2 : 0;
    if (input_pad < 0) input_pad = 0;
    for (gint i = 0; i < input_pad; i++) putchar(' ');
    gint base_col = input_pad + 1;
    printf("\033[%d;%dH\033[36m%s\033[0m", input_row, base_col, label);

    gint field_col = base_col + label_len + label_gap;
    char field_buf[32];
    gint field_fill = MIN(field_width, (gint)sizeof(field_buf) - 1);
    for (gint i = 0; i < field_fill; i++) {
        field_buf[i] = ' ';
    }
    if (buf_len > 0) {
        gint print_len = MIN(buf_len, field_fill);
        memcpy(field_buf, buf, print_len);
    } else {
        field_buf[0] = '_';
    }
    field_buf[field_fill] = '\0';
    printf("\033[%d;%dH\033[33m%s\033[0m", input_row, field_col, field_buf);

    gint cursor_col = field_col + (buf_len > 0 ? MIN(buf_len, field_fill) : 0);
    if (cursor_col < 1) cursor_col = 1;
    if (cursor_col > term_w) cursor_col = term_w;
    printf("\033[%d;%dH\033[?25h", input_row, cursor_col);
    fflush(stdout);
}

void app_book_jump_clear_prompt(PixelTermApp *app) {
    if (!app) {
        return;
    }
    if (!app_is_book_mode(app) && !app_is_book_preview_mode(app)) {
        return;
    }

    gint term_h = app->term_height > 0 ? app->term_height : 24;
    gint input_row = app_is_book_preview_mode(app) ? term_h - 3 : term_h - 2;
    if (input_row < 1) input_row = 1;
    printf("\033[%d;1H\033[2K", input_row);
    printf("\033[?25l");

    if (app_is_book_preview_mode(app)) {
        app_book_preview_render_selected_info(app);
        app_book_preview_render_page_indicator(app);
    } else if (app_is_book_mode(app) && !app->ui_text_hidden) {
        app_book_render_page_indicator(app);
    }

    fflush(stdout);
}

static void app_book_preview_clear_cell_border(const PixelTermApp *app,
                                               const PreviewLayout *layout,
                                               gint index,
                                               gint start_row,
                                               gint vertical_offset) {
    if (!app || !layout) {
        return;
    }
    if (layout->cell_width < 4 || layout->cell_height < 4) {
        return;
    }
    gint cell_x = 0;
    gint cell_y = 0;
    if (!app_grid_get_cell_origin(layout,
                                  index,
                                  app->book.page_count,
                                  start_row,
                                  vertical_offset,
                                  &cell_x,
                                  &cell_y)) {
        return;
    }
    app_grid_clear_cell_border(layout, cell_x, cell_y);
}

static void app_book_preview_draw_cell_border(const PixelTermApp *app,
                                              const PreviewLayout *layout,
                                              gint index,
                                              gint start_row,
                                              gint vertical_offset) {
    if (!app || !layout) {
        return;
    }
    if (layout->cell_width < 4 || layout->cell_height < 4) {
        return;
    }
    gint cell_x = 0;
    gint cell_y = 0;
    if (!app_grid_get_cell_origin(layout,
                                  index,
                                  app->book.page_count,
                                  start_row,
                                  vertical_offset,
                                  &cell_x,
                                  &cell_y)) {
        return;
    }
    app_grid_draw_cell_border(layout, cell_x, cell_y, "\033[34;1m");
}

ErrorCode app_book_preview_move_selection(PixelTermApp *app, gint delta_row, gint delta_col) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_book_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (app->book.page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    PreviewLayout layout = app_book_preview_calculate_layout(app);
    gint cols = layout.cols;
    gint rows = layout.rows;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    gint old_scroll = app->book.preview_scroll;

    gint row = app->book.preview_selected / cols;
    gint col = app->book.preview_selected % cols;

    row += delta_row;
    col += delta_col;

    if (delta_col < 0 && col < 0) {
        col = cols - 1;
    } else if (delta_col > 0 && col >= cols) {
        col = 0;
    }

    if (delta_row > 0 && row >= rows) {
        row = 0;
        app->book.preview_scroll = 0;
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
        app->book.preview_scroll = last_page_scroll;
    } else if (delta_row > 0 && row >= app->book.preview_scroll + layout.visible_rows) {
        gint new_scroll = MIN(app->book.preview_scroll + layout.visible_rows, MAX(rows - 1, 0));
        app->book.preview_scroll = new_scroll;
        row = new_scroll;
    } else if (delta_row < 0 && row < app->book.preview_scroll) {
        gint new_scroll = MAX(app->book.preview_scroll - layout.visible_rows, 0);
        app->book.preview_scroll = new_scroll;
        row = MIN(new_scroll + layout.visible_rows - 1, rows - 1);
    }

    if (row < 0) row = 0;
    if (row >= rows) row = rows - 1;
    if (col < 0) col = 0;
    if (col >= cols) col = cols - 1;

    gint new_index = row * cols + col;
    gint row_start = row * cols;
    gint row_end = MIN(app->book.page_count - 1, row_start + cols - 1);
    if (new_index < row_start) new_index = row_start;
    if (new_index > row_end) new_index = row_end;
    if (new_index >= app->book.page_count) {
        new_index = app->book.page_count - 1;
    }

    app->book.preview_selected = new_index;
    app_book_preview_adjust_scroll(app, &layout);
    if (app->book.preview_scroll != old_scroll) {
        app->needs_screen_clear = TRUE;
    }
    return ERROR_NONE;
}

ErrorCode app_book_preview_page_move(PixelTermApp *app, gint direction) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_book_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (app->book.page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    PreviewLayout layout = app_book_preview_calculate_layout(app);
    gint rows_per_page = layout.visible_rows > 0 ? layout.visible_rows : 1;
    gint total_pages = (layout.rows + rows_per_page - 1) / rows_per_page;
    if (total_pages <= 1) {
        return ERROR_NONE;
    }
    gint cols = layout.cols;
    if (cols < 1) cols = 1;
    gint rows = layout.rows;
    gint old_scroll = app->book.preview_scroll;

    gint current_row = cols > 0 ? app->book.preview_selected / cols : 0;
    gint current_col = cols > 0 ? app->book.preview_selected % cols : 0;
    gint relative_row = current_row - app->book.preview_scroll;
    if (relative_row < 0) relative_row = 0;
    if (relative_row >= rows_per_page) relative_row = rows_per_page - 1;

    gint delta_scroll = direction >= 0 ? rows_per_page : -rows_per_page;
    gint new_scroll = app->book.preview_scroll + delta_scroll;
    gint last_page_scroll = ((rows - 1) / rows_per_page) * rows_per_page;
    if (last_page_scroll < 0) last_page_scroll = 0;
    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > last_page_scroll) new_scroll = last_page_scroll;

    gint new_row = new_scroll + relative_row;
    if (new_row < 0) new_row = 0;
    if (new_row >= rows) new_row = rows - 1;

    if (current_col < 0) current_col = 0;
    if (current_col >= cols) current_col = cols - 1;

    gint new_index = new_row * cols + current_col;
    gint row_start = new_row * cols;
    gint row_end = MIN(app->book.page_count - 1, row_start + cols - 1);
    if (new_index < row_start) new_index = row_start;
    if (new_index > row_end) new_index = row_end;
    if (new_index >= app->book.page_count) {
        new_index = app->book.page_count - 1;
    }

    app->book.preview_scroll = new_scroll;
    app->book.preview_selected = new_index;

    if (app->book.preview_scroll != old_scroll) {
        app->needs_screen_clear = TRUE;
    }
    return ERROR_NONE;
}

ErrorCode app_book_preview_jump_to_page(PixelTermApp *app, gint page_index) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_book_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (app->book.page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    if (page_index < 0) page_index = 0;
    if (page_index >= app->book.page_count) page_index = app->book.page_count - 1;

    PreviewLayout layout = app_book_preview_calculate_layout(app);
    gint cols = layout.cols > 0 ? layout.cols : 1;
    gint rows = layout.rows > 0 ? layout.rows : 1;
    gint rows_per_page = layout.visible_rows > 0 ? layout.visible_rows : 1;

    gint row = page_index / cols;
    gint new_scroll = (rows_per_page > 0) ? (row / rows_per_page) * rows_per_page : row;
    gint last_page_scroll = 0;
    if (rows > 0 && rows_per_page > 0) {
        last_page_scroll = ((rows - 1) / rows_per_page) * rows_per_page;
    }
    if (new_scroll > last_page_scroll) new_scroll = last_page_scroll;
    if (new_scroll < 0) new_scroll = 0;

    gint old_scroll = app->book.preview_scroll;
    app->book.preview_selected = page_index;
    app->book.preview_scroll = new_scroll;
    if (app->book.preview_scroll != old_scroll) {
        app->needs_screen_clear = TRUE;
    }
    return ERROR_NONE;
}

ErrorCode app_book_preview_scroll_pages(PixelTermApp *app, gint direction) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_book_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (app->book.page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    PreviewLayout layout = app_book_preview_calculate_layout(app);
    gint visible_rows = layout.visible_rows;
    if (visible_rows < 1) visible_rows = 1;
    gint total_rows = layout.rows;
    if (total_rows <= visible_rows) {
        return ERROR_NONE;
    }

    gint delta = direction > 0 ? visible_rows : -visible_rows;
    gint new_scroll = app->book.preview_scroll + delta;
    if (new_scroll < 0) new_scroll = 0;
    gint max_scroll = MAX(0, total_rows - visible_rows);
    if (new_scroll > max_scroll) new_scroll = max_scroll;

    if (new_scroll == app->book.preview_scroll) {
        return ERROR_NONE;
    }

    app->book.preview_scroll = new_scroll;
    return ERROR_NONE;
}

ErrorCode app_book_preview_change_zoom(PixelTermApp *app, gint delta) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_book_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (app->term_width <= 0) {
        return ERROR_NONE;
    }

    gint usable_width = app->term_width;
    if (app->book.preview_zoom <= 0) {
        app->book.preview_zoom = usable_width / 4;
    }

    gint current_cols = (gint)(usable_width / app->book.preview_zoom + 0.5f);
    if (current_cols < 2) current_cols = 2;
    gint new_cols = current_cols - delta;
    if (new_cols < 2) new_cols = 2;
    if (new_cols > usable_width) new_cols = usable_width;

    app->book.preview_zoom = (gdouble)usable_width / new_cols;
    if (app->book.preview_zoom < 1) app->book.preview_zoom = 1;

    app->needs_screen_clear = TRUE;
    return ERROR_NONE;
}

// Handle mouse click in preview grid mode
ErrorCode app_handle_mouse_click_book_preview(PixelTermApp *app,
                                              gint mouse_x,
                                              gint mouse_y,
                                              gboolean *redraw_needed,
                                              gboolean *out_hit) {
    if (!app) {
        if (redraw_needed) *redraw_needed = FALSE;
        if (out_hit) *out_hit = FALSE;
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_book_preview_mode(app)) {
        if (redraw_needed) *redraw_needed = FALSE;
        if (out_hit) *out_hit = FALSE;
        return ERROR_INVALID_ARGS;
    }
    if (redraw_needed) *redraw_needed = FALSE;
    if (out_hit) *out_hit = FALSE;

    PreviewLayout layout = app_book_preview_calculate_layout(app);
    gint start_row = app->book.preview_scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint vertical_offset = app_preview_compute_vertical_offset(app, &layout, start_row, end_row);
    gint grid_top_y = layout.header_lines + 1 + vertical_offset;

    if (mouse_y < grid_top_y) {
        return ERROR_NONE;
    }

    gint col = (mouse_x - 1) / layout.cell_width;
    gint row_in_visible = (mouse_y - grid_top_y) / layout.cell_height;
    gint absolute_row = start_row + row_in_visible;

    gint rows_drawn = MAX(0, end_row - start_row);
    if (col < 0 || col >= layout.cols || row_in_visible < 0 || row_in_visible >= rows_drawn) {
        return ERROR_NONE;
    }

    gint index = absolute_row * layout.cols + col;
    if (index >= 0 && index < app->book.page_count) {
        if (out_hit) *out_hit = TRUE;
        if (app->book.preview_selected != index) {
            app->book.preview_selected = index;
            if (redraw_needed) *redraw_needed = TRUE;
        }
    }

    return ERROR_NONE;
}

// Enter preview grid mode
ErrorCode app_enter_book_preview(PixelTermApp *app) {
    if (!app || !app->book.doc) {
        return ERROR_INVALID_IMAGE;
    }

    if (app->gif_player) {
        gif_player_stop(app->gif_player);
    }
    if (app->video_player) {
        video_player_stop(app->video_player);
    }

    (void)app_transition_mode(app, APP_MODE_BOOK_PREVIEW);
    app->book.preview_selected = app->book.page >= 0 ? app->book.page : 0;
    if (app->book.preview_selected >= app->book.page_count) {
        app->book.preview_selected = MAX(0, app->book.page_count - 1);
    }
    app->book.preview_scroll = 0;
    app->info_visible = FALSE;
    app->needs_screen_clear = TRUE;

    app_preloader_clear_queue(app);
    return ERROR_NONE;
}

ErrorCode app_enter_book_page(PixelTermApp *app, gint page_index) {
    if (!app || !app->book.doc) {
        return ERROR_INVALID_IMAGE;
    }

    if (page_index < 0) page_index = 0;
    if (page_index >= app->book.page_count) {
        page_index = MAX(0, app->book.page_count - 1);
    }

    if (app->gif_player) {
        gif_player_stop(app->gif_player);
    }
    if (app->video_player) {
        video_player_stop(app->video_player);
    }

    app->book.page = page_index;
    (void)app_transition_mode(app, APP_MODE_BOOK);
    app->info_visible = FALSE;
    app->needs_redraw = TRUE;

    app_preloader_clear_queue(app);
    return ERROR_NONE;
}

// Exit preview grid mode
ErrorCode app_render_book_preview(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_book_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (!app->book.doc) {
        return ERROR_INVALID_IMAGE;
    }
    if (app->book.page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    gint prev_width = app->term_width;
    gint prev_height = app->term_height;
    get_terminal_size(&app->term_width, &app->term_height);
    if ((prev_width > 0 && prev_width != app->term_width) ||
        (prev_height > 0 && prev_height != app->term_height)) {
        app->needs_screen_clear = TRUE;
    }

    PreviewLayout layout = app_book_preview_calculate_layout(app);
    app_book_preview_adjust_scroll(app, &layout);

    if (app->suppress_full_clear) {
        app->suppress_full_clear = FALSE;
        printf("\033[H\033[0m");
        if (app->ui_text_hidden) {
            ui_clear_single_view_lines(app);
        }
        app->needs_screen_clear = FALSE;
    } else if (app->needs_screen_clear) {
        printf("\033[2J\033[H\033[0m");
        app->needs_screen_clear = FALSE;
    } else {
        printf("\033[H\033[0m");
    }

    if (!app->ui_text_hidden) {
        const char *title = "Book Preview";
        gint title_len = strlen(title);
        gint pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
        printf("\033[1;1H\033[2K");
        for (gint i = 0; i < pad; i++) putchar(' ');
        printf("%s", title);

        printf("\033[2;1H\033[2K");
        app_book_preview_render_page_indicator(app);
    }

    gint content_width = layout.cell_width - 2;
    gint content_height = layout.cell_height - 2;
    if (content_width < 1) content_width = 1;
    if (content_height < 1) content_height = 1;
    ErrorCode renderer_error = ERROR_NONE;
    ImageRenderer *renderer = app_create_grid_renderer(app, content_width, content_height, &renderer_error);
    if (!renderer) {
        return renderer_error != ERROR_NONE ? renderer_error : ERROR_MEMORY_ALLOC;
    }

    gint start_row = app->book.preview_scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint vertical_offset = app_preview_compute_vertical_offset(app, &layout, start_row, end_row);
    GridRenderContext grid_context = {
        .layout = &layout,
        .start_row = start_row,
        .end_row = end_row,
        .vertical_offset = vertical_offset,
        .content_width = content_width,
        .content_height = content_height,
        .total_items = app->book.page_count,
        .selected_index = app->book.preview_selected
    };
    BookPreviewRenderContext render_ctx = {
        .app = app,
        .renderer = renderer
    };
    grid_render_cells(&grid_context, app_book_preview_render_cell, &render_ctx);

    app_book_preview_render_selected_info(app);
    if (app->book.jump_active) {
        app_book_jump_render_prompt(app);
    }

    if (app->term_height > 0 && !app->ui_text_hidden) {
        const HelpSegment segments[] = {
            {"←/→/↑/↓", "Move"},
            {"PgUp/PgDn", "Page"},
            {"P", "Page"},
            {"T", "TOC"},
            {"Enter", "Open"},
            {"TAB", "Toggle"},
            {"+/-", "Zoom"},
            {"~", "Zen"},
            {"ESC", "Exit"}
        };
        ui_print_centered_help_line(app->term_height, app->term_width, segments, G_N_ELEMENTS(segments));
    }

    fflush(stdout);
    renderer_destroy(renderer);
    return ERROR_NONE;
}

ErrorCode app_render_book_preview_selection_change(PixelTermApp *app, gint old_index) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_book_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (app->book.page_count <= 0) {
        return ERROR_INVALID_IMAGE;
    }

    gint old_scroll = app->book.preview_scroll;
    PreviewLayout layout = app_book_preview_calculate_layout(app);
    app_book_preview_adjust_scroll(app, &layout);
    if (app->book.preview_scroll != old_scroll) {
        return app_render_book_preview(app);
    }

    gint selected_row = app->book.preview_selected / layout.cols;
    if (selected_row < app->book.preview_scroll ||
        selected_row >= app->book.preview_scroll + layout.visible_rows) {
        return app_render_book_preview(app);
    }

    gint start_row = app->book.preview_scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint vertical_offset = app_preview_compute_vertical_offset(app, &layout, start_row, end_row);

    if (old_index != app->book.preview_selected) {
        app_book_preview_clear_cell_border(app, &layout, old_index, start_row, vertical_offset);
    }
    app_book_preview_draw_cell_border(app, &layout, app->book.preview_selected, start_row, vertical_offset);
    app_book_preview_render_page_indicator(app);
    app_book_preview_render_selected_info(app);
    if (app->book.jump_active) {
        app_book_jump_render_prompt(app);
    }

    fflush(stdout);
    return ERROR_NONE;
}
