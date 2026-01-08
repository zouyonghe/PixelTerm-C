#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>
#include <unistd.h>

#include "renderer.h"

GdkPixbuf *gdk_pixbuf_new_from_stream(GInputStream *stream, GCancellable *cancellable, GError **error) {
    (void)cancellable;
    if (error) {
        *error = NULL;
    }
    if (!stream) {
        return NULL;
    }

    static const guint8 k_png_signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    guint8 header[8] = {0};
    gssize bytes = g_input_stream_read(stream, header, sizeof(header), NULL, NULL);
    if (bytes < (gssize)sizeof(header)) {
        return NULL;
    }
    if (memcmp(header, k_png_signature, sizeof(header)) != 0) {
        return NULL;
    }

    return gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1, 1);
}

static void remove_path(gpointer data) {
    gchar *path = data;
    if (!path) {
        return;
    }
    g_remove(path);
    g_free(path);
}

static gchar *create_temp_path(const gchar *suffix) {
    gchar *template = g_strdup_printf("%s/pixelterm-renderer-XXXXXX", g_get_tmp_dir());
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

    g_test_queue_destroy(remove_path, path);
    return path;
}

static gchar *create_png_file(void) {
    static const gchar k_png_base64[] =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR4nGNg"
        "YAAAAAMAASsJTYQAAAAASUVORK5CYII=";

    gsize len = 0;
    guchar *data = g_base64_decode(k_png_base64, &len);
    if (!data || len == 0) {
        g_error("Failed to decode PNG data");
    }

    gchar *path = create_temp_path(".png");
    GError *error = NULL;
    if (!g_file_set_contents(path, (const gchar *)data, (gssize)len, &error)) {
        g_free(data);
        g_error("Failed to write png: %s", error ? error->message : "unknown error");
    }
    if (error) {
        g_error_free(error);
    }
    g_free(data);
    return path;
}

static void test_renderer_cache_roundtrip(void) {
    ImageRenderer *renderer = renderer_create();
    g_assert_nonnull(renderer);

    GString *rendered = g_string_new("cached");
    renderer_cache_add(renderer, "path", rendered);

    GString *cached = renderer_cache_get(renderer, "path");
    g_assert_nonnull(cached);
    g_assert_cmpstr(cached->str, ==, "cached");

    renderer_cache_clear(renderer);
    g_assert_null(renderer_cache_get(renderer, "path"));

    renderer_destroy(renderer);
}

static void test_renderer_get_rendered_dimensions_defaults(void) {
    ImageRenderer *renderer = renderer_create();
    g_assert_nonnull(renderer);

    renderer->config.max_width = 123;
    renderer->config.max_height = 45;

    gint width = 0;
    gint height = 0;
    renderer_get_rendered_dimensions(renderer, &width, &height);
    g_assert_cmpint(width, ==, 123);
    g_assert_cmpint(height, ==, 45);

    renderer_destroy(renderer);
}

static void test_renderer_get_image_dimensions_valid(void) {
    gchar *path = create_png_file();

    gint width = 0;
    gint height = 0;
    g_assert_cmpint(renderer_get_image_dimensions(path, &width, &height), ==, ERROR_NONE);
    g_assert_cmpint(width, ==, 1);
    g_assert_cmpint(height, ==, 1);
}

static void test_renderer_get_image_dimensions_invalid(void) {
    gint width = 0;
    gint height = 0;
    g_assert_cmpint(renderer_get_image_dimensions(NULL, &width, &height), ==, ERROR_INVALID_IMAGE);
    g_assert_cmpint(renderer_get_image_dimensions("missing.png", NULL, &height), ==, ERROR_INVALID_IMAGE);
    g_assert_cmpint(renderer_get_image_dimensions("missing.png", &width, &height), ==, ERROR_INVALID_IMAGE);

    gchar *path = create_temp_path(".txt");
    GError *error = NULL;
    g_file_set_contents(path, "text", 4, &error);
    if (error) {
        g_error("Failed to write file: %s", error->message);
    }

    g_assert_cmpint(renderer_get_image_dimensions(path, &width, &height), ==, ERROR_INVALID_IMAGE);
}

static void test_renderer_is_image_supported(void) {
    g_assert_true(renderer_is_image_supported("photo.JPG"));
    g_assert_false(renderer_is_image_supported("note.txt"));

    gchar *path = create_png_file();
    g_assert_true(renderer_is_image_supported(path));
}

void register_renderer_tests(void) {
    g_test_add_func("/renderer/cache_roundtrip", test_renderer_cache_roundtrip);
    g_test_add_func("/renderer/get_rendered_dimensions", test_renderer_get_rendered_dimensions_defaults);
    g_test_add_func("/renderer/get_image_dimensions/valid", test_renderer_get_image_dimensions_valid);
    g_test_add_func("/renderer/get_image_dimensions/invalid", test_renderer_get_image_dimensions_invalid);
    g_test_add_func("/renderer/is_image_supported", test_renderer_is_image_supported);
}
