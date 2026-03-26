#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "book.h"

static gint g_book_fallback_free_calls = 0;
static gpointer g_book_fallback_last_freed = NULL;

static void book_fallback_test_g_free(gpointer data) {
    if (data) {
        g_book_fallback_free_calls++;
        g_book_fallback_last_freed = data;
    }
    free(data);
}

#define book_open fallback_book_open
#define book_close fallback_book_close
#define book_get_path fallback_book_get_path
#define book_get_page_count fallback_book_get_page_count
#define book_render_page fallback_book_render_page
#define book_page_image_free fallback_book_page_image_free
#define book_load_toc fallback_book_load_toc
#define book_toc_free fallback_book_toc_free
#define g_free book_fallback_test_g_free
#ifdef HAVE_MUPDF
#undef HAVE_MUPDF
#endif
#include "../src/book.c"
#undef g_free
#undef book_open
#undef book_close
#undef book_get_path
#undef book_get_page_count
#undef book_render_page
#undef book_page_image_free
#undef book_load_toc
#undef book_toc_free

typedef struct {
    const gchar *path;
    BookDocument *doc;
    ErrorCode error;
} BookOpenInvocation;

static void invoke_book_open(gpointer data) {
    BookOpenInvocation *invocation = data;
    invocation->doc = book_open(invocation->path, &invocation->error);
}

static gchar *capture_stderr(void (*func)(gpointer), gpointer data) {
    GError *error = NULL;
    gchar *capture_path = NULL;
    int capture_fd = g_file_open_tmp("pixelterm-book-stderr-XXXXXX", &capture_path, &error);
    if (capture_fd < 0) {
        g_error("Failed to create stderr capture file: %s",
                error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
        error = NULL;
    }

    fflush(stderr);
    int saved_fd = dup(STDERR_FILENO);
    if (saved_fd < 0) {
        close(capture_fd);
        g_error("Failed to duplicate stderr");
    }
    if (dup2(capture_fd, STDERR_FILENO) < 0) {
        close(saved_fd);
        close(capture_fd);
        g_error("Failed to redirect stderr");
    }
    close(capture_fd);

    func(data);

    fflush(stderr);
    if (dup2(saved_fd, STDERR_FILENO) < 0) {
        close(saved_fd);
        g_error("Failed to restore stderr");
    }
    close(saved_fd);

    gchar *captured = NULL;
    if (!g_file_get_contents(capture_path, &captured, NULL, &error)) {
        g_error("Failed to read stderr capture: %s",
                error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
    }

    g_remove(capture_path);
    g_free(capture_path);
    return captured;
}

static void remove_path(gpointer data) {
    if (!data) {
        return;
    }

    g_remove((const gchar *)data);
    g_free(data);
}

static void remove_dir(gpointer data) {
    if (!data) {
        return;
    }

    g_rmdir((const gchar *)data);
    g_free(data);
}

static gchar *make_temp_dir(void) {
    GError *error = NULL;
    gchar *path = g_dir_make_tmp("pixelterm-book-dir-XXXXXX", &error);
    if (!path) {
        g_error("Failed to create temp directory: %s",
                error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
    }

    g_test_queue_destroy(remove_dir, g_strdup(path));
    return path;
}

static gchar *write_temp_file(const gchar *suffix, const guint8 *data, gsize len) {
    gchar *template = g_strdup_printf("%s/pixelterm-book-test-XXXXXX", g_get_tmp_dir());
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

    GError *error = NULL;
    const gchar *contents = (len == 0 && data == NULL) ? "" : (const gchar *)data;
    if (!g_file_set_contents(path, contents, (gssize)len, &error)) {
        g_error("Failed to write temp file: %s", error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
    }

    g_test_queue_destroy(remove_path, path);
    return path;
}

static gchar *make_missing_temp_path(const gchar *suffix) {
    gchar *template = g_strdup_printf("%s/pixelterm-book-missing-XXXXXX", g_get_tmp_dir());
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

    g_assert_cmpint(g_remove(path), ==, 0);
    return path;
}

static void test_book_open_rejects_null_path(void) {
    ErrorCode error = ERROR_NONE;
    BookDocument *doc = book_open(NULL, &error);

    g_assert_null(doc);
    g_assert_cmpint(error, ==, ERROR_FILE_NOT_FOUND);
}

static void test_book_open_reports_missing_path(void) {
    ErrorCode error = ERROR_NONE;
    gchar *missing_path = make_missing_temp_path(".pdf");
    BookDocument *doc = book_open(missing_path, &error);

    g_assert_null(doc);
    g_assert_cmpint(error, ==, ERROR_FILE_NOT_FOUND);

    g_free(missing_path);
}

static void test_book_open_rejects_directory_path(void) {
    ErrorCode error = ERROR_NONE;
    gchar *dir_path = make_temp_dir();
    BookDocument *doc = book_open(dir_path, &error);

    g_assert_null(doc);
    g_assert_cmpint(error, ==, ERROR_FILE_NOT_FOUND);
}

static void test_book_open_rejects_invalid_book_bytes(void) {
    static const guint8 k_invalid_book[] = {'n', 'o', 't', ' ', 'a', ' ', 'b', 'o', 'o', 'k'};

    gchar *path = write_temp_file(".epub", k_invalid_book, sizeof(k_invalid_book));
    BookOpenInvocation invocation = {
        .path = path,
        .doc = NULL,
        .error = ERROR_NONE,
    };
    gchar *stderr_output = capture_stderr(invoke_book_open, &invocation);

    g_assert_null(invocation.doc);
    g_assert_cmpint(invocation.error, ==, ERROR_INVALID_IMAGE);
    g_assert_cmpstr(stderr_output, ==, "");

    g_free(stderr_output);
}

static void test_book_null_helpers_are_safe(void) {
    g_assert_null(book_get_path(NULL));
    g_assert_cmpint(book_get_page_count(NULL), ==, 0);
    g_assert_null(book_load_toc(NULL));

    book_close(NULL);
    book_toc_free(NULL);
}

static void test_book_page_image_free_resets_and_is_idempotent(void) {
    BookPageImage image = {
        .pixels = g_malloc(12),
        .width = 2,
        .height = 2,
        .stride = 6,
        .channels = 3,
    };

    g_assert_nonnull(image.pixels);

    book_page_image_free(&image);

    g_assert_null(image.pixels);
    g_assert_cmpint(image.width, ==, 0);
    g_assert_cmpint(image.height, ==, 0);
    g_assert_cmpint(image.stride, ==, 0);
    g_assert_cmpint(image.channels, ==, 0);

    book_page_image_free(&image);

    g_assert_null(image.pixels);
    g_assert_cmpint(image.width, ==, 0);
    g_assert_cmpint(image.height, ==, 0);
    g_assert_cmpint(image.stride, ==, 0);
    g_assert_cmpint(image.channels, ==, 0);
}

static void test_book_fallback_page_image_free_frees_pixels(void) {
    guint8 *pixels = g_malloc(12);
    BookPageImage image = {
        .pixels = pixels,
        .width = 2,
        .height = 2,
        .stride = 6,
        .channels = 3,
    };

    g_book_fallback_free_calls = 0;
    g_book_fallback_last_freed = NULL;

    fallback_book_page_image_free(&image);

    g_assert_cmpint(g_book_fallback_free_calls, ==, 1);
    g_assert_true(g_book_fallback_last_freed == pixels);
    g_assert_null(image.pixels);
    g_assert_cmpint(image.width, ==, 0);
    g_assert_cmpint(image.height, ==, 0);
    g_assert_cmpint(image.stride, ==, 0);
    g_assert_cmpint(image.channels, ==, 0);

    fallback_book_page_image_free(&image);

    g_assert_cmpint(g_book_fallback_free_calls, ==, 1);
}

void register_book_tests(void) {
    g_test_add_func("/book/open/null_path", test_book_open_rejects_null_path);
    g_test_add_func("/book/open/missing_path", test_book_open_reports_missing_path);
    g_test_add_func("/book/open/directory_path", test_book_open_rejects_directory_path);
    g_test_add_func("/book/open/invalid_book_bytes", test_book_open_rejects_invalid_book_bytes);
    g_test_add_func("/book/null_helpers_are_safe", test_book_null_helpers_are_safe);
    g_test_add_func("/book/fallback/page_image_free/frees_pixels",
                    test_book_fallback_page_image_free_frees_pixels);
    g_test_add_func("/book/page_image_free/resets_and_is_idempotent",
                    test_book_page_image_free_resets_and_is_idempotent);
}
