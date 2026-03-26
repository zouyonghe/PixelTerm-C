#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "app_startup.h"
#include "common.h"

static void remove_path(gpointer data) {
    if (!data) {
        return;
    }

    g_remove((const gchar *)data);
    g_free(data);
}

static gboolean remove_tree_recursive(const gchar *path) {
    GError *error = NULL;
    GDir *dir = g_dir_open(path, 0, &error);
    if (!dir) {
        if (error) {
            g_error_free(error);
        }
        return g_remove(path) == 0;
    }

    const gchar *name = NULL;
    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *child = g_build_filename(path, name, NULL);
        remove_tree_recursive(child);
        g_free(child);
    }

    g_dir_close(dir);
    return g_rmdir(path) == 0;
}

static void remove_tree(gpointer data) {
    if (!data) {
        return;
    }

    remove_tree_recursive((const gchar *)data);
    g_free(data);
}

static gchar *make_temp_dir(void) {
    GError *error = NULL;
    gchar *path = g_dir_make_tmp("pixelterm-app-startup-XXXXXX", &error);
    if (!path) {
        g_error("Failed to create temp directory: %s",
                error ? error->message : "unknown error");
    }

    if (error) {
        g_error_free(error);
    }

    g_test_queue_destroy(remove_tree, g_strdup(path));
    return path;
}

static gchar *write_temp_file(const gchar *dir,
                              const gchar *name,
                              const guint8 *data,
                              gsize len) {
    gchar *path = g_build_filename(dir, name, NULL);
    GError *error = NULL;
    const gchar *contents = data ? (const gchar *)data : "";

    if (!g_file_set_contents(path, contents, (gssize)len, &error)) {
        g_error("Failed to write temp file: %s",
                error ? error->message : "unknown error");
    }

    if (error) {
        g_error_free(error);
    }

    g_test_queue_destroy(remove_path, g_strdup(path));
    return path;
}

static void test_classify_path_without_argument_uses_current_directory(void) {
    AppStartupPathDecision decision = {0};
    gchar *original_dir = g_get_current_dir();
    gchar *temp_dir = make_temp_dir();

    g_assert_cmpint(g_chdir(temp_dir), ==, 0);
    gchar *expected_dir = g_get_current_dir();

    ErrorCode error = app_startup_classify_path(NULL, &decision);

    g_assert_cmpint(error, ==, ERROR_NONE);
    g_assert_cmpint(decision.kind, ==, APP_STARTUP_PATH_DIRECTORY);
    g_assert_cmpstr(decision.path, ==, expected_dir);

    g_assert_cmpint(g_chdir(original_dir), ==, 0);
    g_free(expected_dir);
    g_free(original_dir);
    g_free(decision.path);
}

static void test_classify_path_rejects_missing_path(void) {
    AppStartupPathDecision decision = {0};
    gchar *missing_path = g_build_filename(g_get_tmp_dir(),
                                           "pixelterm-app-startup-missing-file",
                                           NULL);

    ErrorCode error = app_startup_classify_path(missing_path, &decision);

    g_assert_cmpint(error, ==, ERROR_FILE_NOT_FOUND);
    g_assert_null(decision.path);

    g_free(missing_path);
}

static void test_classify_path_directory_returns_directory_target(void) {
    AppStartupPathDecision decision = {0};
    gchar *temp_dir = make_temp_dir();

    ErrorCode error = app_startup_classify_path(temp_dir, &decision);

    g_assert_cmpint(error, ==, ERROR_NONE);
    g_assert_cmpint(decision.kind, ==, APP_STARTUP_PATH_DIRECTORY);
    g_assert_cmpstr(decision.path, ==, temp_dir);

    g_free(decision.path);
}

static void test_classify_path_book_returns_book_target(void) {
    static const guint8 k_pdf[] = {'%', 'P', 'D', 'F'};
    AppStartupPathDecision decision = {0};
    gchar *temp_dir = make_temp_dir();
    gchar *book_path = write_temp_file(temp_dir, "book.pdf", k_pdf, sizeof(k_pdf));

    ErrorCode error = app_startup_classify_path(book_path, &decision);

    g_assert_cmpint(error, ==, ERROR_NONE);
    g_assert_cmpint(decision.kind, ==, APP_STARTUP_PATH_BOOK);
    g_assert_cmpstr(decision.path, ==, book_path);

    g_free(decision.path);
}

static void test_classify_path_media_returns_media_target(void) {
    static const guint8 k_png[] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    };
    AppStartupPathDecision decision = {0};
    gchar *temp_dir = make_temp_dir();
    gchar *media_path = write_temp_file(temp_dir, "image.png", k_png, sizeof(k_png));

    ErrorCode error = app_startup_classify_path(media_path, &decision);

    g_assert_cmpint(error, ==, ERROR_NONE);
    g_assert_cmpint(decision.kind, ==, APP_STARTUP_PATH_MEDIA);
    g_assert_cmpstr(decision.path, ==, media_path);

    g_free(decision.path);
}

static void test_classify_path_non_media_file_falls_back_to_parent_directory(void) {
    static const guint8 k_text[] = {'n', 'o', 't', 'e'};
    AppStartupPathDecision decision = {0};
    gchar *temp_dir = make_temp_dir();
    gchar *text_path = write_temp_file(temp_dir, "note.txt", k_text, sizeof(k_text));

    ErrorCode error = app_startup_classify_path(text_path, &decision);

    g_assert_cmpint(error, ==, ERROR_NONE);
    g_assert_cmpint(decision.kind, ==, APP_STARTUP_PATH_PARENT_DIRECTORY);
    g_assert_cmpstr(decision.path, ==, temp_dir);

    g_free(decision.path);
}

void register_app_startup_tests(void) {
    g_test_add_func("/app_startup/classify/no_path",
                    test_classify_path_without_argument_uses_current_directory);
    g_test_add_func("/app_startup/classify/bad_path",
                    test_classify_path_rejects_missing_path);
    g_test_add_func("/app_startup/classify/directory",
                    test_classify_path_directory_returns_directory_target);
    g_test_add_func("/app_startup/classify/book",
                    test_classify_path_book_returns_book_target);
    g_test_add_func("/app_startup/classify/media",
                    test_classify_path_media_returns_media_target);
    g_test_add_func("/app_startup/classify/non_media_parent_directory",
                    test_classify_path_non_media_file_falls_back_to_parent_directory);
}
