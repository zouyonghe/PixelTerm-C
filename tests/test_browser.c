#include <glib.h>
#include <glib/gstdio.h>

#include "browser.h"

static void remove_dir_tree(gpointer data) {
    gchar *dir = data;
    if (!dir) {
        return;
    }

    GDir *handle = g_dir_open(dir, 0, NULL);
    if (handle) {
        const gchar *name = NULL;
        while ((name = g_dir_read_name(handle)) != NULL) {
            gchar *path = g_build_filename(dir, name, NULL);
            g_remove(path);
            g_free(path);
        }
        g_dir_close(handle);
    }

    g_rmdir(dir);
    g_free(dir);
}

static gchar *create_temp_dir(void) {
    gchar *template = g_build_filename(g_get_tmp_dir(), "pixelterm-browser-XXXXXX", NULL);
    gchar *dir = g_mkdtemp(template);
    if (!dir) {
        g_free(template);
        g_error("Failed to create temp directory");
    }
    g_test_queue_destroy(remove_dir_tree, dir);
    return dir;
}

static gchar *write_file_in_dir(const gchar *dir, const gchar *name, const guint8 *data, gsize len) {
    gchar *path = g_build_filename(dir, name, NULL);
    const gchar *contents = (len == 0 && data == NULL) ? "" : (const gchar *)data;
    GError *error = NULL;
    if (!g_file_set_contents(path, contents, (gssize)len, &error)) {
        g_error("Failed to write file: %s", error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
    }
    return path;
}

static void test_browser_scan_directory_filters_and_sorts(void) {
    static const guint8 k_jpeg[] = {0xFF, 0xD8, 0xFF, 0x00};
    static const guint8 k_png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    static const guint8 k_invalid[] = {0x00, 0x01, 0x02, 0x03};

    gchar *dir = create_temp_dir();
    gchar *a_jpg = write_file_in_dir(dir, "a.jpg", k_jpeg, sizeof(k_jpeg));
    gchar *b_png = write_file_in_dir(dir, "b.png", k_png, sizeof(k_png));
    gchar *noext = write_file_in_dir(dir, "noext", k_png, sizeof(k_png));
    write_file_in_dir(dir, "c.png", k_invalid, sizeof(k_invalid));
    write_file_in_dir(dir, "note.txt", (const guint8 *)"text", 4);

    FileBrowser *browser = browser_create();
    g_assert_nonnull(browser);
    g_assert_cmpint(browser_scan_directory(browser, dir), ==, ERROR_NONE);
    g_assert_cmpint(browser_get_total_files(browser), ==, 3);

    GList *files = browser_get_all_files(browser);
    g_assert_nonnull(files);
    g_assert_cmpint(g_list_length(files), ==, 3);

    gchar *first = g_path_get_basename(files->data);
    gchar *second = g_path_get_basename(files->next->data);
    gchar *third = g_path_get_basename(files->next->next->data);

    g_assert_cmpstr(first, ==, "a.jpg");
    g_assert_cmpstr(second, ==, "b.png");
    g_assert_cmpstr(third, ==, "noext");

    g_free(first);
    g_free(second);
    g_free(third);

    g_assert_cmpstr(browser_get_directory(browser), ==, dir);
    g_assert_cmpstr(browser_get_current_file(browser), ==, a_jpg);

    browser_destroy(browser);
    g_free(a_jpg);
    g_free(b_png);
    g_free(noext);
}

static void test_browser_navigation_and_delete(void) {
    static const guint8 k_jpeg[] = {0xFF, 0xD8, 0xFF, 0x00};

    gchar *dir = create_temp_dir();
    gchar *a_jpg = write_file_in_dir(dir, "a.jpg", k_jpeg, sizeof(k_jpeg));
    gchar *b_jpg = write_file_in_dir(dir, "b.jpg", k_jpeg, sizeof(k_jpeg));

    FileBrowser *browser = browser_create();
    g_assert_nonnull(browser);
    g_assert_cmpint(browser_scan_directory(browser, dir), ==, ERROR_NONE);
    g_assert_cmpint(browser_get_total_files(browser), ==, 2);

    g_assert_true(browser_is_at_first(browser));
    g_assert_cmpint(browser_next_file(browser), ==, ERROR_NONE);
    g_assert_true(browser_is_at_last(browser));
    g_assert_cmpint(browser_next_file(browser), ==, ERROR_INVALID_IMAGE);

    g_assert_cmpint(browser_previous_file(browser), ==, ERROR_NONE);
    g_assert_true(browser_is_at_first(browser));

    g_assert_cmpint(browser_goto_index(browser, 1), ==, ERROR_NONE);
    g_assert_true(browser_is_at_last(browser));
    g_assert_cmpint(browser_goto_index(browser, -1), ==, ERROR_INVALID_IMAGE);
    g_assert_cmpint(browser_goto_index(browser, 2), ==, ERROR_INVALID_IMAGE);

    g_assert_cmpint(browser_goto_filename(browser, "a.jpg"), ==, ERROR_NONE);
    g_assert_true(browser_is_at_first(browser));

    g_assert_cmpint(browser_goto_filename(browser, "b.jpg"), ==, ERROR_NONE);
    g_assert_cmpint(browser_delete_current_file(browser), ==, ERROR_NONE);
    g_assert_cmpint(browser_get_total_files(browser), ==, 1);
    g_assert_false(file_exists(b_jpg));
    g_assert_cmpstr(browser_get_current_file(browser), ==, a_jpg);

    browser_destroy(browser);
    g_free(a_jpg);
    g_free(b_jpg);
}

static void test_browser_reset(void) {
    static const guint8 k_png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    gchar *dir = create_temp_dir();
    write_file_in_dir(dir, "a.png", k_png, sizeof(k_png));
    write_file_in_dir(dir, "b.png", k_png, sizeof(k_png));

    FileBrowser *browser = browser_create();
    g_assert_nonnull(browser);
    g_assert_cmpint(browser_scan_directory(browser, dir), ==, ERROR_NONE);

    g_assert_cmpint(browser_goto_index(browser, 1), ==, ERROR_NONE);
    g_assert_cmpint(browser_get_current_index(browser), ==, 1);

    browser_reset(browser);
    g_assert_cmpint(browser_get_current_index(browser), ==, 0);

    browser_destroy(browser);
}

void register_browser_tests(void) {
    g_test_add_func("/browser/scan/filters_and_sorts", test_browser_scan_directory_filters_and_sorts);
    g_test_add_func("/browser/navigation_and_delete", test_browser_navigation_and_delete);
    g_test_add_func("/browser/reset", test_browser_reset);
}
