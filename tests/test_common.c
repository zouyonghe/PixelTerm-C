#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "common.h"

void register_browser_tests(void);
void register_gif_player_tests(void);
void register_renderer_tests(void);

static void remove_path(gpointer data) {
    if (!data) {
        return;
    }
    g_remove((const gchar *)data);
    g_free(data);
}

static gchar *write_temp_file(const gchar *suffix, const guint8 *data, gsize len) {
    gchar *template = g_strdup_printf("%s/pixelterm-test-XXXXXX", g_get_tmp_dir());
    int fd = g_mkstemp(template);
    if (fd < 0) {
        g_error("Failed to create temp file");
    }
    close(fd);

    gchar *path = template;
    if (suffix && suffix[0] != '\0') {
        gchar *with_suffix = g_strconcat(template, suffix, NULL);
        if (g_rename(template, with_suffix) == 0) {
            g_free(template);
            path = with_suffix;
        } else {
            g_free(with_suffix);
        }
    }

    const gchar *contents = (len == 0 && data == NULL) ? "" : (const gchar *)data;
    GError *error = NULL;
    gboolean ok = g_file_set_contents(path, contents, (gssize)len, &error);
    if (!ok) {
        g_error("Failed to write temp file: %s", error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
    }

    g_test_queue_destroy(remove_path, path);
    return path;
}

static void test_get_file_extension(void) {
    g_assert_null(get_file_extension(NULL));
    g_assert_null(get_file_extension("noext"));
    g_assert_null(get_file_extension(".hidden"));
    g_assert_cmpstr(get_file_extension("photo.jpg"), ==, ".jpg");
    g_assert_cmpstr(get_file_extension("archive.tar.gz"), ==, ".gz");
}

static void test_is_image_by_content_signatures(void) {
    static const guint8 k_jpeg[] = {0xFF, 0xD8, 0xFF, 0x00};
    static const guint8 k_png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    static const guint8 k_gif[] = {'G', 'I', 'F', '8', '9', 'a'};
    static const guint8 k_webp[] = {'R', 'I', 'F', 'F', 0x00, 0x00, 0x00, 0x00, 'W', 'E', 'B', 'P'};
    static const guint8 k_bmp[] = {'B', 'M', 0x00, 0x00};
    static const guint8 k_tiff_le[] = {'I', 'I', '*', '\0'};
    static const guint8 k_tiff_be[] = {'M', 'M', '\0', '*'};

    gchar *jpeg_path = write_temp_file("", k_jpeg, sizeof(k_jpeg));
    gchar *png_path = write_temp_file("", k_png, sizeof(k_png));
    gchar *gif_path = write_temp_file("", k_gif, sizeof(k_gif));
    gchar *webp_path = write_temp_file("", k_webp, sizeof(k_webp));
    gchar *bmp_path = write_temp_file("", k_bmp, sizeof(k_bmp));
    gchar *tiff_le_path = write_temp_file("", k_tiff_le, sizeof(k_tiff_le));
    gchar *tiff_be_path = write_temp_file("", k_tiff_be, sizeof(k_tiff_be));

    g_assert_true(is_image_by_content(jpeg_path));
    g_assert_true(is_image_by_content(png_path));
    g_assert_true(is_image_by_content(gif_path));
    g_assert_true(is_image_by_content(webp_path));
    g_assert_true(is_image_by_content(bmp_path));
    g_assert_true(is_image_by_content(tiff_le_path));
    g_assert_true(is_image_by_content(tiff_be_path));
}

static void test_is_image_by_content_invalid(void) {
    static const guint8 k_invalid[] = {0x00, 0x01, 0x02, 0x03};
    gchar *path = write_temp_file("", k_invalid, sizeof(k_invalid));
    g_assert_false(is_image_by_content(path));
}

static void test_is_image_file_extension_and_content(void) {
    static const guint8 k_jpeg[] = {0xFF, 0xD8, 0xFF, 0x00};
    static const guint8 k_invalid[] = {0x00, 0x01, 0x02, 0x03};

    g_assert_true(is_image_file("photo.JPG"));
    g_assert_true(is_image_file("image.png"));
    g_assert_false(is_image_file("document.txt"));

    gchar *no_ext_path = write_temp_file("", k_jpeg, sizeof(k_jpeg));
    g_assert_true(is_image_file(no_ext_path));

    gchar *invalid_no_ext = write_temp_file("", k_invalid, sizeof(k_invalid));
    g_assert_false(is_image_file(invalid_no_ext));
}

static void test_is_valid_image_file(void) {
    static const guint8 k_png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    static const guint8 k_invalid[] = {0x00, 0x01, 0x02, 0x03};

    gchar *valid_png = write_temp_file(".png", k_png, sizeof(k_png));
    gchar *invalid_png = write_temp_file(".png", k_invalid, sizeof(k_invalid));
    gchar *empty_png = write_temp_file(".png", NULL, 0);

    g_assert_true(is_valid_image_file(valid_png));
    g_assert_false(is_valid_image_file(invalid_png));
    g_assert_false(is_valid_image_file(empty_png));
    g_assert_false(is_valid_image_file("/path/does/not/exist.png"));
}

static void test_file_helpers(void) {
    static const guint8 k_data[] = {'a', 'b', 'c'};
    gchar *path = write_temp_file("", k_data, sizeof(k_data));

    g_assert_true(file_exists(path));
    g_assert_cmpint(get_file_size(path), ==, 3);
    g_assert_cmpint(get_file_mtime(path), >=, 0);

    g_assert_false(file_exists("/path/does/not/exist.txt"));
    g_assert_cmpint(get_file_size("/path/does/not/exist.txt"), ==, -1);
    g_assert_cmpint(get_file_mtime("/path/does/not/exist.txt"), ==, -1);
}

static void test_cleanup_helpers(void) {
    gchar *text = g_strdup("cleanup");
    cleanup_string(&text);
    g_assert_null(text);

    GString *value = g_string_new("cleanup");
    cleanup_gstring(&value);
    g_assert_null(value);
}

static void test_error_code_to_string(void) {
    g_assert_cmpstr(error_code_to_string(ERROR_NONE), ==, "No error");
    g_assert_cmpstr(error_code_to_string(ERROR_INVALID_ARGS), ==, "Invalid arguments");
    g_assert_cmpstr(error_code_to_string((ErrorCode)999), ==, "Unknown error");
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/common/get_file_extension", test_get_file_extension);
    g_test_add_func("/common/is_image_by_content/signatures", test_is_image_by_content_signatures);
    g_test_add_func("/common/is_image_by_content/invalid", test_is_image_by_content_invalid);
    g_test_add_func("/common/is_image_file", test_is_image_file_extension_and_content);
    g_test_add_func("/common/is_valid_image_file", test_is_valid_image_file);
    g_test_add_func("/common/file_helpers", test_file_helpers);
    g_test_add_func("/common/cleanup_helpers", test_cleanup_helpers);
    g_test_add_func("/common/error_code_to_string", test_error_code_to_string);
    register_browser_tests();
    register_gif_player_tests();
    register_renderer_tests();

    return g_test_run();
}
