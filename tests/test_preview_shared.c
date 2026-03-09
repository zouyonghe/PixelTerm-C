#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app_preview_shared_internal.h"

static gchar *capture_draw_output(gint content_x,
                                  gint content_y,
                                  gint content_width,
                                  gint content_height,
                                  const gchar *text) {
    GString *rendered = g_string_new(text ? text : "");
    gchar *template = g_strdup_printf("%s/pixelterm-preview-shared-XXXXXX", g_get_tmp_dir());
    int fd = g_mkstemp(template);
    g_assert_cmpint(fd, >=, 0);

    int saved_stdout = dup(STDOUT_FILENO);
    g_assert_cmpint(saved_stdout, >=, 0);

    fflush(stdout);
    g_assert_cmpint(dup2(fd, STDOUT_FILENO), >=, 0);
    close(fd);

    app_draw_rendered_lines(content_x,
                            content_y,
                            content_width,
                            content_height,
                            rendered);

    fflush(stdout);
    g_assert_cmpint(dup2(saved_stdout, STDOUT_FILENO), >=, 0);
    close(saved_stdout);

    g_string_free(rendered, TRUE);

    gchar *output = NULL;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(template, &output, NULL, &error));
    g_assert_no_error(error);
    g_remove(template);
    g_free(template);
    return output;
}

static gchar *capture_graphics_output(gint content_x,
                                      gint content_y,
                                      gint content_width,
                                      gint content_height,
                                      gint rendered_width,
                                      gint rendered_height,
                                      const gchar *payload) {
    GString *rendered = g_string_new(payload ? payload : "");
    gchar *template = g_strdup_printf("%s/pixelterm-preview-shared-XXXXXX", g_get_tmp_dir());
    int fd = g_mkstemp(template);
    g_assert_cmpint(fd, >=, 0);

    int saved_stdout = dup(STDOUT_FILENO);
    g_assert_cmpint(saved_stdout, >=, 0);

    fflush(stdout);
    g_assert_cmpint(dup2(fd, STDOUT_FILENO), >=, 0);
    close(fd);

    app_draw_rendered_graphics(content_x,
                               content_y,
                               content_width,
                               content_height,
                               rendered_width,
                               rendered_height,
                               rendered);

    fflush(stdout);
    g_assert_cmpint(dup2(saved_stdout, STDOUT_FILENO), >=, 0);
    close(saved_stdout);

    g_string_free(rendered, TRUE);

    gchar *output = NULL;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(template, &output, NULL, &error));
    g_assert_no_error(error);
    g_remove(template);
    g_free(template);
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

static void test_draw_preview_content_uses_text_path_for_symbol_mode(void) {
    GString *rendered = g_string_new("AA\nBB");
    gchar *template = g_strdup_printf("%s/pixelterm-preview-shared-XXXXXX", g_get_tmp_dir());
    int fd = g_mkstemp(template);
    g_assert_cmpint(fd, >=, 0);

    int saved_stdout = dup(STDOUT_FILENO);
    g_assert_cmpint(saved_stdout, >=, 0);

    fflush(stdout);
    g_assert_cmpint(dup2(fd, STDOUT_FILENO), >=, 0);
    close(fd);

    app_draw_preview_content(11, 5, 6, 6, 2, 2, FALSE, rendered);

    fflush(stdout);
    g_assert_cmpint(dup2(saved_stdout, STDOUT_FILENO), >=, 0);
    close(saved_stdout);

    g_string_free(rendered, TRUE);

    gchar *output = NULL;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(template, &output, NULL, &error));
    g_assert_no_error(error);
    g_remove(template);
    g_free(template);

    g_assert_nonnull(g_strstr_len(output, -1, "\033[7;13HAA"));
    g_assert_nonnull(g_strstr_len(output, -1, "\033[8;13HBB"));
    g_free(output);
}

static void test_draw_preview_content_uses_graphics_path_for_graphics_mode(void) {
    GString *rendered = g_string_new("\033_Gf=100;payload\033\\");
    gchar *template = g_strdup_printf("%s/pixelterm-preview-shared-XXXXXX", g_get_tmp_dir());
    int fd = g_mkstemp(template);
    g_assert_cmpint(fd, >=, 0);

    int saved_stdout = dup(STDOUT_FILENO);
    g_assert_cmpint(saved_stdout, >=, 0);

    fflush(stdout);
    g_assert_cmpint(dup2(fd, STDOUT_FILENO), >=, 0);
    close(fd);

    app_draw_preview_content(10, 4, 20, 10, 8, 3, TRUE, rendered);

    fflush(stdout);
    g_assert_cmpint(dup2(saved_stdout, STDOUT_FILENO), >=, 0);
    close(saved_stdout);

    g_string_free(rendered, TRUE);

    gchar *output = NULL;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(template, &output, NULL, &error));
    g_assert_no_error(error);
    g_remove(template);
    g_free(template);

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
    g_test_add_func("/preview_shared/draw_preview_content/uses_text_path_for_symbol_mode",
                    test_draw_preview_content_uses_text_path_for_symbol_mode);
    g_test_add_func("/preview_shared/draw_preview_content/uses_graphics_path_for_graphics_mode",
                    test_draw_preview_content_uses_graphics_path_for_graphics_mode);
}
