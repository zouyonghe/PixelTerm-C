#include <glib.h>

#define media_classify app_single_render_test_media_classify
#define get_terminal_size app_single_render_test_get_terminal_size
#define get_terminal_cell_geometry app_single_render_test_get_terminal_cell_geometry
#define gif_player_is_animated app_single_render_test_gif_player_is_animated
#define gif_player_load app_single_render_test_gif_player_load
#define gif_player_play app_single_render_test_gif_player_play
#define gif_player_set_render_area app_single_render_test_gif_player_set_render_area
#define gif_player_update_terminal_size app_single_render_test_gif_player_update_terminal_size
#define video_player_load app_single_render_test_video_player_load
#define video_player_play app_single_render_test_video_player_play
#define video_player_set_render_area app_single_render_test_video_player_set_render_area
#define video_player_update_terminal_size app_single_render_test_video_player_update_terminal_size
#define renderer_create app_single_render_test_renderer_create
#define renderer_destroy app_single_render_test_renderer_destroy
#define renderer_initialize app_single_render_test_renderer_initialize
#define renderer_get_rendered_dimensions app_single_render_test_renderer_get_rendered_dimensions
#define renderer_render_image_file app_single_render_test_renderer_render_image_file
#define ui_begin_sync_update app_single_render_test_ui_begin_sync_update
#define ui_end_sync_update app_single_render_test_ui_end_sync_update
#define ui_clear_screen_for_refresh app_single_render_test_ui_clear_screen_for_refresh
#define ui_clear_kitty_images app_single_render_test_ui_clear_kitty_images
#define ui_clear_single_view_lines app_single_render_test_ui_clear_single_view_lines
#define ui_clear_area app_single_render_test_ui_clear_area
#define ui_filename_max_width app_single_render_test_ui_filename_max_width
#define ui_print_centered_help_line app_single_render_test_ui_print_centered_help_line
#define app_display_image_info app_single_render_test_app_display_image_info
#define app_refresh_display app_single_render_test_app_refresh_display
#define app_process_async_render app_single_render_test_app_process_async_render
#include "../src/app_single_render.c"
#undef media_classify
#undef get_terminal_size
#undef get_terminal_cell_geometry
#undef gif_player_is_animated
#undef gif_player_load
#undef gif_player_play
#undef gif_player_set_render_area
#undef gif_player_update_terminal_size
#undef video_player_load
#undef video_player_play
#undef video_player_set_render_area
#undef video_player_update_terminal_size
#undef renderer_create
#undef renderer_destroy
#undef renderer_initialize
#undef renderer_get_rendered_dimensions
#undef renderer_render_image_file
#undef ui_begin_sync_update
#undef ui_end_sync_update
#undef ui_clear_screen_for_refresh
#undef ui_clear_kitty_images
#undef ui_clear_single_view_lines
#undef ui_clear_area
#undef ui_filename_max_width
#undef ui_print_centered_help_line
#undef app_display_image_info
#undef app_refresh_display
#undef app_process_async_render

typedef struct {
    gint gif_load_calls;
    gint gif_play_calls;
    gint video_load_calls;
    gint video_play_calls;
    gint renderer_render_file_calls;
} AppSingleRenderStubState;

static AppSingleRenderStubState g_app_single_render_stub_state;

static void app_single_render_reset_stubs(void) {
    memset(&g_app_single_render_stub_state, 0, sizeof(g_app_single_render_stub_state));
}

static void destroy_render_test_app(PixelTermApp *app) {
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

    return TRUE;
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

MediaKind app_single_render_test_media_classify(const char *path) {
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

void app_single_render_test_get_terminal_size(gint *width, gint *height) {
    if (width) {
        *width = 120;
    }
    if (height) {
        *height = 40;
    }
}

void app_single_render_test_get_terminal_cell_geometry(gint *cell_width, gint *cell_height) {
    if (cell_width) {
        *cell_width = 10;
    }
    if (cell_height) {
        *cell_height = 20;
    }
}

ErrorCode app_single_render_test_gif_player_load(GifPlayer *player, const gchar *filepath) {
    if (!player || !filepath) {
        return ERROR_INVALID_IMAGE;
    }

    g_free(player->filepath);
    player->filepath = g_strdup(filepath);
    player->is_animated = TRUE;
    g_app_single_render_stub_state.gif_load_calls++;
    return ERROR_NONE;
}

ErrorCode app_single_render_test_gif_player_play(GifPlayer *player) {
    if (!player || !player->filepath || !player->is_animated) {
        return ERROR_INVALID_IMAGE;
    }

    player->is_playing = TRUE;
    g_app_single_render_stub_state.gif_play_calls++;
    return ERROR_NONE;
}

gboolean app_single_render_test_gif_player_is_animated(const GifPlayer *player) {
    return player && player->is_animated;
}

void app_single_render_test_gif_player_set_render_area(GifPlayer *player,
                                                       gint term_width,
                                                       gint term_height,
                                                       gint area_top_row,
                                                       gint area_height,
                                                       gint max_width,
                                                       gint max_height) {
    if (!player) {
        return;
    }

    player->render_term_width = term_width;
    player->render_term_height = term_height;
    player->render_area_top_row = area_top_row;
    player->render_area_height = area_height;
    player->render_max_width = max_width;
    player->render_max_height = max_height;
    player->render_layout_valid = TRUE;
}

ErrorCode app_single_render_test_gif_player_update_terminal_size(GifPlayer *player) {
    (void)player;
    return ERROR_NONE;
}

ErrorCode app_single_render_test_video_player_load(VideoPlayer *player, const gchar *filepath) {
    if (!player || !filepath) {
        return ERROR_INVALID_IMAGE;
    }

    g_free(player->filepath);
    player->filepath = g_strdup(filepath);
    player->has_video = TRUE;
    g_app_single_render_stub_state.video_load_calls++;
    return ERROR_NONE;
}

ErrorCode app_single_render_test_video_player_play(VideoPlayer *player) {
    if (!player || !player->filepath) {
        return ERROR_INVALID_IMAGE;
    }

    player->is_playing = TRUE;
    g_app_single_render_stub_state.video_play_calls++;
    return ERROR_NONE;
}

void app_single_render_test_video_player_set_render_area(VideoPlayer *player,
                                                         gint term_width,
                                                         gint term_height,
                                                         gint area_top_row,
                                                         gint area_height,
                                                         gint max_width,
                                                         gint max_height) {
    if (!player) {
        return;
    }

    player->render_term_width = term_width;
    player->render_term_height = term_height;
    player->render_area_top_row = area_top_row;
    player->render_area_height = area_height;
    player->render_max_width = max_width;
    player->render_max_height = max_height;
    player->render_layout_valid = TRUE;
}

ErrorCode app_single_render_test_video_player_update_terminal_size(VideoPlayer *player) {
    (void)player;
    return ERROR_NONE;
}

ImageRenderer *app_single_render_test_renderer_create(void) {
    return g_new0(ImageRenderer, 1);
}

void app_single_render_test_renderer_destroy(ImageRenderer *renderer) {
    g_free(renderer);
}

ErrorCode app_single_render_test_renderer_initialize(ImageRenderer *renderer,
                                                     const RendererConfig *config) {
    if (!renderer || !config) {
        return ERROR_MEMORY_ALLOC;
    }

    renderer->config = *config;
    return ERROR_NONE;
}

void app_single_render_test_renderer_get_rendered_dimensions(ImageRenderer *renderer,
                                                             gint *width,
                                                             gint *height) {
    if (!renderer) {
        if (width) {
            *width = 0;
        }
        if (height) {
            *height = 0;
        }
        return;
    }

    if (width) {
        *width = renderer->config.max_width;
    }
    if (height) {
        *height = 1;
    }
}

GString *app_single_render_test_renderer_render_image_file(ImageRenderer *renderer,
                                                           const char *filepath) {
    (void)renderer;
    (void)filepath;
    g_app_single_render_stub_state.renderer_render_file_calls++;
    return g_string_new("");
}

gint app_single_render_test_ui_filename_max_width(const PixelTermApp *app) {
    return app && app->term_width > 0 ? app->term_width : 80;
}

void app_single_render_test_ui_print_centered_help_line(gint row,
                                                        gint term_width,
                                                        const HelpSegment *segments,
                                                        gsize n) {
    (void)row;
    (void)term_width;
    (void)segments;
    (void)n;
}

void app_single_render_test_ui_begin_sync_update(void) {
}

void app_single_render_test_ui_end_sync_update(void) {
}

void app_single_render_test_ui_clear_screen_for_refresh(const PixelTermApp *app) {
    (void)app;
}

void app_single_render_test_ui_clear_kitty_images(const PixelTermApp *app) {
    (void)app;
}

void app_single_render_test_ui_clear_single_view_lines(const PixelTermApp *app) {
    (void)app;
}

void app_single_render_test_ui_clear_area(const PixelTermApp *app, gint top_row, gint height) {
    (void)app;
    (void)top_row;
    (void)height;
}
