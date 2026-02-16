#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "common.h"

void register_browser_tests(void);
void register_gif_player_tests(void);
void register_renderer_tests(void);
void register_text_utils_tests(void);
void register_app_mode_tests(void);

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

static void test_is_video_file_and_media_file(void) {
    g_assert_true(is_video_file("clip.MP4"));
    g_assert_true(is_video_file("movie.mkv"));
    g_assert_false(is_video_file("photo.jpg"));
    g_assert_false(is_video_file("noext"));

    g_assert_true(is_media_file("photo.JPG"));
    g_assert_true(is_media_file("movie.mp4"));
    g_assert_false(is_media_file("note.txt"));
}

static void test_is_book_file_and_valid(void) {
    g_assert_true(is_book_file("book.pdf"));
    g_assert_true(is_book_file("novel.EPUB"));
    g_assert_true(is_book_file("comic.cbz"));
    g_assert_false(is_book_file("image.png"));

    static const guint8 k_dummy[] = {'%', 'P', 'D', 'F'};
    gchar *pdf_path = write_temp_file(".pdf", k_dummy, sizeof(k_dummy));
    g_assert_true(is_valid_book_file(pdf_path));

    gchar *empty_pdf = write_temp_file(".pdf", NULL, 0);
    g_assert_false(is_valid_book_file(empty_pdf));
}

static void test_is_valid_video_file_by_content(void) {
    static const guint8 k_mp4[] = {
        0x00, 0x00, 0x00, 0x00,
        'f', 't', 'y', 'p',
        'i', 's', 'o', 'm'
    };
    static const guint8 k_invalid[] = {0x00, 0x01, 0x02, 0x03};

    gchar *mp4_path = write_temp_file("", k_mp4, sizeof(k_mp4));
    gchar *invalid_path = write_temp_file("", k_invalid, sizeof(k_invalid));

    g_assert_true(is_valid_video_file(mp4_path));
    g_assert_false(is_valid_video_file(invalid_path));
}

static void test_is_animated_image_candidate(void) {
    static const guint8 k_png_anim[] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x00,
        'a', 'c', 'T', 'L',
        0x00, 0x00, 0x00, 0x00
    };
    static const guint8 k_png_static[] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x00,
        'I', 'D', 'A', 'T',
        0x00, 0x00, 0x00, 0x00
    };
    static const guint8 k_webp_anim[] = {
        'R', 'I', 'F', 'F',
        0x00, 0x00, 0x00, 0x00,
        'W', 'E', 'B', 'P',
        'A', 'N', 'I', 'M',
        0x00, 0x00, 0x00, 0x00
    };
    static const guint8 k_webp_static[] = {
        'R', 'I', 'F', 'F',
        0x00, 0x00, 0x00, 0x00,
        'W', 'E', 'B', 'P',
        'V', 'P', '8', ' ',
        0x00, 0x00, 0x00, 0x00
    };
    static const guint8 k_tiff_multi[] = {
        'I', 'I', '*', '\0',
        0x08, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x01, 0x00, 0x00, 0x00
    };
    static const guint8 k_tiff_single[] = {
        'I', 'I', '*', '\0',
        0x08, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    gchar *png_anim = write_temp_file(".png", k_png_anim, sizeof(k_png_anim));
    gchar *png_static = write_temp_file(".png", k_png_static, sizeof(k_png_static));
    gchar *webp_anim = write_temp_file(".webp", k_webp_anim, sizeof(k_webp_anim));
    gchar *webp_static = write_temp_file(".webp", k_webp_static, sizeof(k_webp_static));
    gchar *tiff_multi = write_temp_file(".tiff", k_tiff_multi, sizeof(k_tiff_multi));
    gchar *tiff_single = write_temp_file(".tiff", k_tiff_single, sizeof(k_tiff_single));

    g_assert_true(is_animated_image_candidate(png_anim));
    g_assert_false(is_animated_image_candidate(png_static));
    g_assert_true(is_animated_image_candidate(webp_anim));
    g_assert_false(is_animated_image_candidate(webp_static));
    g_assert_true(is_animated_image_candidate(tiff_multi));
    g_assert_false(is_animated_image_candidate(tiff_single));
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
    g_test_add_func("/common/is_video_file_and_media_file", test_is_video_file_and_media_file);
    g_test_add_func("/common/is_book_file_and_valid", test_is_book_file_and_valid);
    g_test_add_func("/common/is_valid_video_file_by_content", test_is_valid_video_file_by_content);
    g_test_add_func("/common/is_animated_image_candidate", test_is_animated_image_candidate);
    g_test_add_func("/common/file_helpers", test_file_helpers);
    g_test_add_func("/common/cleanup_helpers", test_cleanup_helpers);
    g_test_add_func("/common/error_code_to_string", test_error_code_to_string);
    register_browser_tests();
    register_gif_player_tests();
    register_renderer_tests();
    register_text_utils_tests();
    register_app_mode_tests();

    return g_test_run();
}
