#include <glib.h>

#include <string.h>

#include "app.h"
#include "grid_render.h"
#include "ui_render_utils.h"

typedef struct {
    gint create_grid_renderer_calls;
    gint grid_render_calls;
    gint term_width;
    gint term_height;
} BookPreviewStubState;

static BookPreviewStubState g_book_preview_stub_state;

static void reset_book_preview_stubs(void) {
    memset(&g_book_preview_stub_state, 0, sizeof(g_book_preview_stub_state));
    g_book_preview_stub_state.term_width = 80;
    g_book_preview_stub_state.term_height = 30;
}

static void init_book_preview_app(PixelTermApp *app,
                                  gint page_count,
                                  gint zoom,
                                  gint term_width,
                                  gint term_height) {
    g_assert_nonnull(app);

    memset(app, 0, sizeof(*app));
    app->mode = APP_MODE_BOOK_PREVIEW;
    app->ui_text_hidden = TRUE;
    app->term_width = term_width;
    app->term_height = term_height;
    app->book.doc = (BookDocument *)0x1;
    app->book.page_count = page_count;
    app->book.preview_zoom = zoom;

    reset_book_preview_stubs();
    g_book_preview_stub_state.term_width = term_width;
    g_book_preview_stub_state.term_height = term_height;
}

static void cleanup_book_preview_app(PixelTermApp *app) {
    if (!app) {
        return;
    }

    app->book.doc = NULL;
    app->book.page_count = 0;
}

static void test_scroll_pages_keeps_last_page_non_overlapping(void) {
    PixelTermApp app;

    init_book_preview_app(&app, 10, 40, 80, 63);
    app.book.preview_selected = 5;

    g_assert_cmpint(app_book_preview_scroll_pages(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.book.preview_scroll, ==, 3);

    cleanup_book_preview_app(&app);
}

static void test_render_book_preview_normalizes_last_page_to_page_boundary(void) {
    PixelTermApp app;

    init_book_preview_app(&app, 10, 40, 80, 63);
    app.book.preview_selected = 9;

    g_assert_cmpint(app_render_book_preview(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.book.preview_scroll, ==, 3);
    g_assert_cmpint(g_book_preview_stub_state.create_grid_renderer_calls, ==, 1);
    g_assert_cmpint(g_book_preview_stub_state.grid_render_calls, ==, 1);

    cleanup_book_preview_app(&app);
}

gint app_preview_bottom_reserved_lines(const PixelTermApp *app) {
    (void)app;
    return 0;
}

gint app_preview_compute_vertical_offset(const PixelTermApp *app,
                                         const PreviewLayout *layout,
                                         gint start_row,
                                         gint end_row) {
    (void)app;
    (void)layout;
    (void)start_row;
    (void)end_row;
    return 0;
}

ImageRenderer *app_create_grid_renderer(const PixelTermApp *app,
                                        gint content_width,
                                        gint content_height,
                                        ErrorCode *out_error) {
    (void)app;
    (void)content_width;
    (void)content_height;
    g_book_preview_stub_state.create_grid_renderer_calls++;
    if (out_error) {
        *out_error = ERROR_NONE;
    }
    return g_new0(ImageRenderer, 1);
}

void get_terminal_size(gint *width, gint *height) {
    if (width) {
        *width = g_book_preview_stub_state.term_width;
    }
    if (height) {
        *height = g_book_preview_stub_state.term_height;
    }
}

void grid_render_cells(const GridRenderContext *context,
                       GridRenderCellFn callback,
                       void *userdata) {
    (void)context;
    (void)callback;
    (void)userdata;
    g_book_preview_stub_state.grid_render_calls++;
}

void renderer_destroy(ImageRenderer *renderer) {
    g_free(renderer);
}

void app_draw_grid_cell_background(const PreviewLayout *layout,
                                   gint cell_x,
                                   gint cell_y,
                                   gboolean use_border,
                                   const char *border_style) {
    (void)layout;
    (void)cell_x;
    (void)cell_y;
    (void)use_border;
    (void)border_style;
}

void app_draw_preview_content(gint content_x,
                              gint content_y,
                              gint content_width,
                              gint content_height,
                              gint rendered_width,
                              gint rendered_height,
                              gboolean graphics_mode,
                              const GString *rendered) {
    (void)content_x;
    (void)content_y;
    (void)content_width;
    (void)content_height;
    (void)rendered_width;
    (void)rendered_height;
    (void)graphics_mode;
    (void)rendered;
}

ErrorCode book_render_page(BookDocument *doc,
                           gint page_index,
                           gint target_cols,
                           gint target_rows,
                           BookPageImage *out_image) {
    (void)doc;
    (void)page_index;
    (void)target_cols;
    (void)target_rows;
    if (out_image) {
        memset(out_image, 0, sizeof(*out_image));
    }
    return ERROR_INVALID_IMAGE;
}

void book_page_image_free(BookPageImage *image) {
    (void)image;
}

GString *renderer_render_image_data(ImageRenderer *renderer,
                                    const guint8 *pixel_data,
                                    gint width,
                                    gint height,
                                    gint rowstride,
                                    gint n_channels) {
    (void)renderer;
    (void)pixel_data;
    (void)width;
    (void)height;
    (void)rowstride;
    (void)n_channels;
    return NULL;
}

void renderer_get_rendered_dimensions(ImageRenderer *renderer, gint *width, gint *height) {
    (void)renderer;
    if (width) {
        *width = 0;
    }
    if (height) {
        *height = 0;
    }
}

gboolean renderer_is_graphics_mode(const ImageRenderer *renderer) {
    (void)renderer;
    return FALSE;
}

gchar *sanitize_for_terminal(const gchar *text) {
    return g_strdup(text ? text : "");
}

gchar *truncate_utf8_middle_keep_suffix(const gchar *text, gint max_width) {
    (void)max_width;
    return g_strdup(text ? text : "");
}

gint ui_filename_max_width(const PixelTermApp *app) {
    return app ? app->term_width : 0;
}

void ui_print_centered_help_line(gint row,
                                 gint term_width,
                                 const HelpSegment *segments,
                                 gsize n) {
    (void)row;
    (void)term_width;
    (void)segments;
    (void)n;
}

gint utf8_display_width(const gchar *text) {
    return text ? (gint)strlen(text) : 0;
}

void ui_clear_single_view_lines(const PixelTermApp *app) {
    (void)app;
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/app_preview_book/scroll_pages/keeps_last_page_non_overlapping",
                    test_scroll_pages_keeps_last_page_non_overlapping);
    g_test_add_func("/app_preview_book/render/normalizes_last_page_to_page_boundary",
                    test_render_book_preview_normalizes_last_page_to_page_boundary);

    return g_test_run();
}
