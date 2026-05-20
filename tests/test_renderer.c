#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>
#include <unistd.h>

#include "renderer.h"
#include "renderer_test_internal.h"

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

typedef enum {
    TEST_PATTERN_TOP_HALF = 0,
    TEST_PATTERN_BOTTOM_LEFT_QUARTER,
    TEST_PATTERN_LEFT_HALF
} RendererTestPattern;

typedef struct {
    gunichar code_point;
    gint raw_fg;
    gint raw_bg;
} RenderedCell;

static void fill_test_pattern_rgba(guint8 *pixels, RendererTestPattern pattern) {
    memset(pixels, 0, 8 * 8 * 4);

    for (gint y = 0; y < 8; y++) {
        for (gint x = 0; x < 8; x++) {
            gboolean on = FALSE;
            if (pattern == TEST_PATTERN_TOP_HALF) {
                on = y < 4;
            } else if (pattern == TEST_PATTERN_BOTTOM_LEFT_QUARTER) {
                on = x < 4 && y >= 4;
            } else if (pattern == TEST_PATTERN_LEFT_HALF) {
                on = x < 4;
            }

            guint8 *pixel = pixels + ((y * 8 + x) * 4);
            pixel[0] = on ? 255 : 0;
            pixel[1] = on ? 255 : 0;
            pixel[2] = on ? 255 : 0;
            pixel[3] = 255;
        }
    }
}

static RenderedCell render_test_pattern_cell(TextSymbolMode mode, RendererTestPattern pattern) {
    ImageRenderer *renderer = renderer_create();
    g_assert_nonnull(renderer);

    RendererConfig config = {
        .max_width = 1,
        .max_height = 1,
        .preserve_aspect_ratio = TRUE,
        .dither = FALSE,
        .color_space = CHAFA_COLOR_SPACE_RGB,
        .work_factor = 6,
        .force_text = TRUE,
        .force_sixel = FALSE,
        .force_kitty = FALSE,
        .force_iterm2 = FALSE,
        .text_symbol_mode = mode,
        .gamma = 1.0,
        .dither_mode = CHAFA_DITHER_MODE_NONE,
        .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
        .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
    };

    g_assert_cmpint(renderer_initialize(renderer, &config), ==, ERROR_NONE);

    guint8 pixels[8 * 8 * 4];
    fill_test_pattern_rgba(pixels, pattern);
    chafa_canvas_draw_all_pixels(renderer->canvas,
                                 CHAFA_PIXEL_RGBA8_UNASSOCIATED,
                                 pixels,
                                 8,
                                 8,
                                 8 * 4);

    RenderedCell cell = {
        .code_point = chafa_canvas_get_char_at(renderer->canvas, 0, 0),
        .raw_fg = 0,
        .raw_bg = 0
    };
    chafa_canvas_get_raw_colors_at(renderer->canvas, 0, 0, &cell.raw_fg, &cell.raw_bg);
    renderer_destroy(renderer);
    return cell;
}

static gboolean rendered_cell_equals(const RenderedCell *a, const RenderedCell *b) {
    return a && b &&
           a->code_point == b->code_point &&
           a->raw_fg == b->raw_fg &&
           a->raw_bg == b->raw_bg;
}

static gboolean rendered_cell_is_left_half_split(const RenderedCell *cell) {
    if (!cell) {
        return FALSE;
    }

    return (cell->code_point == 0x258C && cell->raw_fg == 0xFFFFFF && cell->raw_bg == 0x000000) ||
           (cell->code_point == 0x2590 && cell->raw_fg == 0x000000 && cell->raw_bg == 0xFFFFFF);
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

static void test_renderer_is_graphics_mode_false_for_text_mode(void) {
    ImageRenderer *renderer = renderer_create();
    g_assert_nonnull(renderer);

    RendererConfig config = {
        .max_width = 20,
        .max_height = 10,
        .preserve_aspect_ratio = TRUE,
        .dither = FALSE,
        .color_space = CHAFA_COLOR_SPACE_RGB,
        .work_factor = 6,
        .force_text = TRUE,
        .force_sixel = FALSE,
        .force_kitty = FALSE,
        .force_iterm2 = FALSE,
        .gamma = 1.0,
        .dither_mode = CHAFA_DITHER_MODE_NONE,
        .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
        .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
    };

    g_assert_cmpint(renderer_initialize(renderer, &config), ==, ERROR_NONE);
    g_assert_false(renderer_is_graphics_mode(renderer));

    renderer_destroy(renderer);
}

static void test_renderer_is_graphics_mode_true_for_forced_kitty_mode(void) {
    ImageRenderer *renderer = renderer_create();
    g_assert_nonnull(renderer);

    RendererConfig config = {
        .max_width = 20,
        .max_height = 10,
        .preserve_aspect_ratio = TRUE,
        .dither = FALSE,
        .color_space = CHAFA_COLOR_SPACE_RGB,
        .work_factor = 6,
        .force_text = FALSE,
        .force_sixel = FALSE,
        .force_kitty = TRUE,
        .force_iterm2 = FALSE,
        .gamma = 1.0,
        .dither_mode = CHAFA_DITHER_MODE_NONE,
        .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
        .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
    };

    g_assert_cmpint(renderer_initialize(renderer, &config), ==, ERROR_NONE);
    g_assert_true(renderer_is_graphics_mode(renderer));

    renderer_destroy(renderer);
}

static void test_renderer_text_symbol_mode_half_reduces_quadrant_detail(void) {
    RenderedCell auto_cell = render_test_pattern_cell(TEXT_SYMBOL_MODE_AUTO, TEST_PATTERN_BOTTOM_LEFT_QUARTER);
    RenderedCell half_cell = render_test_pattern_cell(TEXT_SYMBOL_MODE_HALF, TEST_PATTERN_BOTTOM_LEFT_QUARTER);

    g_assert_false(rendered_cell_equals(&auto_cell, &half_cell));
}

static void test_renderer_text_symbol_mode_quarter_preserves_quadrant_detail(void) {
    RenderedCell auto_cell = render_test_pattern_cell(TEXT_SYMBOL_MODE_AUTO, TEST_PATTERN_BOTTOM_LEFT_QUARTER);
    RenderedCell quarter_cell = render_test_pattern_cell(TEXT_SYMBOL_MODE_QUARTER, TEST_PATTERN_BOTTOM_LEFT_QUARTER);

    g_assert_true(rendered_cell_equals(&auto_cell, &quarter_cell));
}

static void test_renderer_text_symbol_mode_quarter_keeps_exact_half_split(void) {
    RenderedCell quarter_cell = render_test_pattern_cell(TEXT_SYMBOL_MODE_QUARTER, TEST_PATTERN_LEFT_HALF);
    g_assert_true(rendered_cell_is_left_half_split(&quarter_cell));
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

static void test_renderer_color_enhance_off_keeps_pixels_unchanged(void) {
    const guint8 pixels[] = {120, 80, 70, 255};

    guint8 *adjusted = renderer_color_enhance_copy_for_test(pixels,
                                                            1,
                                                            1,
                                                            4,
                                                            4,
                                                            COLOR_ENHANCE_OFF);

    g_assert_null(adjusted);
    g_assert_cmpuint(pixels[0], ==, 120);
    g_assert_cmpuint(pixels[1], ==, 80);
    g_assert_cmpuint(pixels[2], ==, 70);
    g_assert_cmpuint(pixels[3], ==, 255);
}

static void test_renderer_color_enhance_vivid_boosts_color_separation(void) {
    const guint8 pixels[] = {120, 80, 70, 255};

    guint8 *adjusted = renderer_color_enhance_copy_for_test(pixels,
                                                            1,
                                                            1,
                                                            4,
                                                            4,
                                                            COLOR_ENHANCE_VIVID);

    g_assert_nonnull(adjusted);
    g_assert_cmpuint(adjusted[0], >, pixels[0]);
    g_assert_cmpuint(adjusted[1], <, pixels[1]);
    g_assert_cmpuint(adjusted[2], <, pixels[2]);
    g_assert_cmpuint(adjusted[3], ==, pixels[3]);
    g_assert_cmpuint(adjusted[0], <=, 255);
    g_assert_cmpuint(adjusted[1], <=, 255);
    g_assert_cmpuint(adjusted[2], <=, 255);
    g_free(adjusted);
}

static void test_renderer_color_enhance_skips_short_pixel_formats(void) {
    const guint8 pixels[] = {120, 80};

    guint8 *adjusted = renderer_color_enhance_copy_for_test(pixels,
                                                            1,
                                                            1,
                                                            2,
                                                            2,
                                                            COLOR_ENHANCE_VIVID);

    g_assert_null(adjusted);
}

static void test_renderer_validate_pixel_data_rejects_short_rowstride(void) {
    gsize buffer_size = 0;

    g_assert_false(renderer_validate_pixel_data_for_test(4, 2, 11, 3, &buffer_size));
}

static void test_renderer_validate_pixel_data_rejects_unsupported_channels(void) {
    gsize buffer_size = 0;

    g_assert_false(renderer_validate_pixel_data_for_test(1, 1, 2, 2, &buffer_size));
}

static void test_renderer_validate_pixel_data_accepts_extra_channels(void) {
    gsize buffer_size = 0;

    g_assert_true(renderer_validate_pixel_data_for_test(1, 1, 5, 5, &buffer_size));
    g_assert_cmpuint(buffer_size, ==, 5);
}

static void test_renderer_validate_pixel_data_rejects_large_geometry(void) {
    gsize buffer_size = 0;

    g_assert_false(renderer_validate_pixel_data_for_test(1,
                                                         PIXELTERM_MAX_DECODED_PIXELS + 1,
                                                         3,
                                                         3,
                                                         &buffer_size));
}

void register_renderer_tests(void) {
    g_test_add_func("/renderer/cache_roundtrip", test_renderer_cache_roundtrip);
    g_test_add_func("/renderer/get_rendered_dimensions", test_renderer_get_rendered_dimensions_defaults);
    g_test_add_func("/renderer/is_graphics_mode/text_mode",
                    test_renderer_is_graphics_mode_false_for_text_mode);
    g_test_add_func("/renderer/is_graphics_mode/forced_kitty_mode",
                    test_renderer_is_graphics_mode_true_for_forced_kitty_mode);
    g_test_add_func("/renderer/text_symbol_mode/half_reduces_quadrant_detail",
                    test_renderer_text_symbol_mode_half_reduces_quadrant_detail);
    g_test_add_func("/renderer/text_symbol_mode/quarter_preserves_quadrant_detail",
                    test_renderer_text_symbol_mode_quarter_preserves_quadrant_detail);
    g_test_add_func("/renderer/text_symbol_mode/quarter_keeps_exact_half_split",
                    test_renderer_text_symbol_mode_quarter_keeps_exact_half_split);
    g_test_add_func("/renderer/get_image_dimensions/valid", test_renderer_get_image_dimensions_valid);
    g_test_add_func("/renderer/get_image_dimensions/invalid", test_renderer_get_image_dimensions_invalid);
    g_test_add_func("/renderer/is_image_supported", test_renderer_is_image_supported);
    g_test_add_func("/renderer/color_enhance/off_keeps_pixels_unchanged",
                    test_renderer_color_enhance_off_keeps_pixels_unchanged);
    g_test_add_func("/renderer/color_enhance/vivid_boosts_color_separation",
                    test_renderer_color_enhance_vivid_boosts_color_separation);
    g_test_add_func("/renderer/color_enhance/skips_short_pixel_formats",
                    test_renderer_color_enhance_skips_short_pixel_formats);
    g_test_add_func("/renderer/validate_pixel_data/rejects_short_rowstride",
                    test_renderer_validate_pixel_data_rejects_short_rowstride);
    g_test_add_func("/renderer/validate_pixel_data/rejects_unsupported_channels",
                    test_renderer_validate_pixel_data_rejects_unsupported_channels);
    g_test_add_func("/renderer/validate_pixel_data/accepts_extra_channels",
                    test_renderer_validate_pixel_data_accepts_extra_channels);
    g_test_add_func("/renderer/validate_pixel_data/rejects_large_geometry",
                    test_renderer_validate_pixel_data_rejects_large_geometry);
}
