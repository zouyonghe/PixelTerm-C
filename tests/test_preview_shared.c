#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app_preview_shared_internal.h"

typedef void (*PreviewDrawFunc)(gpointer user_data);

typedef struct {
    gint content_x;
    gint content_y;
    gint content_width;
    gint content_height;
    gint rendered_width;
    gint rendered_height;
    gboolean graphics_mode;
    GString *rendered;
} PreviewCaptureArgs;

static gchar *capture_output(PreviewDrawFunc draw_func, gpointer user_data) {
    gchar *template = g_strdup_printf("%s/pixelterm-preview-shared-XXXXXX", g_get_tmp_dir());
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

static void draw_lines_capture(gpointer user_data) {
    PreviewCaptureArgs *args = (PreviewCaptureArgs *)user_data;
    app_draw_rendered_lines(args->content_x,
                            args->content_y,
                            args->content_width,
                            args->content_height,
                            args->rendered);
}

static void draw_graphics_capture(gpointer user_data) {
    PreviewCaptureArgs *args = (PreviewCaptureArgs *)user_data;
    app_draw_rendered_graphics(args->content_x,
                               args->content_y,
                               args->content_width,
                               args->content_height,
                               args->rendered_width,
                               args->rendered_height,
                               args->rendered);
}

static void draw_preview_content_capture(gpointer user_data) {
    PreviewCaptureArgs *args = (PreviewCaptureArgs *)user_data;
    app_draw_preview_content(args->content_x,
                             args->content_y,
                             args->content_width,
                             args->content_height,
                             args->rendered_width,
                             args->rendered_height,
                             args->graphics_mode,
                             args->rendered);
}

static gchar *capture_draw_output(gint content_x,
                                  gint content_y,
                                  gint content_width,
                                  gint content_height,
                                  const gchar *text) {
    PreviewCaptureArgs args = {
        .content_x = content_x,
        .content_y = content_y,
        .content_width = content_width,
        .content_height = content_height,
        .rendered = g_string_new(text ? text : "")
    };
    gchar *output = capture_output(draw_lines_capture, &args);
    g_string_free(args.rendered, TRUE);
    return output;
}

static gchar *capture_graphics_output(gint content_x,
                                      gint content_y,
                                      gint content_width,
                                      gint content_height,
                                      gint rendered_width,
                                      gint rendered_height,
                                      const gchar *payload) {
    PreviewCaptureArgs args = {
        .content_x = content_x,
        .content_y = content_y,
        .content_width = content_width,
        .content_height = content_height,
        .rendered_width = rendered_width,
        .rendered_height = rendered_height,
        .rendered = g_string_new(payload ? payload : "")
    };
    gchar *output = capture_output(draw_graphics_capture, &args);
    g_string_free(args.rendered, TRUE);
    return output;
}

static gchar *capture_preview_content_output(gint content_x,
                                             gint content_y,
                                             gint content_width,
                                             gint content_height,
                                             gint rendered_width,
                                             gint rendered_height,
                                             gboolean graphics_mode,
                                             const gchar *payload) {
    PreviewCaptureArgs args = {
        .content_x = content_x,
        .content_y = content_y,
        .content_width = content_width,
        .content_height = content_height,
        .rendered_width = rendered_width,
        .rendered_height = rendered_height,
        .graphics_mode = graphics_mode,
        .rendered = g_string_new(payload ? payload : "")
    };
    gchar *output = capture_output(draw_preview_content_capture, &args);
    g_string_free(args.rendered, TRUE);
    return output;
}

static void test_draw_rendered_lines_centers_both_axes(void) {
    gchar *output = capture_draw_output(11, 5, 6, 6, "AA\nBB");

    g_assert_nonnull(g_strstr_len(output, -1, "\033[7;13HAA"));
    g_assert_nonnull(g_strstr_len(output, -1, "\033[8;13HBB"));

    g_free(output);
}

static void test_draw_rendered_lines_trailing_newline_does_not_shift_center(void) {
    gchar *output = capture_draw_output(2, 4, 5, 6, "A\nB\n");

    g_assert_nonnull(g_strstr_len(output, -1, "\033[6;4HA"));
    g_assert_nonnull(g_strstr_len(output, -1, "\033[7;4HB"));
    g_assert_null(g_strstr_len(output, -1, "\033[5;4HA"));

    g_free(output);
}

static void test_draw_rendered_graphics_centers_payload_as_single_block(void) {
    gchar *output = capture_graphics_output(10,
                                            4,
                                            20,
                                            10,
                                            8,
                                            3,
                                            "\033_Gf=100;payload\033\\");

    g_assert_true(g_str_has_prefix(output, "\033[7;16H\033_Gf=100;payload\033\\"));
    g_assert_null(g_strstr_len(output, -1, "\033[8;"));

    g_free(output);
}

static void test_draw_rendered_graphics_resets_terminal_attributes(void) {
    gchar *output = capture_graphics_output(3,
                                            2,
                                            12,
                                            6,
                                            4,
                                            2,
                                            "\033[31mGRAPHIC");

    g_assert_true(g_str_has_suffix(output, "\033[0m"));

    g_free(output);
}

static void test_draw_preview_content_uses_text_path_for_symbol_mode(void) {
    gchar *output = capture_preview_content_output(11, 5, 6, 6, 2, 2, FALSE, "AA\nBB");

    g_assert_nonnull(g_strstr_len(output, -1, "\033[7;13HAA"));
    g_assert_nonnull(g_strstr_len(output, -1, "\033[8;13HBB"));
    g_free(output);
}

static void test_draw_preview_content_uses_graphics_path_for_graphics_mode(void) {
    gchar *output = capture_preview_content_output(10,
                                                   4,
                                                   20,
                                                   10,
                                                   8,
                                                   3,
                                                   TRUE,
                                                   "\033_Gf=100;payload\033\\");

    g_assert_true(g_str_has_prefix(output, "\033[7;16H\033_Gf=100;payload\033\\"));
    g_assert_null(g_strstr_len(output, -1, "\033[8;"));
    g_free(output);
}

void register_preview_shared_tests(void) {
    g_test_add_func("/preview_shared/draw_rendered_lines/centers_both_axes",
                    test_draw_rendered_lines_centers_both_axes);
    g_test_add_func("/preview_shared/draw_rendered_lines/trailing_newline_does_not_shift_center",
                    test_draw_rendered_lines_trailing_newline_does_not_shift_center);
    g_test_add_func("/preview_shared/draw_rendered_graphics/centers_payload_as_single_block",
                    test_draw_rendered_graphics_centers_payload_as_single_block);
    g_test_add_func("/preview_shared/draw_rendered_graphics/resets_terminal_attributes",
                    test_draw_rendered_graphics_resets_terminal_attributes);
    g_test_add_func("/preview_shared/draw_preview_content/uses_text_path_for_symbol_mode",
                    test_draw_preview_content_uses_text_path_for_symbol_mode);
    g_test_add_func("/preview_shared/draw_preview_content/uses_graphics_path_for_graphics_mode",
                    test_draw_preview_content_uses_graphics_path_for_graphics_mode);
}
