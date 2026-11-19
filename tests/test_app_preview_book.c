#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <glib.h>

#include <string.h>

#include "app.h"
#include "grid_render.h"
#include "ui_render_utils.h"
#include "terminal_output.h"
#include "test_output_stream.h"

/* Exercise TOC frame ownership with the same output sink as book pages. */
#include "../src/app_book_toc.c"

typedef struct {
    gint create_grid_renderer_calls;
    gint grid_render_calls;
    gint renderer_initialize_calls;
    gint term_width;
    gint term_height;
    gboolean last_force_text;
    gboolean last_force_sixel;
    gboolean last_force_kitty;
    gboolean last_force_iterm2;
    gboolean renderer_fail;
    gint renderer_destroy_calls;
    gint help_calls;
    gboolean real_frame_controls;
    gboolean double_page;
    gint full_clears;
    gint partial_clears;
} BookPreviewStubState;

static BookPreviewStubState g_book_preview_stub_state;

ErrorCode app_prepare_mode_entry(PixelTermApp *app, gboolean clear_preloader_queue) {
    (void)app;
    (void)clear_preloader_queue;
    return ERROR_NONE;
}

ErrorCode app_transition_mode(PixelTermApp *app, AppMode mode) {
    app->mode = mode;
    app->info_visible = FALSE;
    app->help_visible = FALSE;
    return ERROR_NONE;
}

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

static void test_entry_with_previous_error(void) {
    PixelTermApp app;
    init_book_preview_app(&app, 4, 20, 80, 30);
    app.mode = APP_MODE_SINGLE;
    app.last_error = ERROR_OUTPUT_IO;
    g_assert_cmpint(app_enter_book_preview(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_BOOK_PREVIEW);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(app_enter_book_page(&app, 2), ==, ERROR_NONE);
    g_assert_cmpint(app.mode, ==, APP_MODE_BOOK);
    g_assert_cmpint(app.book.page, ==, 2);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    cleanup_book_preview_app(&app);
}

static void test_scroll_pages_keeps_last_page_non_overlapping(void) {
    PixelTermApp app;

    init_book_preview_app(&app, 10, 40, 80, 63);
    app.book.preview_selected = 5;

    g_assert_cmpint(app_book_preview_scroll_pages(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.book.preview_scroll, ==, 3);

    cleanup_book_preview_app(&app);
}

static void test_scroll_pages_persists_destination_page_after_render(void) {
    PixelTermApp app;

    init_book_preview_app(&app, 10, 40, 80, 63);
    app.book.preview_selected = 5;

    g_assert_cmpint(app_book_preview_scroll_pages(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app_render_book_preview(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.book.preview_selected, ==, 9);
    g_assert_cmpint(app.book.preview_scroll, ==, 3);
    g_assert_cmpint(g_book_preview_stub_state.create_grid_renderer_calls, ==, 1);
    g_assert_cmpint(g_book_preview_stub_state.grid_render_calls, ==, 1);

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

static void test_page_move_round_trip_restores_selection_on_short_last_page(void) {
    PixelTermApp app;

    init_book_preview_app(&app, 10, 40, 80, 63);
    app.book.preview_selected = 5;

    g_assert_cmpint(app_book_preview_page_move(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.book.preview_selected, ==, 9);
    g_assert_cmpint(app.book.preview_scroll, ==, 3);

    g_assert_cmpint(app_book_preview_page_move(&app, -1), ==, ERROR_NONE);
    g_assert_cmpint(app.book.preview_selected, ==, 5);
    g_assert_cmpint(app.book.preview_scroll, ==, 0);

    cleanup_book_preview_app(&app);
}

static void test_page_move_round_trip_restores_middle_row_on_short_last_page(void) {
    PixelTermApp app;

    init_book_preview_app(&app, 10, 40, 80, 63);
    app.book.preview_selected = 3;

    g_assert_cmpint(app_book_preview_page_move(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.book.preview_selected, ==, 7);
    g_assert_cmpint(app.book.preview_scroll, ==, 3);

    g_assert_cmpint(app_book_preview_page_move(&app, -1), ==, ERROR_NONE);
    g_assert_cmpint(app.book.preview_selected, ==, 3);
    g_assert_cmpint(app.book.preview_scroll, ==, 0);

    cleanup_book_preview_app(&app);
}

static void test_page_move_clamps_top_slot_on_short_last_page_after_render(void) {
    PixelTermApp app;

    init_book_preview_app(&app, 10, 40, 80, 63);
    app.book.preview_selected = 1;

    g_assert_cmpint(app_book_preview_page_move(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.book.preview_selected, ==, 7);
    g_assert_cmpint(app.book.preview_scroll, ==, 3);

    g_assert_cmpint(app_render_book_preview(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.book.preview_selected, ==, 7);
    g_assert_cmpint(app.book.preview_scroll, ==, 3);
    g_assert_cmpint(g_book_preview_stub_state.create_grid_renderer_calls, ==, 1);
    g_assert_cmpint(g_book_preview_stub_state.grid_render_calls, ==, 1);

    cleanup_book_preview_app(&app);
}

static void test_page_move_round_trip_uses_clamped_column_on_partial_final_row(void) {
    PixelTermApp app;

    init_book_preview_app(&app, 15, 20, 80, 30);
    app.book.preview_selected = 7;

    g_assert_cmpint(app_book_preview_page_move(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.book.preview_selected, ==, 14);
    g_assert_cmpint(app.book.preview_scroll, ==, 2);

    g_assert_cmpint(app_book_preview_page_move(&app, -1), ==, ERROR_NONE);
    g_assert_cmpint(app.book.preview_selected, ==, 6);
    g_assert_cmpint(app.book.preview_scroll, ==, 0);

    cleanup_book_preview_app(&app);
}

static void test_book_page_help_overlay_forces_text_rendering(void) {
    PixelTermApp app;

    memset(&app, 0, sizeof(app));
    app.mode = APP_MODE_BOOK;
    app.term_width = 120;
    app.term_height = 40;
    app.book.doc = (BookDocument *)0x1;
    app.book.page_count = 1;
    app.force_kitty = TRUE;
    app.help_visible = TRUE;

    reset_book_preview_stubs();
    g_book_preview_stub_state.term_width = app.term_width;
    g_book_preview_stub_state.term_height = app.term_height;

    g_assert_cmpint(app_render_book_page(&app), ==, ERROR_NONE);
    g_assert_cmpint(g_book_preview_stub_state.renderer_initialize_calls, ==, 1);
    g_assert_true(g_book_preview_stub_state.last_force_text);
    g_assert_false(g_book_preview_stub_state.last_force_kitty);
    g_assert_false(g_book_preview_stub_state.last_force_sixel);
    g_assert_false(g_book_preview_stub_state.last_force_iterm2);

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
    if (g_book_preview_stub_state.renderer_fail) {
        if (out_error) *out_error = ERROR_INVALID_IMAGE;
        return NULL;
    }
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
    g_book_preview_stub_state.renderer_destroy_calls++;
    g_free(renderer);
}

ImageRenderer *renderer_create(void) {
    return g_new0(ImageRenderer, 1);
}

ErrorCode renderer_initialize(ImageRenderer *renderer, const RendererConfig *config) {
    if (!renderer || !config) {
        return ERROR_MEMORY_ALLOC;
    }
    renderer->config = *config;
    g_book_preview_stub_state.renderer_initialize_calls++;
    g_book_preview_stub_state.last_force_text = config->force_text;
    g_book_preview_stub_state.last_force_sixel = config->force_sixel;
    g_book_preview_stub_state.last_force_kitty = config->force_kitty;
    g_book_preview_stub_state.last_force_iterm2 = config->force_iterm2;
    return ERROR_NONE;
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
        out_image->pixels = (guint8 *)"x";
        out_image->width = 1;
        out_image->height = 1;
        out_image->stride = 4;
        out_image->channels = 4;
    }
    return ERROR_NONE;
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
    return g_string_new("X");
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

RendererConfig app_renderer_config_from_app(const PixelTermApp *app, gint max_width, gint max_height) {
    RendererConfig config = {0};
    config.max_width = max_width;
    config.max_height = max_height;
    config.work_factor = app ? app->render_work_factor : 0;
    config.force_text = app ? app->force_text : FALSE;
    config.force_sixel = app ? app->force_sixel : FALSE;
    config.force_kitty = app ? app->force_kitty : FALSE;
    config.force_iterm2 = app ? app->force_iterm2 : FALSE;
    config.text_symbol_mode = app ? app->text_symbol_mode : TEXT_SYMBOL_MODE_AUTO;
    config.gamma = app ? app->gamma : 1.0;
    config.color_enhance = app ? app->color_enhance : COLOR_ENHANCE_OFF;
    return config;
}

gboolean app_book_use_double_page(const PixelTermApp *app) {
    (void)app;
    return g_book_preview_stub_state.double_page;
}

void app_get_image_target_dimensions(const PixelTermApp *app, gint *max_width, gint *max_height) {
    if (max_width) {
        *max_width = app && app->term_width > 0 ? app->term_width : 80;
    }
    if (max_height) {
        gint height = app && app->term_height > 6 ? app->term_height - 6 : 1;
        *max_height = height;
    }
}

gboolean ui_begin_sync_update(void) {
    if (g_book_preview_stub_state.real_frame_controls) {
        if (terminal_output_write("\033[?2026h", 8)) return TRUE;
        terminal_output_defer("\033[?2026l", TERMINAL_OUTPUT_RESTORE);
        return FALSE;
    }
    return TRUE;
}

void ui_end_sync_update(void) {
    if (g_book_preview_stub_state.real_frame_controls)
        terminal_output_defer("\033[?2026l", TERMINAL_OUTPUT_RESTORE);
}

gboolean ui_clear_kitty_images(const PixelTermApp *app) {
    if (g_book_preview_stub_state.real_frame_controls && app->force_kitty) {
        const char command[] = "\033_Ga=d,q=2\033\\";
        return terminal_output_write(command, sizeof(command) - 1);
    }
    return TRUE;
}

void ui_clear_screen_for_refresh(const PixelTermApp *app) {
    (void)app;
    g_book_preview_stub_state.full_clears++;
}

void ui_clear_area(const PixelTermApp *app, gint top_row, gint height) {
    (void)app;
    (void)top_row;
    (void)height;
    g_book_preview_stub_state.partial_clears++;
}

gchar *sanitize_for_terminal(const gchar *text) {
    return g_strdup(text ? text : "");
}

gchar *truncate_utf8_middle_keep_suffix(const gchar *text, gint max_width) {
    (void)max_width;
    return g_strdup(text ? text : "");
}

gchar *truncate_utf8_for_display(const gchar *text, gint max_width) {
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
    g_book_preview_stub_state.help_calls++;
}

gint utf8_display_width(const gchar *text) {
    return text ? (gint)strlen(text) : 0;
}

void ui_clear_single_view_lines(const PixelTermApp *app) {
    (void)app;
}

gboolean app_grid_get_cell_origin(const PreviewLayout *layout, gint index,
                                  gint total_items, gint start_row, gint vertical_offset,
                                  gint *cell_x, gint *cell_y) {
    (void)layout;
    (void)start_row;
    (void)vertical_offset;
    *cell_x = *cell_y = 1;
    return index >= 0 && index < total_items;
}

void app_grid_clear_cell_border(const PreviewLayout *layout, gint cell_x, gint cell_y) {
    (void)layout;
    printf("\033[%d;%dHclear-border", cell_y, cell_x);
}

void app_grid_draw_cell_border(const PreviewLayout *layout, gint cell_x, gint cell_y,
                               const char *border_style) {
    (void)layout;
    printf("\033[%d;%dH%sdraw-border", cell_y, cell_x, border_style);
}

typedef struct {
    gboolean fail;
    GString *attempted;
    GString *delivered;
} BookOutput;

static ssize_t book_output_write(void *data, const char *bytes, size_t length) {
    BookOutput *sink = data;
    g_string_append_len(sink->attempted, bytes, length);
    if (sink->fail) { errno = EIO; return -1; }
    if (sink->delivered) g_string_append_len(sink->delivered, bytes, length);
    return (ssize_t)length;
}

static void test_book_preview_output(gconstpointer data) {
    gint scenario = GPOINTER_TO_INT(data);
    gboolean selection = scenario & 1;
    gboolean pending = scenario & 2;
    gboolean success = scenario & 4;
    gboolean renderer_fail = scenario & 8;
    gboolean jump = scenario & 16;
    PixelTermApp app;
    init_book_preview_app(&app, 4, 20, 80, 30);
    app.book.preview_selected = 1;
    app.needs_screen_clear = FALSE;
    if (jump) {
        app.ui_text_hidden = FALSE;
        app.book.path = "book.pdf";
        app.book.jump_active = TRUE;
    }
    g_book_preview_stub_state.renderer_fail = renderer_fail;
    BookOutput sink = {.fail = !success && !renderer_fail, .attempted = g_string_new(NULL),
                       .delivered = g_string_new(NULL)};
    FILE *stream = test_output_stream(&sink, book_output_write);
    g_assert_nonnull(stream);
    char buffer[65536];
    g_assert_cmpint(setvbuf(stream, buffer, _IOFBF, sizeof(buffer)), ==, 0);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    if (pending) terminal_output_defer("pending-book", TERMINAL_OUTPUT_RESTORE);
    ErrorCode result = selection ? app_render_book_preview_selection_change(&app, 0)
                                 : app_render_book_preview(&app);
    gchar *attempted = g_strdup(sink.attempted->str);
    g_string_truncate(sink.delivered, 0);
    sink.fail = FALSE;
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    g_assert_cmpint(result, ==, success ? ERROR_NONE : renderer_fail ? ERROR_INVALID_IMAGE : ERROR_OUTPUT_IO);
    g_assert_true(recovered);
    g_assert_cmpstr(sink.delivered->str, ==, success || renderer_fail ? "" : pending ? "\033\\pending-book" : "\033\\");
    if (!success) {
        g_assert_true(app.needs_redraw);
        g_assert_true(app.needs_screen_clear);
    }
    if (pending) {
        g_assert_cmpstr(attempted, ==, "\033\\pending-book");
        g_assert_cmpint(g_book_preview_stub_state.create_grid_renderer_calls, ==, 0);
    } else if (!selection) {
        g_assert_cmpint(g_book_preview_stub_state.create_grid_renderer_calls, ==, 1);
        g_assert_cmpint(g_book_preview_stub_state.renderer_destroy_calls, ==, renderer_fail ? 0 : 1);
    }
    if (renderer_fail) g_assert_cmpstr(attempted, ==, "");
    if (jump) g_assert_cmpint(g_book_preview_stub_state.help_calls, ==, 0);
    g_free(attempted);
    g_string_free(sink.attempted, TRUE);
    g_string_free(sink.delivered, TRUE);
    cleanup_book_preview_app(&app);
}

static void test_book_output_backoff_and_prompt_failure(gconstpointer data) {
    gint scenario = GPOINTER_TO_INT(data);
    gint operation = scenario & 3;
    gboolean backoff = scenario & 4;
    PixelTermApp app;
    init_book_preview_app(&app, 4, 20, 80, 30);
    app.book.jump_active = TRUE;
    BookOutput sink = {.fail = TRUE, .attempted = g_string_new(NULL),
                       .delivered = g_string_new(NULL)};
    FILE *stream = test_output_stream(&sink, book_output_write);
    g_assert_nonnull(stream);
    char buffer[65536];
    g_assert_cmpint(setvbuf(stream, buffer, _IOFBF, sizeof(buffer)), ==, 0);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    if (backoff) {
        terminal_output_defer("pending-book", TERMINAL_OUTPUT_RESTORE);
        g_assert_false(terminal_output_flush());
    }
    ErrorCode result;
    switch (operation) {
        case 0: result = app_book_jump_render_prompt(&app); break;
        case 1: result = app_book_jump_clear_prompt(&app); break;
        case 2: result = app_render_book_preview(&app); break;
        default: result = app_render_book_preview_selection_change(&app, 1); break;
    }
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_true(app.needs_redraw);
    g_assert_true(app.needs_screen_clear);
    /* Retry timing is covered deterministically by test_terminal_output.c;
     * this integration checks error flags and retained recovery controls. */
    sink.fail = FALSE;
    g_assert_true(terminal_output_flush());
    fclose(stream);
    stdout = saved;
    g_assert_cmpstr(sink.delivered->str, ==, backoff ? "\033\\pending-book" : "\033\\");
    g_string_free(sink.attempted, TRUE);
    g_string_free(sink.delivered, TRUE);
    cleanup_book_preview_app(&app);
}

typedef struct {
    gint writes;
    gint successful_writes;
    gboolean recovered;
    GString *delivered;
} PageOutput;

static ssize_t page_output_write(void *data, const char *bytes, size_t length) {
    PageOutput *sink = data;
    if (++sink->writes > sink->successful_writes && !sink->recovered) {
        errno = EIO;
        return -1;
    }
    g_string_append_len(sink->delivered, bytes, length);
    return (ssize_t)length;
}

static void test_book_page_frame_failure(gconstpointer data) {
    gint scenario = GPOINTER_TO_INT(data);
    gint stage = scenario & 3; /* begin, Kitty clear, jump prompt, final flush */
    PixelTermApp app;
    init_book_preview_app(&app, 2, 20, 80, 30);
    app.mode = APP_MODE_BOOK;
    BookToc toc = {0};
    BookTocItem items[40] = {0};
    gboolean is_toc = scenario & 8;
    app.book.toc = is_toc ? &toc : NULL;
    if (is_toc) {
        for (gsize i = 0; i < G_N_ELEMENTS(items); i++) {
            items[i].title = "Chapter";
            items[i].next = i + 1 < G_N_ELEMENTS(items) ? &items[i + 1] : NULL;
        }
        toc.items = items;
        toc.count = G_N_ELEMENTS(items);
        app.book.toc_selected = 30;
        app.book.toc_scroll = 2;
    }
    app.force_kitty = stage == 1;
    app.book.jump_active = stage == 2;
    app.suppress_full_clear = TRUE;
    g_book_preview_stub_state.real_frame_controls = TRUE;
    g_book_preview_stub_state.double_page = scenario & 4;
    PageOutput sink = {.successful_writes = stage == 0 ? 0 : 1,
                       .delivered = g_string_new(NULL)};
    FILE *stream = test_output_stream(&sink, page_output_write);
    g_assert_nonnull(stream);
    char buffer[65536];
    g_assert_cmpint(setvbuf(stream, buffer, _IOFBF, sizeof(buffer)), ==, 0);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    ErrorCode result = is_toc ? app_render_book_toc(&app) : app_render_book_page(&app);
    gint writes_at_return = sink.writes;
    gchar *delivered_at_return = g_strdup(sink.delivered->str);
    g_string_truncate(sink.delivered, 0);
    sink.recovered = TRUE;
    gboolean recovered = terminal_output_flush();
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    if (is_toc) g_assert_cmpint(app.book.toc_scroll, ==, 2);
    g_assert_true(app.needs_redraw);
    g_assert_true(app.needs_screen_clear);
    g_assert_false(app.suppress_full_clear);
    g_assert_true(recovered);
    /* No eager retry after the failing helper; sync-end remains owned by the queue. */
    g_assert_cmpint(writes_at_return, ==, stage == 1 ? 3 : 2);
    g_assert_cmpstr(delivered_at_return, ==, stage == 0 ? "" : "\033[?2026h");
    g_assert_cmpstr(sink.delivered->str, ==, stage == 3
        ? "\033\\\033[?2026l\033\\" : "\033\\\033\\\033[?2026l");
    gint full_clears = g_book_preview_stub_state.full_clears;
    app.suppress_full_clear = TRUE;
    app.book.jump_active = FALSE;
    g_assert_cmpint(is_toc ? app_render_book_toc(&app) : app_render_book_page(&app), ==, ERROR_NONE);
    if (is_toc) g_assert_cmpint(app.book.toc_scroll, ==, 19);
    g_assert_cmpint(g_book_preview_stub_state.full_clears, ==, full_clears + 1);
    g_assert_false(app.needs_screen_clear);
    fclose(stream);
    stdout = saved;
    g_free(delivered_at_return);
    g_string_free(sink.delivered, TRUE);
    app.book.toc = NULL;
    cleanup_book_preview_app(&app);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/app_book_toc/output/begin_failure", GINT_TO_POINTER(8), test_book_page_frame_failure);
    g_test_add_data_func("/app_book_toc/output/clear_failure", GINT_TO_POINTER(9), test_book_page_frame_failure);
    g_test_add_data_func("/app_book_toc/output/final_failure", GINT_TO_POINTER(11), test_book_page_frame_failure);
    g_test_add_data_func("/app_book_page/output/begin_failure", GINT_TO_POINTER(0), test_book_page_frame_failure);
    g_test_add_data_func("/app_book_page/output/clear_failure", GINT_TO_POINTER(1), test_book_page_frame_failure);
    g_test_add_data_func("/app_book_page/output/prompt_failure", GINT_TO_POINTER(2), test_book_page_frame_failure);
    g_test_add_data_func("/app_book_page/output/final_failure", GINT_TO_POINTER(3), test_book_page_frame_failure);
    g_test_add_data_func("/app_book_page/output/spread_begin_failure", GINT_TO_POINTER(4), test_book_page_frame_failure);
    g_test_add_data_func("/app_book_page/output/spread_clear_failure", GINT_TO_POINTER(5), test_book_page_frame_failure);
    g_test_add_data_func("/app_book_page/output/spread_prompt_failure", GINT_TO_POINTER(6), test_book_page_frame_failure);
    g_test_add_data_func("/app_book_page/output/spread_final_failure", GINT_TO_POINTER(7), test_book_page_frame_failure);
    g_test_add_data_func("/app_preview_book/output/prompt_render_failure", GINT_TO_POINTER(0), test_book_output_backoff_and_prompt_failure);
    g_test_add_data_func("/app_preview_book/output/prompt_clear_failure", GINT_TO_POINTER(1), test_book_output_backoff_and_prompt_failure);
    g_test_add_data_func("/app_preview_book/output/prompt_render_backoff", GINT_TO_POINTER(4), test_book_output_backoff_and_prompt_failure);
    g_test_add_data_func("/app_preview_book/output/prompt_clear_backoff", GINT_TO_POINTER(5), test_book_output_backoff_and_prompt_failure);
    g_test_add_data_func("/app_preview_book/output/full_backoff", GINT_TO_POINTER(6), test_book_output_backoff_and_prompt_failure);
    g_test_add_data_func("/app_preview_book/output/selection_backoff", GINT_TO_POINTER(7), test_book_output_backoff_and_prompt_failure);
    g_test_add_func("/app_preview_book/entry/previous_error", test_entry_with_previous_error);
    g_test_add_data_func("/app_preview_book/output/full_final_failure", GINT_TO_POINTER(0), test_book_preview_output);
    g_test_add_data_func("/app_preview_book/output/selection_final_failure", GINT_TO_POINTER(1), test_book_preview_output);
    g_test_add_data_func("/app_preview_book/output/full_pending_failure", GINT_TO_POINTER(2), test_book_preview_output);
    g_test_add_data_func("/app_preview_book/output/selection_pending_failure", GINT_TO_POINTER(3), test_book_preview_output);
    g_test_add_data_func("/app_preview_book/output/full_success", GINT_TO_POINTER(4), test_book_preview_output);
    g_test_add_data_func("/app_preview_book/output/selection_success", GINT_TO_POINTER(5), test_book_preview_output);
    g_test_add_data_func("/app_preview_book/output/renderer_failure", GINT_TO_POINTER(8), test_book_preview_output);
    g_test_add_data_func("/app_preview_book/output/full_jump_failure", GINT_TO_POINTER(16), test_book_preview_output);
    g_test_add_data_func("/app_preview_book/output/selection_jump_failure", GINT_TO_POINTER(17), test_book_preview_output);

    g_test_add_func("/app_preview_book/scroll_pages/keeps_last_page_non_overlapping",
                    test_scroll_pages_keeps_last_page_non_overlapping);
    g_test_add_func("/app_preview_book/scroll_pages/persists_destination_page_after_render",
                    test_scroll_pages_persists_destination_page_after_render);
    g_test_add_func("/app_preview_book/render/normalizes_last_page_to_page_boundary",
                    test_render_book_preview_normalizes_last_page_to_page_boundary);
    g_test_add_func("/app_preview_book/page_move/round_trip_restores_selection_on_short_last_page",
                    test_page_move_round_trip_restores_selection_on_short_last_page);
    g_test_add_func("/app_preview_book/page_move/round_trip_restores_middle_row_on_short_last_page",
                    test_page_move_round_trip_restores_middle_row_on_short_last_page);
    g_test_add_func("/app_preview_book/page_move/clamps_top_slot_on_short_last_page_after_render",
                    test_page_move_clamps_top_slot_on_short_last_page_after_render);
    g_test_add_func("/app_preview_book/page_move/round_trip_uses_clamped_column_on_partial_final_row",
                    test_page_move_round_trip_uses_clamped_column_on_partial_final_row);
    g_test_add_func("/app_book_page/help_overlay/forces_text_rendering",
                    test_book_page_help_overlay_forces_text_rendering);

    return g_test_run();
}
