#include "app.h"
#include "text_utils.h"
#include "app_preview_shared_internal.h"
#include "grid_render.h"
#include "media_utils.h"
#include "preload_control.h"
#include "ui_render_utils.h"

static PreviewLayout app_preview_calculate_layout(PixelTermApp *app) {
    PreviewLayout layout = {1, 1, app ? app->term_width : 80, 10, 3, 1};
    if (!app || app->total_images <= 0) {
        return layout;
    }

    const gint header_lines = app->ui_text_hidden ? 0 : 3;
    gint usable_width = app->term_width > 0 ? app->term_width : 80;
    gint bottom_reserved = app_preview_bottom_reserved_lines(app);
    gint usable_height = app->term_height > header_lines + bottom_reserved
                             ? app->term_height - header_lines - bottom_reserved
                             : 6;

    // If preview_zoom (target cell width) is uninitialized, default to ~30 chars/col
    if (app->preview.zoom <= 0) {
        app->preview.zoom = 30;
    }

    // Calculate columns based on target width
    gint cols = usable_width / app->preview.zoom;

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
    // Keep the viewport full whenever possible to avoid layout jumps near the end.
    gint max_offset = MAX(0, total_rows - visible_rows);
    if (app->preview.scroll > max_offset) {
        app->preview.scroll = max_offset;
    }
    if (app->preview.scroll < 0) {
        app->preview.scroll = 0;
    }

    // Ensure selection is visible
    gint row = app->preview.selected / layout->cols;
    if (row < app->preview.scroll) {
        app->preview.scroll = row;
    } else if (row >= app->preview.scroll + visible_rows) {
        app->preview.scroll = row - visible_rows + 1;
    }
}

static GList* app_preview_find_link_with_hint(const PixelTermApp *app,
                                              gint target_index,
                                              GList *hint_link,
                                              gint hint_index) {
    if (!app || !app->image_files || target_index < 0) {
        return NULL;
    }
    if (app->total_images > 0 && target_index >= app->total_images) {
        return NULL;
    }

    GList *cursor = hint_link;
    gint idx = hint_index;
    if (!cursor || idx < 0) {
        cursor = app->image_files;
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

    cursor = app->image_files;
    idx = 0;
    while (cursor && idx < target_index) {
        cursor = cursor->next;
        idx++;
    }
    return (cursor && idx == target_index) ? cursor : NULL;
}

static void app_preview_set_selected_index(PixelTermApp *app, gint index) {
    if (!app) {
        return;
    }

    app->preview.selected = index;

    if (!app->image_files || index < 0 || (app->total_images > 0 && index >= app->total_images)) {
        app->preview.selected_link = NULL;
        app->preview.selected_link_index = -1;
        return;
    }

    GList *cursor = app_preview_find_link_with_hint(app,
                                                    index,
                                                    app->preview.selected_link,
                                                    app->preview.selected_link_index);
    if (!cursor) {
        app->preview.selected_link = NULL;
        app->preview.selected_link_index = -1;
        return;
    }

    app->preview.selected_link = cursor;
    app->preview.selected_link_index = index;
}

// Queue preload tasks for currently visible (and adjacent) preview cells
static void app_preview_queue_preloads(PixelTermApp *app, const PreviewLayout *layout) {
    if (!app || !layout || !app->preloader || !app->preload_enabled) {
        return;
    }

    gint content_width = MAX(2, layout->cell_width - 2);
    gint content_height = MAX(2, layout->cell_height - 2);

    // Preload current screen plus one row of lookahead/behind to smooth paging
    gint start_row = MAX(0, app->preview.scroll - 1);
    gint end_row = MIN(layout->rows, app->preview.scroll + layout->visible_rows + 1);
    gint start_index = start_row * layout->cols;
    GList *cursor = app_preview_find_link_with_hint(app,
                                                    start_index,
                                                    app->preview.selected_link,
                                                    app->preview.selected_link_index);

    for (gint row = start_row; row < end_row && cursor; row++) {
        for (gint col = 0; col < layout->cols && cursor; col++) {
            gint idx = row * layout->cols + col;
            if (idx >= app->total_images) {
                cursor = NULL;
                break;
            }
            const gchar *filepath = (const gchar*)cursor->data;
            gint distance = ABS(idx - app->preview.selected);
            gint priority = (distance == 0) ? 0 : (distance <= layout->cols ? 1 : 5 + distance);
            preloader_add_task(app->preloader, filepath, priority, content_width, content_height);
            cursor = cursor->next;
        }
    }
}

static GList* app_preview_get_selected_link(PixelTermApp *app) {
    if (!app || !app->image_files || app->preview.selected < 0) {
        return NULL;
    }
    if (app->total_images > 0 && app->preview.selected >= app->total_images) {
        return NULL;
    }
    GList *cursor = app_preview_find_link_with_hint(app,
                                                    app->preview.selected,
                                                    app->preview.selected_link,
                                                    app->preview.selected_link_index);
    if (!cursor) {
        app->preview.selected_link = NULL;
        app->preview.selected_link_index = -1;
        return NULL;
    }
    app->preview.selected_link = cursor;
    app->preview.selected_link_index = app->preview.selected;
    return cursor;
}

static void app_preview_render_selected_filename(PixelTermApp *app) {
    if (!app || app->ui_text_hidden || app->term_height < 3) {
        return;
    }

    GList *selected_link = app_preview_get_selected_link(app);
    const gchar *sel_path = selected_link ? (const gchar*)selected_link->data : NULL;
    if (!sel_path) {
        return;
    }

    gchar *base = g_path_get_basename(sel_path);
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

typedef struct {
    PixelTermApp *app;
    ImageRenderer *renderer;
    GList *cursor;
} PreviewGridRenderContext;

static GridRenderResult app_preview_grid_render_cell(const GridRenderContext *context,
                                                     const GridRenderCell *cell,
                                                     void *userdata) {
    PreviewGridRenderContext *render_ctx = (PreviewGridRenderContext *)userdata;
    if (!render_ctx || !render_ctx->app || !render_ctx->renderer) {
        return GRID_RENDER_STOP_ALL;
    }

    if (!render_ctx->cursor) {
        return GRID_RENDER_STOP_ALL;
    }

    PixelTermApp *app = render_ctx->app;
    const gchar *filepath = (const gchar*)render_ctx->cursor->data;
    render_ctx->cursor = render_ctx->cursor->next;
    if (!filepath) {
        return GRID_RENDER_STOP_ROW;
    }

    MediaKind media_kind = media_classify(filepath);
    gboolean is_video = media_is_video(media_kind);

    const char *border_style =
        (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) ? "\033[33;1m" : "\033[34;1m";
    app_draw_grid_cell_background(context->layout,
                                  cell->cell_x,
                                  cell->cell_y,
                                  cell->use_border,
                                  border_style);

    gboolean rendered_from_preload = FALSE;
    gboolean rendered_from_renderer_cache = FALSE;
    GString *rendered = NULL;

    if (app->preloader && app->preload_enabled) {
        rendered = preloader_get_cached_image(app->preloader,
                                              filepath,
                                              context->content_width,
                                              context->content_height);
        rendered_from_preload = (rendered != NULL);
    }

    if (!rendered) {
        if (is_video) {
            guint8 *frame_pixels = NULL;
            gint frame_width = 0;
            gint frame_height = 0;
            gint frame_rowstride = 0;
            if (video_player_get_first_frame(filepath,
                                             &frame_pixels,
                                             &frame_width,
                                             &frame_height,
                                             &frame_rowstride) == ERROR_NONE) {
                rendered = renderer_render_image_data(render_ctx->renderer,
                                                      frame_pixels,
                                                      frame_width,
                                                      frame_height,
                                                      frame_rowstride,
                                                      4);
            }
            g_free(frame_pixels);
        } else {
            rendered = renderer_render_image_file(render_ctx->renderer, filepath);
            if (rendered) {
                GString *cached_entry = renderer_cache_get(render_ctx->renderer, filepath);
                rendered_from_renderer_cache = (cached_entry == rendered);
            }
        }
    }

    if (!rendered) {
        if (is_video) {
            const char *label = "VIDEO";
            gint label_len = (gint)strlen(label);
            gint label_row = cell->content_y + context->content_height / 2;
            gint label_col = cell->content_x + (context->content_width - label_len) / 2;
            if (label_row < cell->content_y) label_row = cell->content_y;
            if (label_col < cell->content_x) label_col = cell->content_x;
            printf("\033[%d;%dH\033[35m%s\033[0m", label_row, label_col, label);
        }
        return GRID_RENDER_CONTINUE;
    }

    if (!rendered_from_preload && app->preloader && app->preload_enabled) {
        gint rendered_w = 0, rendered_h = 0;
        renderer_get_rendered_dimensions(render_ctx->renderer, &rendered_w, &rendered_h);
        preloader_cache_add(app->preloader,
                            filepath,
                            rendered,
                            rendered_w,
                            rendered_h,
                            context->content_width,
                            context->content_height);
    }

    app_draw_rendered_lines(cell->content_x,
                            cell->content_y,
                            context->content_width,
                            context->content_height,
                            rendered);

    if (!rendered_from_renderer_cache) {
        g_string_free(rendered, TRUE);
    }

    return GRID_RENDER_CONTINUE;
}



static void app_preview_clear_cell_border(const PixelTermApp *app,
                                          const PreviewLayout *layout,
                                          gint index,
                                          gint start_row,
                                          gint vertical_offset) {
    if (!app || !layout) {
        return;
    }
    gint cell_x = 0;
    gint cell_y = 0;
    if (!app_grid_get_cell_origin(layout,
                                  index,
                                  app->total_images,
                                  start_row,
                                  vertical_offset,
                                  &cell_x,
                                  &cell_y)) {
        return;
    }
    app_grid_clear_cell_border(layout, cell_x, cell_y);
}

static void app_preview_draw_cell_border(const PixelTermApp *app,
                                         const PreviewLayout *layout,
                                         gint index,
                                         gint start_row,
                                         gint vertical_offset) {
    if (!app || !layout) {
        return;
    }
    gint cell_x = 0;
    gint cell_y = 0;
    if (!app_grid_get_cell_origin(layout,
                                  index,
                                  app->total_images,
                                  start_row,
                                  vertical_offset,
                                  &cell_x,
                                  &cell_y)) {
        return;
    }
    const char *border_style = (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL)
                                   ? "\033[33;1m"
                                   : "\033[34;1m";
    app_grid_draw_cell_border(layout, cell_x, cell_y, border_style);
}

// Print brief info for the currently selected preview item on the status line
ErrorCode app_preview_print_info(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    // Use current selection (preview) or current image index if available
    const gchar *filepath = NULL;
    gint display_index = 0;
    if (app_is_preview_mode(app)) {
        GList *selected_link = app_preview_get_selected_link(app);
        filepath = selected_link ? (const gchar*)selected_link->data : NULL;
        display_index = app->preview.selected;
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
    renderer_get_media_dimensions(filepath, &width, &height);
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

// Move selection inside preview grid
ErrorCode app_preview_move_selection(PixelTermApp *app, gint delta_row, gint delta_col) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    PreviewLayout layout = app_preview_calculate_layout(app);
    gint cols = layout.cols;
    gint rows = layout.rows;
    gint visible_rows = layout.visible_rows > 0 ? layout.visible_rows : 1;
    gint max_scroll = MAX(0, rows - visible_rows);

    gint old_scroll = app->preview.scroll;

    gint row = app->preview.selected / cols;
    gint col = app->preview.selected % cols;

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
        app->preview.scroll = 0;
    } else if (delta_row < 0 && row < 0) {
        row = rows - 1;
        app->preview.scroll = max_scroll;
    } else if (delta_row > 0 && row >= app->preview.scroll + layout.visible_rows) {
        gint new_scroll = MIN(app->preview.scroll + layout.visible_rows, max_scroll);
        app->preview.scroll = new_scroll;
        row = new_scroll; // first row of next page, keep column
    } else if (delta_row < 0 && row < app->preview.scroll) {
        gint new_scroll = MAX(app->preview.scroll - layout.visible_rows, 0);
        app->preview.scroll = new_scroll;
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
    app_preview_set_selected_index(app, new_index);

    app_preview_adjust_scroll(app, &layout);
    if (app->preview.scroll != old_scroll) {
        app->needs_screen_clear = TRUE;
    }
    return ERROR_NONE;
}

// Jump a page up/down based on visible rows
ErrorCode app_preview_page_move(PixelTermApp *app, gint direction) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    PreviewLayout layout = app_preview_calculate_layout(app);
    gint rows_per_page = layout.visible_rows > 0 ? layout.visible_rows : 1;
    gint total_pages = (layout.rows + rows_per_page - 1) / rows_per_page;
    if (total_pages <= 1) {
        // Page scrolling is a no-op when everything fits on one page.
        return ERROR_NONE;
    }

    gint old_selected = app->preview.selected;
    gint old_scroll = app->preview.scroll;
    gint rows = layout.rows;
    gint cols = layout.cols;
    gint max_scroll = MAX(0, rows - rows_per_page);

    gint current_row = cols > 0 ? app->preview.selected / cols : 0;
    gint current_col = cols > 0 ? app->preview.selected % cols : 0;
    gint relative_row = current_row - app->preview.scroll;
    if (relative_row < 0) relative_row = 0;
    if (relative_row >= rows_per_page) relative_row = rows_per_page - 1;

    gint delta_scroll = direction >= 0 ? rows_per_page : -rows_per_page;
    gint new_scroll = app->preview.scroll + delta_scroll;
    if (new_scroll < 0) new_scroll = 0;
    if (new_scroll > max_scroll) new_scroll = max_scroll;

    gint new_row = new_scroll + relative_row;
    if (new_row < 0) new_row = 0;
    if (new_row >= rows) new_row = rows - 1;

    if (current_col < 0) current_col = 0;
    if (current_col >= cols) current_col = cols - 1;

    gint new_index = new_row * cols + current_col;
    gint row_start = new_row * cols;
    gint row_end = MIN(app->total_images - 1, row_start + cols - 1);
    if (new_index < row_start) new_index = row_start;
    if (new_index > row_end) new_index = row_end;
    if (new_index >= app->total_images) new_index = app->total_images - 1;
    if (new_index < 0) new_index = 0;

    app->preview.scroll = new_scroll;
    app_preview_set_selected_index(app, new_index);

    if (app->preview.scroll != old_scroll) {
        app->needs_screen_clear = TRUE;
    }
    if (app->preview.selected == old_selected && app->preview.scroll == old_scroll) {
        return ERROR_NONE;
    }
    return ERROR_NONE;
}

// Change preview zoom (target cell width) by stepping column count
ErrorCode app_preview_change_zoom(PixelTermApp *app, gint delta) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
    }

    gint usable_width = app->term_width > 0 ? app->term_width : 80;

    // Initialize if needed (default to 4 columns)
    if (app->preview.zoom <= 0) {
        app->preview.zoom = usable_width / 4; // Start with 4 columns
    }

    // Calculate current implied columns with proper rounding
    gint current_cols = (gint)(usable_width / app->preview.zoom + 0.5f);
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
    app->preview.zoom = (gdouble)usable_width / new_cols;
    if (app->preview.zoom < 1) app->preview.zoom = 1;

    // Mark for screen clear since layout changed
    app->needs_screen_clear = TRUE;

    // Only refresh if zoom actually changed
    return app_render_preview_grid(app);
}

ErrorCode app_handle_mouse_click_preview(PixelTermApp *app,
                                         gint mouse_x,
                                         gint mouse_y,
                                         gboolean *redraw_needed,
                                         gboolean *out_hit) {
    if (!app) {
        if (redraw_needed) *redraw_needed = FALSE;
        if (out_hit) *out_hit = FALSE;
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_preview_mode(app)) {
        if (redraw_needed) *redraw_needed = FALSE;
        if (out_hit) *out_hit = FALSE;
        return ERROR_INVALID_ARGS;
    }
    if (redraw_needed) *redraw_needed = FALSE; // Default to no redraw
    if (out_hit) *out_hit = FALSE; // Default to no hit

    PreviewLayout layout = app_preview_calculate_layout(app);
    gint start_row = app->preview.scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint vertical_offset = app_preview_compute_vertical_offset(app, &layout, start_row, end_row);
    gint grid_top_y = layout.header_lines + 1 + vertical_offset;

    // Check if click is in header area
    if (mouse_y < grid_top_y) {
        return ERROR_NONE; // Ignore clicks in header
    }

    // Calculate clicked cell position
    gint col = (mouse_x - 1) / layout.cell_width;
    gint row_in_visible = (mouse_y - grid_top_y) / layout.cell_height;
    gint absolute_row = start_row + row_in_visible;

    // Check bounds
    gint rows_drawn = MAX(0, end_row - start_row);
    if (col < 0 || col >= layout.cols || row_in_visible < 0 || row_in_visible >= rows_drawn) {
        return ERROR_NONE; // Out of bounds
    }

    // Calculate image index
    gint index = absolute_row * layout.cols + col;

    // Check if index is valid
    if (index >= 0 && index < app->total_images) {
        if (out_hit) *out_hit = TRUE;
        if (app->preview.selected != index) { // Check if selection actually changed
            app_preview_set_selected_index(app, index);
            app->current_index = index; // Also update current index for consistency
            if (redraw_needed) *redraw_needed = TRUE;
        }
    }

    return ERROR_NONE;
}


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
        if (is_valid_media_file(filepath)) {
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
            app->preview.selected_link = NULL;
            app->preview.selected_link_index = -1;
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
    if (app->video_player) {
        video_player_stop(app->video_player);
    }

    (void)app_transition_mode(app, APP_MODE_PREVIEW);
    app_preview_set_selected_index(app, app->current_index >= 0 ? app->current_index : 0);

    // For yellow border mode (RETURN_MODE_PREVIEW_VIRTUAL), always select first image
    if (app->return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL) {
        app_preview_set_selected_index(app, 0);
    }

    app->info_visible = FALSE;
    app->needs_redraw = TRUE;

    // Clear screen on mode entry to avoid ghosting
    ui_clear_screen_for_refresh(app);
    fflush(stdout);

    app_preloader_clear_queue(app);
    return ERROR_NONE;
}


ErrorCode app_exit_preview(PixelTermApp *app, gboolean open_selected) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_preview_mode(app)) {
        return ERROR_NONE;
    }

    if (open_selected && app_has_images(app)) {
        if (app->preview.selected >= 0 && app->preview.selected < app->total_images) {
            app->current_index = app->preview.selected;
        }
        app->image_zoom = 1.0;
        app->image_pan_x = 0.0;
        app->image_pan_y = 0.0;
    }

    (void)app_transition_mode(app, APP_MODE_SINGLE);
    app->info_visible = FALSE;
    app->needs_redraw = TRUE;
    app_preloader_queue_directory(app);
    return ERROR_NONE;
}

// Render preview grid of images
ErrorCode app_render_preview_grid(PixelTermApp *app) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
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
        // Inside preview mode, prefer a normal clear to avoid extra terminal work.
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
    ErrorCode renderer_error = ERROR_NONE;
    ImageRenderer *renderer = app_create_grid_renderer(app, content_width, content_height, &renderer_error);
    if (!renderer) {
        return renderer_error != ERROR_NONE ? renderer_error : ERROR_MEMORY_ALLOC;
    }

    if (!app->ui_text_hidden) {
        // Header: title + page indicator on row 2; keep 3 header lines total
        const char *title = "Preview Grid";
        gint title_len = strlen(title);
        gint pad = (app->term_width > title_len) ? (app->term_width - title_len) / 2 : 0;
        printf("\033[1;1H\033[2K");
        for (gint i = 0; i < pad; i++) putchar(' ');
        printf("%s", title);

        // Row 3: Page indicator centered (numbers only)
        gint rows_per_page = layout.visible_rows > 0 ? layout.visible_rows : 1;
        gint total_pages = (layout.rows + rows_per_page - 1) / rows_per_page;
        if (total_pages < 1) total_pages = 1;
        gint current_page = (app->preview.scroll + rows_per_page - 1) / rows_per_page + 1;
        if (current_page < 1) current_page = 1;
        if (current_page > total_pages) current_page = total_pages;
        char page_text[32];
        g_snprintf(page_text, sizeof(page_text), "%d/%d", current_page, total_pages);
        gint page_len = (gint)strlen(page_text);
        gint page_pad = (app->term_width > page_len) ? (app->term_width - page_len) / 2 : 0;
        printf("\033[3;1H\033[2K");
        for (gint i = 0; i < page_pad; i++) putchar(' ');
        printf("%s", page_text);

        // Row 2: spacer
        printf("\033[2;1H\033[2K");
    }

    gint start_row = app->preview.scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint vertical_offset = app_preview_compute_vertical_offset(app, &layout, start_row, end_row);
    gint start_index = start_row * layout.cols;
    GList *cursor = app_preview_find_link_with_hint(app,
                                                    start_index,
                                                    app->preview.selected_link,
                                                    app->preview.selected_link_index);
    GridRenderContext grid_context = {
        .layout = &layout,
        .start_row = start_row,
        .end_row = end_row,
        .vertical_offset = vertical_offset,
        .content_width = content_width,
        .content_height = content_height,
        .total_items = app->total_images,
        .selected_index = app->preview.selected
    };
    PreviewGridRenderContext render_ctx = {
        .app = app,
        .renderer = renderer,
        .cursor = cursor
    };
    grid_render_cells(&grid_context, app_preview_grid_render_cell, &render_ctx);

    app_preview_render_selected_filename(app);

    // Footer hints centered on last line
    if (app->term_height > 0 && !app->ui_text_hidden) {
        const HelpSegment segments[] = {
            {"←/→/↑/↓", "Move"},
            {"PgUp/PgDn", "Page"},
            {"Enter", "Open"},
            {"TAB", "Toggle"},
            {"r", "Delete"},
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

ErrorCode app_render_preview_selection_change(PixelTermApp *app, gint old_index) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_is_preview_mode(app)) {
        return ERROR_INVALID_ARGS;
    }
    if (!app_has_images(app)) {
        return ERROR_INVALID_IMAGE;
    }

    gint old_scroll = app->preview.scroll;
    PreviewLayout layout = app_preview_calculate_layout(app);
    app_preview_adjust_scroll(app, &layout);
    if (app->preview.scroll != old_scroll) {
        return app_render_preview_grid(app);
    }

    gint selected_row = app->preview.selected / layout.cols;
    if (selected_row < app->preview.scroll ||
        selected_row >= app->preview.scroll + layout.visible_rows) {
        return app_render_preview_grid(app);
    }

    app_preview_queue_preloads(app, &layout);

    gint start_row = app->preview.scroll;
    gint end_row = MIN(layout.rows, start_row + layout.visible_rows);
    gint vertical_offset = app_preview_compute_vertical_offset(app, &layout, start_row, end_row);

    if (old_index != app->preview.selected) {
        app_preview_clear_cell_border(app, &layout, old_index, start_row, vertical_offset);
    }
    app_preview_draw_cell_border(app, &layout, app->preview.selected, start_row, vertical_offset);
    app_preview_render_selected_filename(app);

    ui_end_sync_update();
    fflush(stdout);
    return ERROR_NONE;
}
