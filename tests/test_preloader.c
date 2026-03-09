#include <glib.h>

#include "preloader.h"

static void test_preloader_get_cached_image_returns_caller_owned_copy(void) {
    ImagePreloader *preloader = preloader_create();
    g_assert_nonnull(preloader);

    GString *rendered = g_string_new("cached-image");
    preloader_cache_add(preloader, "image.png", rendered, 7, 3, FALSE, 10, 5);
    g_string_free(rendered, TRUE);

    GString *first = preloader_get_cached_image(preloader, "image.png", 10, 5);
    g_assert_nonnull(first);
    g_assert_cmpstr(first->str, ==, "cached-image");

    GString *second = preloader_get_cached_image(preloader, "image.png", 10, 5);
    g_assert_nonnull(second);
    g_assert_true(first != second);
    g_assert_cmpstr(first->str, ==, second->str);

    g_string_assign(first, "caller-mutation");
    g_assert_cmpstr(second->str, ==, "cached-image");
    g_string_free(first, TRUE);
    g_string_free(second, TRUE);

    preloader_destroy(preloader);
}

static void test_preloader_get_cached_image_miss_returns_null(void) {
    ImagePreloader *preloader = preloader_create();
    g_assert_nonnull(preloader);

    g_assert_null(preloader_get_cached_image(preloader, "missing.png", 10, 5));

    preloader_destroy(preloader);
}

static void test_preloader_get_cached_render_info_returns_dimensions_and_graphics_mode(void) {
    ImagePreloader *preloader = preloader_create();
    g_assert_nonnull(preloader);

    GString *rendered = g_string_new("\033_Gf=100;payload\033\\");
    preloader_cache_add(preloader, "image.png", rendered, 8, 3, TRUE, 10, 5);
    g_string_free(rendered, TRUE);

    gint width = 0;
    gint height = 0;
    gboolean graphics_mode = FALSE;
    g_assert_true(preloader_get_cached_render_info(preloader,
                                                   "image.png",
                                                   10,
                                                   5,
                                                   &width,
                                                   &height,
                                                   &graphics_mode));
    g_assert_cmpint(width, ==, 8);
    g_assert_cmpint(height, ==, 3);
    g_assert_true(graphics_mode);

    preloader_destroy(preloader);
}

void register_preloader_tests(void) {
    g_test_add_func("/preloader/get_cached_image/caller_owned_copy",
                    test_preloader_get_cached_image_returns_caller_owned_copy);
    g_test_add_func("/preloader/get_cached_image/miss_returns_null",
                    test_preloader_get_cached_image_miss_returns_null);
    g_test_add_func("/preloader/get_cached_render_info/returns_dimensions_and_graphics_mode",
                    test_preloader_get_cached_render_info_returns_dimensions_and_graphics_mode);
}
