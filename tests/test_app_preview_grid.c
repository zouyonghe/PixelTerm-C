#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <glib.h>
#include <glib/gstdio.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "app_preview_transaction_internal.h"
#include "app_preview_render_internal.h"
#include "ui_render_utils.h"
#include "terminal_output.h"
#include "test_output_stream.h"
#include <errno.h>

typedef struct {
    gint create_grid_renderer_calls;
    gint term_width;
    gint term_height;
    gint prepare_calls;
    gint transition_calls;
    ErrorCode prepare_error;
    ErrorCode transition_error;
    ErrorCode renderer_error;
    gint preload_mutations;
} PreviewGridStubState;

typedef void (*PreviewGridCaptureFunc)(gpointer user_data);

static PreviewGridStubState g_preview_grid_stub_state;

static void reset_preview_grid_stubs(void) {
    memset(&g_preview_grid_stub_state, 0, sizeof(g_preview_grid_stub_state));
    g_preview_grid_stub_state.term_width = 80;
    g_preview_grid_stub_state.term_height = 30;
}

static void init_preview_app(PixelTermApp *app,
                             gint total_images,
                             gint zoom,
                             gint term_width,
                             gint term_height) {
    g_assert_nonnull(app);

    memset(app, 0, sizeof(*app));
    app->mode = APP_MODE_PREVIEW;
    app->ui_text_hidden = TRUE;
    app->term_width = term_width;
    app->term_height = term_height;
    app->preview.zoom = zoom;
    app->preview.selected_link_index = -1;

    for (gint index = 0; index < total_images; index++) {
        app->image_files = g_list_append(app->image_files,
                                         g_strdup_printf("img-%d", index));
    }
    app->total_images = total_images;

    reset_preview_grid_stubs();
    g_preview_grid_stub_state.term_width = term_width;
    g_preview_grid_stub_state.term_height = term_height;
}

static void cleanup_preview_app(PixelTermApp *app) {
    if (!app) {
        return;
    }

    g_list_free_full(app->image_files, g_free);
    app->image_files = NULL;
    app->total_images = 0;
    app->preview.selected_link = NULL;
    app->preview.selected_link_index = -1;
}

static gchar *capture_output(PreviewGridCaptureFunc draw_func, gpointer user_data) {
    gchar *template = g_strdup_printf("%s/pixelterm-preview-grid-XXXXXX", g_get_tmp_dir());
    int fd = g_mkstemp(template);
    g_assert_cmpint(fd, >=, 0);

    int saved_stdout = dup(STDOUT_FILENO);
    g_assert_cmpint(saved_stdout, >=, 0);

    fflush(stdout);
    g_assert_cmpint(dup2(fd, STDOUT_FILENO), >=, 0);
    close(fd);

    draw_func(user_data);

    fflush(stdout);
    g_assert_cmpint(dup2(saved_stdout, STDOUT_FILENO), >=, 0);
    close(saved_stdout);

    gchar *output = NULL;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(template, &output, NULL, &error));
    g_assert_no_error(error);
    g_remove(template);
    g_free(template);
    return output;
}

static void render_preview_grid_capture(gpointer user_data) {
    PixelTermApp *app = (PixelTermApp *)user_data;
    g_assert_cmpint(app_render_preview_grid(app), ==, ERROR_NONE);
}

static void enter_preview_capture(gpointer user_data) {
    g_assert_cmpint(app_enter_preview(user_data), ==, ERROR_NONE);
}

static void test_enter_preview_clears_kitty_before_screen(void) {
    PixelTermApp app;
    init_preview_app(&app, 2, 20, 80, 30);
    app.mode = APP_MODE_SINGLE;
    app.force_kitty = TRUE;
    app.current_index = 1;
    gchar *output = capture_output(enter_preview_capture, &app);
    const char *delete_images = strstr(output, "\033_Ga=d,q=2\033\\");
    const char *clear_screen = strstr(output, "\033[2J");
    g_assert_nonnull(delete_images);
    g_assert_nonnull(clear_screen);
    g_assert_true(delete_images < clear_screen);
    g_assert_cmpint(app.mode, ==, APP_MODE_PREVIEW);
    g_assert_cmpint(app.preview.selected, ==, 1);
    g_free(output);
    cleanup_preview_app(&app);
}

static void test_enter_preview_other_protocols_skip_kitty_delete(void) {
    for (gint protocol = 0; protocol < 3; protocol++) {
        PixelTermApp app;
        init_preview_app(&app, 1, 20, 80, 30);
        app.mode = APP_MODE_SINGLE;
        app.force_text = protocol == 0;
        app.force_sixel = protocol == 1;
        app.force_iterm2 = protocol == 2;
        gchar *output = capture_output(enter_preview_capture, &app);
        g_assert_null(strstr(output, "\033_G"));
        g_assert_nonnull(strstr(output, "\033[2J"));
        g_free(output);
        cleanup_preview_app(&app);
    }
}

typedef struct {
    guint writes;
    guint fail_at;
    GString *delivered;
} PreviewOutput;

static ssize_t preview_output_write(void *data, const char *bytes, size_t length) {
    PreviewOutput *sink = data;
    if (++sink->writes == sink->fail_at) {
        errno = EIO;
        return -1;
    }
    if (sink->delivered) g_string_append_len(sink->delivered, bytes, length);
    return (ssize_t)length;
}

static void test_enter_preview_output_failure_preserves_mode(gconstpointer data) {
    PixelTermApp app;
    init_preview_app(&app, 2, 20, 80, 30);
    app.mode = APP_MODE_SINGLE;
    app.force_kitty = TRUE;
    app.current_index = 1;
    app.info_visible = TRUE;
    app.help_visible = TRUE;
    GList *original_images = app.image_files;
    PreviewOutput sink = {.fail_at = GPOINTER_TO_UINT(data), .delivered = g_string_new(NULL)};
    FILE *stream = test_output_stream(&sink, preview_output_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    ErrorCode result = app_enter_preview(&app);
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_true(recovered);
    g_assert_cmpstr(sink.delivered->str, ==, GPOINTER_TO_UINT(data) == 1 ?
        "\033\\" : "\033_Ga=d,q=2\033\\\033\\");
    g_assert_true(app.needs_redraw);
    g_assert_true(app.needs_screen_clear);
    g_string_free(sink.delivered, TRUE);
    g_assert_cmpint(app.mode, ==, APP_MODE_SINGLE);
    g_assert_true(app.image_files == original_images);
    g_assert_cmpint(app.current_index, ==, 1);
    g_assert_true(app.info_visible);
    g_assert_true(app.help_visible);
    g_assert_cmpint(g_preview_grid_stub_state.prepare_calls, ==, 1);
    g_assert_cmpint(g_preview_grid_stub_state.transition_calls, ==, 0);
    cleanup_preview_app(&app);
}

static void test_enter_preview_stop_failure_keeps_display(void) {
    PixelTermApp app;
    init_preview_app(&app, 2, 20, 80, 30);
    app.mode = APP_MODE_SINGLE;
    app.force_kitty = TRUE;
    GList *images = app.image_files;
    g_preview_grid_stub_state.prepare_error = ERROR_OUTPUT_IO;
    PreviewOutput sink = {0};
    FILE *stream = test_output_stream(&sink, preview_output_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    ErrorCode result = app_enter_preview(&app);
    g_assert_true(terminal_output_flush());
    fclose(stream);
    stdout = saved;
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_cmpuint(sink.writes, ==, 0);
    g_assert_cmpint(app.mode, ==, APP_MODE_SINGLE);
    g_assert_true(app.image_files == images);
    g_assert_cmpint(g_preview_grid_stub_state.prepare_calls, ==, 1);
    g_assert_cmpint(g_preview_grid_stub_state.transition_calls, ==, 0);
    cleanup_preview_app(&app);
}

static void test_enter_preview_transition_failure_retains_list(void) {
    PixelTermApp app;
    init_preview_app(&app, 2, 20, 80, 30);
    app.mode = APP_MODE_SINGLE;
    GList *images = app.image_files;
    g_preview_grid_stub_state.transition_error = ERROR_OUTPUT_IO;
    FILE *capture = tmpfile();
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    ErrorCode result = app_enter_preview(&app);
    stdout = saved;
    fclose(capture);
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(g_preview_grid_stub_state.transition_calls, ==, 1);
    g_assert_true(app.image_files == images);
    g_assert_cmpint(app.mode, ==, APP_MODE_SINGLE);
    g_assert_true(app.needs_redraw);
    g_assert_true(app.needs_screen_clear);
    cleanup_preview_app(&app);
}

static void test_preview_transaction(gconstpointer data) {
    gint scenario = GPOINTER_TO_INT(data);
    PixelTermApp app;
    init_preview_app(&app, 3, 20, 80, 30);
    g_free(app.image_files->data);
    app.image_files->data = g_strdup("invalid");
    app.mode = APP_MODE_SINGLE;
    app.current_index = 2;
    app.preview.selected = 2;
    app.preview.scroll = 3;
    app.preview.selected_link = g_list_last(app.image_files);
    app.preview.selected_link_index = 2;
    app.info_visible = app.help_visible = TRUE;
    app.return_to_mode = RETURN_MODE_PREVIEW_VIRTUAL;
    app.preload_enabled = TRUE;
    app.preloader = (ImagePreloader *)0x1;
    GList *images = app.image_files;
    PreviewState preview = app.preview;
    g_preview_grid_stub_state.renderer_error = scenario == 1 ? ERROR_MEMORY_ALLOC : ERROR_NONE;
    PreviewOutput sink = {.fail_at = scenario == 2 ? 2 : 0};
    FILE *stream = test_output_stream(&sink, preview_output_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    ErrorCode result = app_enter_preview_and_render(&app);
    g_assert_true(terminal_output_flush());
    fclose(stream);
    stdout = saved;
    g_assert_cmpint(result, ==, scenario == 1 ? ERROR_MEMORY_ALLOC : scenario == 2 ? ERROR_OUTPUT_IO : ERROR_NONE);
    g_assert_true(app.preloader == (ImagePreloader *)0x1);
    g_assert_true(app.preload_enabled);
    g_assert_cmpint(g_preview_grid_stub_state.preload_mutations, ==, scenario == 0 ? 1 : 0);
    if (scenario) {
        g_assert_true(app.image_files == images);
        g_assert_cmpint(app.total_images, ==, 3);
        g_assert_cmpint(app.current_index, ==, 2);
        g_assert_cmpmem(&app.preview, sizeof(app.preview), &preview, sizeof(preview));
        g_assert_cmpint(app.mode, ==, APP_MODE_SINGLE);
        g_assert_cmpint(app.return_to_mode, ==, RETURN_MODE_PREVIEW_VIRTUAL);
        g_assert_true(app.info_visible);
        g_assert_true(app.help_visible);
        g_assert_true(app.needs_redraw);
        g_assert_true(app.needs_screen_clear);
    } else {
        g_assert_cmpint(app.mode, ==, APP_MODE_PREVIEW);
        g_assert_cmpint(app.total_images, ==, 2);
        g_assert_cmpint(app.current_index, ==, 1);
        g_assert_cmpint(app.return_to_mode, ==, RETURN_MODE_PREVIEW);
        g_assert_cmpstr(app.preview.selected_link->data, ==, "img-2");
    }
    cleanup_preview_app(&app);
}

static void test_enter_preview_with_previous_error(void) {
    PixelTermApp app;
    init_preview_app(&app, 2, 20, 80, 30);
    app.mode = APP_MODE_SINGLE;
    app.last_error = ERROR_OUTPUT_IO;
    gchar *output = capture_output(enter_preview_capture, &app);
    g_assert_cmpint(app.mode, ==, APP_MODE_PREVIEW);
    g_assert_cmpint(app.last_error, ==, ERROR_OUTPUT_IO);
    g_free(output);
    cleanup_preview_app(&app);
}

static void test_full_preview_output_failure_is_reported(void) {
    PixelTermApp app;
    init_preview_app(&app, 2, 20, 80, 30);
    app.needs_screen_clear = TRUE;
    PreviewOutput sink = {.fail_at = 1, .delivered = g_string_new(NULL)};
    FILE *stream = test_output_stream(&sink, preview_output_write);
    g_assert_nonnull(stream);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    ErrorCode result = app_render_preview_grid(&app);
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_true(app.needs_screen_clear);
    g_assert_true(recovered);
    g_assert_cmpstr(sink.delivered->str, ==, "\033\\");
    g_string_free(sink.delivered, TRUE);
    cleanup_preview_app(&app);
}

static void test_preview_backoff_blocks_ui(gconstpointer data) {
    PixelTermApp app;
    init_preview_app(&app, 2, 20, 80, 30);
    app.ui_text_hidden = FALSE;
    PreviewOutput sink = {.fail_at = 1, .delivered = g_string_new(NULL)};
    FILE *stream = test_output_stream(&sink, preview_output_write);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    terminal_output_defer("pending-preview", TERMINAL_OUTPUT_RESTORE);
    g_assert_false(terminal_output_flush());
    guint writes = sink.writes;
    ErrorCode result = GPOINTER_TO_INT(data) ? app_render_preview_selection_change(&app, 1)
                                           : app_render_preview_grid(&app);
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_cmpuint(sink.writes, ==, writes);
    g_assert_true(app.needs_redraw);
    g_assert_true(app.needs_screen_clear);
    g_assert_true(terminal_output_flush());
    fclose(stream);
    stdout = saved;
    g_assert_cmpstr(sink.delivered->str, ==, "\033\\pending-preview");
    g_string_free(sink.delivered, TRUE);
    cleanup_preview_app(&app);
}

static void test_move_selection_normalizes_selection_and_scroll_before_moving(void) {
    PixelTermApp app;

    init_preview_app(&app, 7, 40, 80, 50);
    app.preview.selected = 99;
    app.preview.scroll = 99;

    g_assert_cmpint(app_preview_move_selection(&app, -1, 0), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 4);
    g_assert_cmpint(app.preview.scroll, ==, 2);
    g_assert_cmpint(app.preview.selected_link_index, ==, 4);
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, "img-4");

    cleanup_preview_app(&app);
}

static void test_change_zoom_initializes_default_zoom_without_refresh(void) {
    PixelTermApp app;

    init_preview_app(&app, 12, 0, 80, 30);
    app.preview.selected = 0;

    g_assert_cmpint(app_preview_change_zoom(&app, 0), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.zoom, ==, 20);
    g_assert_cmpint(g_preview_grid_stub_state.create_grid_renderer_calls, ==, 0);

    cleanup_preview_app(&app);
}

static void test_change_zoom_respects_min_and_max_column_bounds(void) {
    PixelTermApp app;

    init_preview_app(&app, 12, 40, 80, 30);
    app.preview.selected = 0;

    g_assert_cmpint(app_preview_change_zoom(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.zoom, ==, 40);
    g_assert_cmpint(g_preview_grid_stub_state.create_grid_renderer_calls, ==, 0);

    app.preview.zoom = 6;
    g_assert_cmpint(app_preview_change_zoom(&app, -1), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.zoom, ==, 6);
    g_assert_cmpint(g_preview_grid_stub_state.create_grid_renderer_calls, ==, 0);

    cleanup_preview_app(&app);
}

static void test_change_zoom_normalizes_invalid_selection_and_scroll_before_refresh(void) {
    PixelTermApp app;

    init_preview_app(&app, 7, 20, 80, 30);
    app.preview.selected = 99;
    app.preview.scroll = 99;

    g_assert_cmpint(app_preview_change_zoom(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(g_preview_grid_stub_state.create_grid_renderer_calls, ==, 1);
    g_assert_cmpint(app.preview.selected, ==, 6);
    g_assert_cmpint(app.preview.scroll, ==, 2);
    g_assert_cmpint(app.preview.selected_link_index, ==, 6);
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, "img-6");

    cleanup_preview_app(&app);
}

static void test_page_move_preserves_relative_row_and_column(void) {
    PixelTermApp app;

    init_preview_app(&app, 20, 20, 80, 30);
    app.preview.selected = 5;

    g_assert_cmpint(app_preview_page_move(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 13);
    g_assert_cmpint(app.preview.scroll, ==, 2);
    g_assert_cmpint(app.preview.selected_link_index, ==, 13);
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, "img-13");

    cleanup_preview_app(&app);
}

static void test_page_move_round_trip_restores_selection_cache_state(void) {
    PixelTermApp app;
    gchar *initial_path = NULL;

    init_preview_app(&app, 20, 20, 80, 30);
    app.preview.selected = 5;
    initial_path = g_strdup(app_preview_get_selected_filepath(&app));

    g_assert_cmpint(app_preview_page_move(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app_preview_page_move(&app, -1), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 5);
    g_assert_cmpint(app.preview.scroll, ==, 0);
    g_assert_cmpint(app.preview.selected_link_index, ==, 5);
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, initial_path);

    g_free(initial_path);
    cleanup_preview_app(&app);
}

static void test_move_selection_advances_to_non_overlapping_last_page(void) {
    PixelTermApp app;

    init_preview_app(&app, 10, 40, 80, 63);
    app.preview.selected = 5;

    g_assert_cmpint(app_preview_move_selection(&app, 1, 0), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 7);
    g_assert_cmpint(app.preview.scroll, ==, 3);
    g_assert_cmpint(app.preview.selected_link_index, ==, 7);
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, "img-7");

    cleanup_preview_app(&app);
}

static void test_page_move_keeps_last_page_non_overlapping(void) {
    PixelTermApp app;

    init_preview_app(&app, 10, 40, 80, 63);
    app.preview.selected = 5;

    g_assert_cmpint(app_preview_page_move(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 9);
    g_assert_cmpint(app.preview.scroll, ==, 3);
    g_assert_cmpint(app.preview.selected_link_index, ==, 9);
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, "img-9");

    cleanup_preview_app(&app);
}

static void test_page_move_round_trip_restores_selection_on_short_last_page(void) {
    PixelTermApp app;
    gchar *initial_path = NULL;

    init_preview_app(&app, 10, 40, 80, 63);
    app.preview.selected = 5;
    initial_path = g_strdup(app_preview_get_selected_filepath(&app));

    g_assert_cmpint(app_preview_page_move(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 9);
    g_assert_cmpint(app.preview.scroll, ==, 3);

    g_assert_cmpint(app_preview_page_move(&app, -1), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 5);
    g_assert_cmpint(app.preview.scroll, ==, 0);
    g_assert_cmpint(app.preview.selected_link_index, ==, 5);
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, initial_path);

    g_free(initial_path);
    cleanup_preview_app(&app);
}

static void test_page_move_round_trip_restores_middle_row_on_short_last_page(void) {
    PixelTermApp app;
    gchar *initial_path = NULL;

    init_preview_app(&app, 10, 40, 80, 63);
    app.preview.selected = 3;
    initial_path = g_strdup(app_preview_get_selected_filepath(&app));

    g_assert_cmpint(app_preview_page_move(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 7);
    g_assert_cmpint(app.preview.scroll, ==, 3);
    g_assert_cmpint(app.preview.selected_link_index, ==, 7);
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, "img-7");

    g_assert_cmpint(app_preview_page_move(&app, -1), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 3);
    g_assert_cmpint(app.preview.scroll, ==, 0);
    g_assert_cmpint(app.preview.selected_link_index, ==, 3);
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, initial_path);

    g_free(initial_path);
    cleanup_preview_app(&app);
}

static void test_page_move_clamps_top_slot_on_short_last_page_after_render(void) {
    PixelTermApp app;

    init_preview_app(&app, 10, 40, 80, 63);
    app.preview.selected = 1;

    g_assert_cmpint(app_preview_page_move(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 7);
    g_assert_cmpint(app.preview.scroll, ==, 3);
    g_assert_cmpint(app.preview.selected_link_index, ==, 7);
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, "img-7");

    g_assert_cmpint(app_render_preview_grid(&app), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 7);
    g_assert_cmpint(app.preview.scroll, ==, 3);
    g_assert_cmpint(app.preview.selected_link_index, ==, 7);
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, "img-7");

    cleanup_preview_app(&app);
}

static void test_page_move_round_trip_uses_clamped_column_on_partial_final_row(void) {
    PixelTermApp app;
    gchar *clamped_path = NULL;

    init_preview_app(&app, 15, 20, 80, 30);
    app.preview.selected = 7;

    g_assert_cmpint(app_preview_page_move(&app, 1), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 14);
    g_assert_cmpint(app.preview.scroll, ==, 2);
    g_assert_cmpint(app.preview.selected_link_index, ==, 14);
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, "img-14");
    clamped_path = g_strdup(app_preview_get_selected_filepath(&app));

    g_assert_cmpint(app_preview_page_move(&app, -1), ==, ERROR_NONE);
    g_assert_cmpint(app.preview.selected, ==, 6);
    g_assert_cmpint(app.preview.scroll, ==, 0);
    g_assert_cmpint(app.preview.selected_link_index, ==, 6);
    g_assert_nonnull(clamped_path);
    g_assert_cmpstr(clamped_path, ==, "img-14");
    g_assert_cmpstr(app_preview_get_selected_filepath(&app), ==, "img-6");

    g_free(clamped_path);
    cleanup_preview_app(&app);
}

static void test_render_preview_grid_preserves_visible_layout_contract(void) {
    PixelTermApp app;

    init_preview_app(&app, 6, 40, 80, 30);
    app.ui_text_hidden = FALSE;
    app.preview.selected = 4;
    app.preview.scroll = 2;

    gchar *output = capture_output(render_preview_grid_capture, &app);

    g_assert_nonnull(g_strstr_len(output, -1, "\033[1;1H\033[2K"));
    g_assert_nonnull(g_strstr_len(output, -1, "Preview Grid"));
    g_assert_nonnull(g_strstr_len(output, -1, "\033[3;1H\033[2K"));
    g_assert_nonnull(g_strstr_len(output, -1, "3/3"));
    g_assert_nonnull(g_strstr_len(output, -1, "\033[28;1H"));
    g_assert_nonnull(g_strstr_len(output, -1, "\033[34mimg-4\033[0m"));
    g_assert_nonnull(g_strstr_len(output, -1, "\033[30;1H\033[2K"));
    g_assert_nonnull(g_strstr_len(output, -1, "Enter"));
    g_assert_nonnull(g_strstr_len(output, -1, "Open"));

    g_free(output);
    cleanup_preview_app(&app);
}

ImageRenderer* app_create_grid_renderer(const PixelTermApp *app,
                                        gint content_width,
                                        gint content_height,
                                        ErrorCode *out_error) {
    (void)app;
    (void)content_width;
    (void)content_height;
    g_preview_grid_stub_state.create_grid_renderer_calls++;
    if (g_preview_grid_stub_state.renderer_error != ERROR_NONE) {
        if (out_error) *out_error = g_preview_grid_stub_state.renderer_error;
        return NULL;
    }
    if (out_error) {
        *out_error = ERROR_NONE;
    }
    return g_new0(ImageRenderer, 1);
}

gboolean app_has_images(const PixelTermApp *app) {
    return app && app->total_images > 0;
}

void app_preloader_clear_queue(PixelTermApp *app) {
    if (app->preloader && app->preload_enabled) g_preview_grid_stub_state.preload_mutations++;
}

ErrorCode preloader_clear_queue(ImagePreloader *preloader) {
    (void)preloader;
    g_preview_grid_stub_state.preload_mutations++;
    return ERROR_NONE;
}

void app_preloader_queue_directory(PixelTermApp *app) {
    (void)app;
}

gint app_preview_bottom_reserved_lines(const PixelTermApp *app) {
    (void)app;
    return 0;
}

void app_preview_clear_cell_border(const PixelTermApp *app,
                                   const PreviewLayout *layout,
                                   gint index,
                                   gint start_row,
                                   gint vertical_offset) {
    (void)app;
    (void)layout;
    (void)index;
    (void)start_row;
    (void)vertical_offset;
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

void app_preview_draw_cell_border(const PixelTermApp *app,
                                  const PreviewLayout *layout,
                                  gint index,
                                  gint start_row,
                                  gint vertical_offset) {
    (void)app;
    (void)layout;
    (void)index;
    (void)start_row;
    (void)vertical_offset;
}

void app_preview_render_cells(const GridRenderContext *context,
                              PixelTermApp *app,
                              ImageRenderer *renderer,
                              GList *cursor) {
    (void)context;
    (void)app;
    (void)renderer;
    (void)cursor;
}

void app_preview_render_selected_filename(PixelTermApp *app) {
    const gchar *filepath = app_preview_get_selected_filepath(app);
    if (!app || app->ui_text_hidden || !filepath) {
        return;
    }

    gchar *basename = g_path_get_basename(filepath);
    ui_render_centered_row(app->term_height - 2, app->term_width, basename, "\033[34m");
    g_free(basename);
}

ErrorCode app_transition_mode(PixelTermApp *app, AppMode mode) {
    g_preview_grid_stub_state.transition_calls++;
    if (g_preview_grid_stub_state.transition_error != ERROR_NONE)
        return g_preview_grid_stub_state.transition_error;
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    app->mode = mode;
    app->info_visible = FALSE;
    app->help_visible = FALSE;
    return ERROR_NONE;
}

ErrorCode app_prepare_mode_entry(PixelTermApp *app, gboolean clear_preloader_queue) {
    g_preview_grid_stub_state.prepare_calls++;
    if (g_preview_grid_stub_state.prepare_error != ERROR_NONE)
        app->last_error = g_preview_grid_stub_state.prepare_error;
    if (clear_preloader_queue) app_preloader_clear_queue(app);
    return g_preview_grid_stub_state.prepare_error;
}

void get_terminal_size(gint *width, gint *height) {
    if (width) {
        *width = g_preview_grid_stub_state.term_width;
    }
    if (height) {
        *height = g_preview_grid_stub_state.term_height;
    }
}

ErrorCode gif_player_stop(GifPlayer *player) {
    (void)player;
    return ERROR_NONE;
}

gboolean is_valid_media_file(const char *filepath) {
    return g_strcmp0(filepath, "invalid") != 0;
}

ErrorCode preloader_add_task(ImagePreloader *preloader,
                             const char *filepath,
                             gint priority,
                             gint target_width,
                             gint target_height) {
    (void)preloader;
    (void)filepath;
    (void)priority;
    (void)target_width;
    (void)target_height;
    g_preview_grid_stub_state.preload_mutations++;
    return ERROR_NONE;
}

void renderer_destroy(ImageRenderer *renderer) {
    g_free(renderer);
}

ErrorCode video_player_stop(VideoPlayer *player) {
    (void)player;
    return ERROR_NONE;
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/app_preview_grid/transaction/success", GINT_TO_POINTER(0), test_preview_transaction);
    g_test_add_data_func("/app_preview_grid/transaction/renderer_failure", GINT_TO_POINTER(1), test_preview_transaction);
    g_test_add_data_func("/app_preview_grid/transaction/final_flush_failure", GINT_TO_POINTER(2), test_preview_transaction);
    g_test_add_data_func("/app_preview_grid/output/full_backoff", GINT_TO_POINTER(0), test_preview_backoff_blocks_ui);
    g_test_add_data_func("/app_preview_grid/output/selection_backoff", GINT_TO_POINTER(1), test_preview_backoff_blocks_ui);
    g_test_add_func("/app_preview_grid/entry/previous_error", test_enter_preview_with_previous_error);
    g_test_add_func("/app_preview_grid/entry/stop_failure_keeps_display", test_enter_preview_stop_failure_keeps_display);
    g_test_add_func("/app_preview_grid/entry/transition_failure_retains_list", test_enter_preview_transition_failure_retains_list);
    g_test_add_func("/app_preview_grid/render/full_output_failure", test_full_preview_output_failure_is_reported);
    g_test_add_data_func("/app_preview_grid/entry/clear_failure_preserves_mode", GUINT_TO_POINTER(1), test_enter_preview_output_failure_preserves_mode);
    g_test_add_data_func("/app_preview_grid/entry/screen_failure_preserves_mode", GUINT_TO_POINTER(2), test_enter_preview_output_failure_preserves_mode);
    g_test_add_func("/app_preview_grid/entry/clears_kitty_before_screen",
                    test_enter_preview_clears_kitty_before_screen);
    g_test_add_func("/app_preview_grid/entry/other_protocols_skip_kitty_delete",
                    test_enter_preview_other_protocols_skip_kitty_delete);

    g_test_add_func("/app_preview_grid/move_selection/normalizes_selection_and_scroll_before_moving",
                    test_move_selection_normalizes_selection_and_scroll_before_moving);
    g_test_add_func("/app_preview_grid/change_zoom/initializes_default_zoom_without_refresh",
                    test_change_zoom_initializes_default_zoom_without_refresh);
    g_test_add_func("/app_preview_grid/change_zoom/respects_min_and_max_column_bounds",
                    test_change_zoom_respects_min_and_max_column_bounds);
    g_test_add_func("/app_preview_grid/change_zoom/normalizes_invalid_selection_and_scroll_before_refresh",
                    test_change_zoom_normalizes_invalid_selection_and_scroll_before_refresh);
    g_test_add_func("/app_preview_grid/page_move/preserves_relative_row_and_column",
                    test_page_move_preserves_relative_row_and_column);
    g_test_add_func("/app_preview_grid/page_move/round_trip_restores_selection_cache_state",
                    test_page_move_round_trip_restores_selection_cache_state);
    g_test_add_func("/app_preview_grid/move_selection/advances_to_non_overlapping_last_page",
                    test_move_selection_advances_to_non_overlapping_last_page);
    g_test_add_func("/app_preview_grid/page_move/keeps_last_page_non_overlapping",
                    test_page_move_keeps_last_page_non_overlapping);
    g_test_add_func("/app_preview_grid/page_move/round_trip_restores_selection_on_short_last_page",
                    test_page_move_round_trip_restores_selection_on_short_last_page);
    g_test_add_func("/app_preview_grid/page_move/round_trip_restores_middle_row_on_short_last_page",
                    test_page_move_round_trip_restores_middle_row_on_short_last_page);
    g_test_add_func("/app_preview_grid/page_move/clamps_top_slot_on_short_last_page_after_render",
                    test_page_move_clamps_top_slot_on_short_last_page_after_render);
    g_test_add_func("/app_preview_grid/page_move/round_trip_uses_clamped_column_on_partial_final_row",
                    test_page_move_round_trip_uses_clamped_column_on_partial_final_row);
    g_test_add_func("/app_preview_grid/render_preview_grid/preserves_visible_layout_contract",
                    test_render_preview_grid_preserves_visible_layout_contract);

    return g_test_run();
}
