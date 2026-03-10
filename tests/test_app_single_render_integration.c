#include <glib.h>
#include <string.h>

#include "app.h"
#include "app_single_render_test_internal.h"
#include "ui_render_utils.h"
#include "../src/app_single_render_internal.h"

typedef struct {
    gint gif_load_calls;
    gint gif_play_calls;
    gint video_load_calls;
    gint video_play_calls;
    gint renderer_render_file_calls;
} AppSingleRenderStubState;

static AppSingleRenderStubState g_app_single_render_stub_state;

static MediaKind test_media_classify(const char *path);
static void test_get_terminal_cell_geometry(gint *cell_width, gint *cell_height);
static ErrorCode test_gif_player_load(GifPlayer *player, const gchar *filepath);
static gboolean test_gif_player_is_animated(const GifPlayer *player);
static ErrorCode test_gif_player_play(GifPlayer *player);
static ErrorCode test_video_player_load(VideoPlayer *player, const gchar *filepath);
static ErrorCode test_video_player_play(VideoPlayer *player);
static ImageRenderer *test_renderer_create(void);
static void test_renderer_destroy(ImageRenderer *renderer);
static ErrorCode test_renderer_initialize(ImageRenderer *renderer, const RendererConfig *config);
static GString *test_renderer_render_image_file(ImageRenderer *renderer, const char *filepath);
static void test_renderer_get_rendered_dimensions(ImageRenderer *renderer, gint *width, gint *height);
static void test_ui_begin_sync_update(void);
static void test_ui_end_sync_update(void);
static void test_ui_clear_screen_for_refresh(const PixelTermApp *app);
static void test_ui_clear_kitty_images(const PixelTermApp *app);
static void test_ui_clear_single_view_lines(const PixelTermApp *app);
static void test_ui_clear_area(const PixelTermApp *app, gint top_row, gint height);

static const AppSingleRenderTestHooks k_app_single_render_test_hooks = {
    .media_classify = test_media_classify,
    .get_terminal_cell_geometry = test_get_terminal_cell_geometry,
    .gif_player_load = test_gif_player_load,
    .gif_player_is_animated = test_gif_player_is_animated,
    .gif_player_play = test_gif_player_play,
    .video_player_load = test_video_player_load,
    .video_player_play = test_video_player_play,
    .renderer_create = test_renderer_create,
    .renderer_destroy = test_renderer_destroy,
    .renderer_initialize = test_renderer_initialize,
    .renderer_render_image_file = test_renderer_render_image_file,
    .renderer_get_rendered_dimensions = test_renderer_get_rendered_dimensions,
    .ui_begin_sync_update = test_ui_begin_sync_update,
    .ui_end_sync_update = test_ui_end_sync_update,
    .ui_clear_screen_for_refresh = test_ui_clear_screen_for_refresh,
    .ui_clear_kitty_images = test_ui_clear_kitty_images,
    .ui_clear_single_view_lines = test_ui_clear_single_view_lines,
    .ui_clear_area = test_ui_clear_area,
};

static void app_single_render_reset_stubs(void) {
    memset(&g_app_single_render_stub_state, 0, sizeof(g_app_single_render_stub_state));
}

static void destroy_render_test_app(PixelTermApp *app) {
    app_single_render_reset_test_hooks();

    if (!app) {
        return;
    }

    g_list_free_full(app->image_files, g_free);
    app->image_files = NULL;
    app->total_images = 0;
    app->current_index = 0;

    if (app->gif_player) {
        gif_player_destroy(app->gif_player);
        app->gif_player = NULL;
    }
    if (app->video_player) {
        video_player_destroy(app->video_player);
        app->video_player = NULL;
    }
}

static gboolean init_render_test_app(PixelTermApp *app) {
    if (!app) {
        return FALSE;
    }

    app->gif_player = gif_player_new(1, TRUE, FALSE, FALSE, FALSE, 1.0);
    app->video_player = video_player_new(1, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!app->gif_player || !app->video_player) {
        destroy_render_test_app(app);
        return FALSE;
    }

    app->image_files = g_list_append(app->image_files, g_strdup("clip.mp4"));
    app->image_files = g_list_append(app->image_files, g_strdup("anim.gif"));
    app->image_files = g_list_append(app->image_files, g_strdup("still.png"));
    app->total_images = g_list_length(app->image_files);
    app->current_index = 0;
    app->mode = APP_MODE_SINGLE;
    app->term_width = 120;
    app->term_height = 40;
    app->video_scale = 1.0;
    app->image_zoom = 1.0;
    app->ui_text_hidden = TRUE;

    app_single_render_set_test_hooks(&k_app_single_render_test_hooks);
    return TRUE;
}

static void test_file_size_display_clamps_missing_values(void) {
    g_assert_cmpfloat(app_single_render_file_size_mb_for_display(-1), ==, 0.0);
    g_assert_cmpfloat(app_single_render_file_size_mb_for_display(0), ==, 0.0);
    g_assert_cmpfloat(app_single_render_file_size_mb_for_display(1024 * 1024), ==, 1.0);
}

static void test_single_view_render_switches_media_players(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();

    app.current_index = 0;
    g_assert_cmpint(app_render_current_image(&app), ==, ERROR_NONE);
    g_assert_true(app.video_player->is_playing);
    g_assert_false(app.gif_player->is_playing);
    g_assert_cmpint(g_app_single_render_stub_state.video_play_calls, ==, 1);
    g_assert_cmpint(g_app_single_render_stub_state.gif_play_calls, ==, 0);

    app.current_index = 1;
    g_assert_cmpint(app_render_current_image(&app), ==, ERROR_NONE);
    g_assert_true(app.gif_player->is_playing);
    g_assert_false(app.video_player->is_playing);
    g_assert_cmpint(g_app_single_render_stub_state.gif_play_calls, ==, 1);

    app.current_index = 0;
    g_assert_cmpint(app_render_current_image(&app), ==, ERROR_NONE);
    g_assert_true(app.video_player->is_playing);
    g_assert_false(app.gif_player->is_playing);
    g_assert_cmpint(g_app_single_render_stub_state.video_play_calls, ==, 2);

    app.current_index = 2;
    g_assert_cmpint(app_render_current_image(&app), ==, ERROR_NONE);
    g_assert_false(app.video_player->is_playing);
    g_assert_false(app.gif_player->is_playing);
    g_assert_cmpint(g_app_single_render_stub_state.renderer_render_file_calls, ==, 2);

    destroy_render_test_app(&app);
}

void register_app_single_render_integration_tests(void) {
    g_test_add_func("/app_single_render/file_size_display/clamps_missing_values",
                    test_file_size_display_clamps_missing_values);
    g_test_add_func("/app_single_render/single_view/switches_media_players",
                    test_single_view_render_switches_media_players);
}

const gchar *app_get_current_filepath(const PixelTermApp *app) {
    if (!app) {
        return NULL;
    }
    return g_list_nth_data(app->image_files, app->current_index);
}

gint app_get_current_index(const PixelTermApp *app) {
    return app ? app->current_index : -1;
}

gint app_get_total_images(const PixelTermApp *app) {
    return app ? app->total_images : 0;
}

void app_get_image_target_dimensions(const PixelTermApp *app, gint *max_width, gint *max_height) {
    if (!max_width || !max_height) {
        return;
    }

    gint width = (app && app->term_width > 0) ? app->term_width : 80;
    gint height = (app && app->term_height > 0) ? app->term_height : 24;
    height -= 6;
    if (height < 1) {
        height = 1;
    }
    if (width < 1) {
        width = 1;
    }

    *max_width = width;
    *max_height = height;
}

void app_preloader_update_terminal(PixelTermApp *app) {
    (void)app;
}

ErrorCode app_render_book_page(PixelTermApp *app) {
    (void)app;
    return ERROR_NONE;
}

static MediaKind test_media_classify(const char *path) {
    if (g_strcmp0(path, "clip.mp4") == 0) {
        return MEDIA_KIND_VIDEO;
    }
    if (g_strcmp0(path, "anim.gif") == 0) {
        return MEDIA_KIND_ANIMATED_IMAGE;
    }
    if (g_strcmp0(path, "still.png") == 0) {
        return MEDIA_KIND_IMAGE;
    }
    return MEDIA_KIND_UNKNOWN;
}

static void test_get_terminal_cell_geometry(gint *cell_width, gint *cell_height) {
    if (cell_width) {
        *cell_width = 10;
    }
    if (cell_height) {
        *cell_height = 20;
    }
}

static ErrorCode test_gif_player_load(GifPlayer *player, const gchar *filepath) {
    if (!player || !filepath) {
        return ERROR_INVALID_IMAGE;
    }

    g_free(player->filepath);
    player->filepath = g_strdup(filepath);
    player->is_animated = TRUE;
    g_app_single_render_stub_state.gif_load_calls++;
    return ERROR_NONE;
}

static gboolean test_gif_player_is_animated(const GifPlayer *player) {
    return player && player->is_animated;
}

static ErrorCode test_gif_player_play(GifPlayer *player) {
    if (!player || !player->filepath || !player->is_animated) {
        return ERROR_INVALID_IMAGE;
    }

    player->is_playing = TRUE;
    g_app_single_render_stub_state.gif_play_calls++;
    return ERROR_NONE;
}

static ErrorCode test_video_player_load(VideoPlayer *player, const gchar *filepath) {
    if (!player || !filepath) {
        return ERROR_INVALID_IMAGE;
    }

    g_free(player->filepath);
    player->filepath = g_strdup(filepath);
    player->has_video = TRUE;
    g_app_single_render_stub_state.video_load_calls++;
    return ERROR_NONE;
}

static ErrorCode test_video_player_play(VideoPlayer *player) {
    if (!player || !player->filepath) {
        return ERROR_INVALID_IMAGE;
    }

    player->is_playing = TRUE;
    g_app_single_render_stub_state.video_play_calls++;
    return ERROR_NONE;
}

static ImageRenderer *test_renderer_create(void) {
    return g_new0(ImageRenderer, 1);
}

static void test_renderer_destroy(ImageRenderer *renderer) {
    g_free(renderer);
}

static ErrorCode test_renderer_initialize(ImageRenderer *renderer, const RendererConfig *config) {
    if (!renderer || !config) {
        return ERROR_MEMORY_ALLOC;
    }

    renderer->config = *config;
    return ERROR_NONE;
}

static GString *test_renderer_render_image_file(ImageRenderer *renderer, const char *filepath) {
    (void)renderer;
    (void)filepath;
    g_app_single_render_stub_state.renderer_render_file_calls++;
    return g_string_new("");
}

static void test_renderer_get_rendered_dimensions(ImageRenderer *renderer, gint *width, gint *height) {
    if (width) {
        *width = renderer ? renderer->config.max_width : 0;
    }
    if (height) {
        *height = 1;
    }
}

static void test_ui_begin_sync_update(void) {
}

static void test_ui_end_sync_update(void) {
}

static void test_ui_clear_screen_for_refresh(const PixelTermApp *app) {
    (void)app;
}

static void test_ui_clear_kitty_images(const PixelTermApp *app) {
    (void)app;
}

static void test_ui_clear_single_view_lines(const PixelTermApp *app) {
    (void)app;
}

static void test_ui_clear_area(const PixelTermApp *app, gint top_row, gint height) {
    (void)app;
    (void)top_row;
    (void)height;
}

gint ui_filename_max_width(const PixelTermApp *app) {
    return app && app->term_width > 0 ? app->term_width : 80;
}

void ui_print_centered_help_line(gint row,
                                 gint term_width,
                                 const HelpSegment *segments,
                                 gsize n) {
    (void)row;
    (void)term_width;
    (void)segments;
    (void)n;
}

void ui_begin_sync_update(void) {
}

void ui_end_sync_update(void) {
}

void ui_clear_screen_for_refresh(const PixelTermApp *app) {
    (void)app;
}

void ui_clear_kitty_images(const PixelTermApp *app) {
    (void)app;
}

void ui_clear_single_view_lines(const PixelTermApp *app) {
    (void)app;
}

void ui_clear_area(const PixelTermApp *app, gint top_row, gint height) {
    (void)app;
    (void)top_row;
    (void)height;
}
