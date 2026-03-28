#include "app_preview_render_internal.h"

#include "app_preview_shared_internal.h"
#include "media_utils.h"
#include "preloader.h"
#include "text_utils.h"
#include "ui_render_utils.h"
#include "video_player.h"

#include <libavutil/mem.h>

typedef struct {
    PixelTermApp *app;
    ImageRenderer *renderer;
    GList *cursor;
} PreviewGridRenderContext;

static GridRenderResult app_preview_render_cell(const GridRenderContext *context,
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
    gboolean rendered_owned = FALSE;
    GString *rendered = NULL;
    gint rendered_w = 0;
    gint rendered_h = 0;
    gboolean graphics_mode = FALSE;

    if (app->preloader && app->preload_enabled) {
        rendered = preloader_get_cached_image(app->preloader,
                                              filepath,
                                              context->content_width,
                                              context->content_height);
        rendered_from_preload = (rendered != NULL);
        rendered_owned = rendered_from_preload;
        if (rendered_from_preload) {
            (void)preloader_get_cached_render_info(app->preloader,
                                                   filepath,
                                                   context->content_width,
                                                   context->content_height,
                                                   &rendered_w,
                                                   &rendered_h,
                                                   &graphics_mode);
        }
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
                rendered_owned = (rendered != NULL);
            }
            av_free(frame_pixels);
        } else {
            rendered = renderer_render_image_file(render_ctx->renderer, filepath);
            rendered_owned = (rendered != NULL);
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

    if (!rendered_from_preload) {
        renderer_get_rendered_dimensions(render_ctx->renderer, &rendered_w, &rendered_h);
        graphics_mode = renderer_is_graphics_mode(render_ctx->renderer);
    }

    if (!rendered_from_preload && app->preloader && app->preload_enabled) {
        preloader_cache_add(app->preloader,
                            filepath,
                            rendered,
                            rendered_w,
                            rendered_h,
                            graphics_mode,
                            context->content_width,
                            context->content_height);
    }

    app_draw_preview_content(cell->content_x,
                             cell->content_y,
                             context->content_width,
                             context->content_height,
                             rendered_w,
                             rendered_h,
                             graphics_mode,
                             rendered);
    if (rendered_owned) {
        g_string_free(rendered, TRUE);
    }

    return GRID_RENDER_CONTINUE;
}

void app_preview_render_cells(const GridRenderContext *context,
                              PixelTermApp *app,
                              ImageRenderer *renderer,
                              GList *cursor) {
    if (!context || !app || !renderer) {
        return;
    }

    PreviewGridRenderContext render_ctx = {
        .app = app,
        .renderer = renderer,
        .cursor = cursor
    };
    grid_render_cells(context, app_preview_render_cell, &render_ctx);
}

void app_preview_render_selected_filename(PixelTermApp *app) {
    if (!app || app->ui_text_hidden || app->term_height < 3) {
        return;
    }

    const gchar *sel_path = app_preview_get_selected_filepath(app);
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

void app_preview_clear_cell_border(const PixelTermApp *app,
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

void app_preview_draw_cell_border(const PixelTermApp *app,
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

    const gchar *filepath = app_preview_get_selected_filepath(app);
    gint display_index = app->preview.selected;
    if (!filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    gchar *basename = g_path_get_basename(filepath);
    gchar *dirname = g_path_get_dirname(filepath);
    gchar *safe_basename = sanitize_for_terminal(basename);
    gchar *safe_dirname = sanitize_for_terminal(dirname);
    gint width = 0;
    gint height = 0;
    renderer_get_media_dimensions(filepath, &width, &height);
    gint64 file_size = get_file_size(filepath);
    gdouble file_size_mb = file_size > 0 ? file_size / (1024.0 * 1024.0) : 0.0;
    const char *ext = get_file_extension(filepath);
    gdouble aspect = (height > 0) ? (gdouble)width / height : 0.0;

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
