#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "app_single_render_test_internal.h"
#include "input_dispatch_test_support.h"
#include "ui_render_utils.h"
#include "../src/app_single_render_internal.h"

typedef struct {
    gint gif_load_calls;
    gint gif_play_calls;
    gint video_load_calls;
    gint video_play_calls;
    gint renderer_render_file_calls;
    ErrorCode video_load_result;
    gboolean last_force_text;
    gboolean last_force_kitty;
    gboolean last_force_iterm2;
    gboolean last_force_sixel;
    gint clear_screen_calls;
    gint clear_area_calls;
} AppSingleRenderStubState;

static AppSingleRenderStubState g_app_single_render_stub_state;

typedef void (*RenderCaptureFunc)(gpointer user_data);

static gchar *capture_render_output(RenderCaptureFunc render_func, gpointer user_data) {
    gchar *template = g_strdup_printf("%s/pixelterm-single-render-XXXXXX", g_get_tmp_dir());
    int fd = g_mkstemp(template);
    g_assert_cmpint(fd, >=, 0);

    int saved_stdout = dup(STDOUT_FILENO);
    g_assert_cmpint(saved_stdout, >=, 0);

    fflush(stdout);
    g_assert_cmpint(dup2(fd, STDOUT_FILENO), >=, 0);
    close(fd);

    render_func(user_data);

    fflush(stdout);
    g_assert_cmpint(dup2(saved_stdout, STDOUT_FILENO), >=, 0);
    close(saved_stdout);

    gchar *output = NULL;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(template, &output, NULL, &error));
    g_assert_no_error(error);
    g_remove(template);
    g_free(template);
    return output;
}

static void render_current_image_capture(gpointer user_data) {
    PixelTermApp *app = (PixelTermApp *)user_data;
    g_assert_cmpint(app_render_current_image(app), ==, ERROR_NONE);
}

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
    g_app_single_render_stub_state.video_load_result = ERROR_NONE;
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
    if (app->preloader) {
        preloader_destroy(app->preloader);
        app->preloader = NULL;
    }
}

static gboolean init_render_test_app(PixelTermApp *app) {
    if (!app) {
        return FALSE;
    }

    app->gif_player = gif_player_new(1, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    app->video_player = video_player_new(1, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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

static void test_single_view_video_load_failure_does_not_fallback_to_image_render(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    g_app_single_render_stub_state.video_load_result = ERROR_INVALID_IMAGE;
    app.current_index = 0;

    g_assert_cmpint(app_render_current_image(&app), ==, ERROR_INVALID_IMAGE);
    g_assert_cmpint(g_app_single_render_stub_state.video_load_calls, ==, 1);
    g_assert_cmpint(g_app_single_render_stub_state.video_play_calls, ==, 0);
    g_assert_cmpint(g_app_single_render_stub_state.renderer_render_file_calls, ==, 0);

    destroy_render_test_app(&app);
}

static void test_single_view_render_preserves_info_visibility(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 2;
    app.info_visible = TRUE;

    g_assert_cmpint(app_render_current_image(&app), ==, ERROR_NONE);
    g_assert_true(app.info_visible);

    destroy_render_test_app(&app);
}

static void test_single_view_visible_shell_preserves_current_layout_contract(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 2;
    app.ui_text_hidden = FALSE;

    gchar *output = capture_render_output(render_current_image_capture, &app);

    g_assert_nonnull(g_strstr_len(output, -1, "\033[1;1H\033[2K"));
    g_assert_nonnull(g_strstr_len(output, -1, "Image View"));
    g_assert_nonnull(g_strstr_len(output, -1, "\033[3;1H\033[2K"));
    g_assert_nonnull(g_strstr_len(output, -1, "3/3"));
    g_assert_nonnull(g_strstr_len(output, -1, "\033[38;1H\033[2K"));
    g_assert_nonnull(g_strstr_len(output, -1, "\033[34mstill.png\033[0m"));
    g_assert_nonnull(g_strstr_len(output, -1, "\033[40;1H\033[2K"));
    g_assert_nonnull(g_strstr_len(output, -1, "Enter"));
    g_assert_nonnull(g_strstr_len(output, -1, "Preview"));

    g_free(output);
    destroy_render_test_app(&app);
}

static void test_info_overlay_renders_even_when_ui_text_is_hidden(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 2;
    app.info_visible = TRUE;
    app.force_kitty = TRUE;

    gchar *output = capture_render_output(render_current_image_capture, &app);
    g_assert_nonnull(g_strstr_len(output, -1, "File Info"));
    g_free(output);

    destroy_render_test_app(&app);
}

static void test_info_overlay_forces_text_rendering_over_graphics_modes(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 2;
    app.info_visible = TRUE;
    app.force_kitty = TRUE;

    g_assert_cmpint(app_render_current_image(&app), ==, ERROR_NONE);
    g_assert_true(g_app_single_render_stub_state.last_force_text);
    g_assert_false(g_app_single_render_stub_state.last_force_kitty);
    g_assert_false(g_app_single_render_stub_state.last_force_iterm2);
    g_assert_false(g_app_single_render_stub_state.last_force_sixel);

    destroy_render_test_app(&app);
}

static void test_help_overlay_forces_text_rendering_over_graphics_modes(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 2;
    app.help_visible = TRUE;
    app.force_kitty = TRUE;

    g_assert_cmpint(app_render_current_image(&app), ==, ERROR_NONE);
    g_assert_true(g_app_single_render_stub_state.last_force_text);
    g_assert_false(g_app_single_render_stub_state.last_force_kitty);
    g_assert_false(g_app_single_render_stub_state.last_force_iterm2);
    g_assert_false(g_app_single_render_stub_state.last_force_sixel);

    destroy_render_test_app(&app);
}

static void test_help_overlay_rerenders_during_single_view_redraw(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 2;
    app.help_visible = TRUE;

    gchar *output = capture_render_output(render_current_image_capture, &app);

    g_assert_nonnull(g_strstr_len(output, -1, "Image View Help"));
    g_assert_true(app.help_visible);

    g_free(output);
    destroy_render_test_app(&app);
}

static void test_help_overlay_clears_when_terminal_too_small_to_render(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 2;
    app.help_visible = TRUE;
    app.force_kitty = TRUE;
    app.term_width = 20;
    app.term_height = 6;

    g_assert_cmpint(app_render_current_image(&app), ==, ERROR_NONE);
    g_assert_false(app.help_visible);
    g_assert_false(g_app_single_render_stub_state.last_force_text);
    g_assert_true(g_app_single_render_stub_state.last_force_kitty);

    destroy_render_test_app(&app);
}

static void test_help_overlay_uses_full_clear_even_when_suppressed(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 2;
    app.help_visible = TRUE;
    app.suppress_full_clear = TRUE;

    g_assert_cmpint(app_render_current_image(&app), ==, ERROR_NONE);
    g_assert_cmpint(g_app_single_render_stub_state.clear_screen_calls, ==, 1);
    g_assert_cmpint(g_app_single_render_stub_state.clear_area_calls, ==, 0);

    destroy_render_test_app(&app);
}

static void test_help_overlay_pauses_video_and_skips_playback(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 0;
    app.help_visible = TRUE;
    app.video_player->is_playing = TRUE;
    app.video_player->has_video = TRUE;

    gchar *output = capture_render_output(render_current_image_capture, &app);

    g_assert_false(app.video_player->is_playing);
    g_assert_cmpint(g_app_single_render_stub_state.video_play_calls, ==, 0);
    g_assert_nonnull(g_strstr_len(output, -1, "Image View Help"));

    g_free(output);
    destroy_render_test_app(&app);
}

static void test_info_overlay_pauses_video_and_renders_panel(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 0;
    app.info_visible = TRUE;
    app.video_player->is_playing = TRUE;
    app.video_player->has_video = TRUE;

    gchar *output = capture_render_output(render_current_image_capture, &app);

    g_assert_false(app.video_player->is_playing);
    g_assert_cmpint(g_app_single_render_stub_state.video_play_calls, ==, 0);
    g_assert_nonnull(g_strstr_len(output, -1, "File Info"));

    g_free(output);
    destroy_render_test_app(&app);
}

static void test_info_overlay_does_not_render_when_terminal_too_short(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 2;
    app.info_visible = TRUE;
    app.term_width = 80;
    app.term_height = 9;

    gchar *output = capture_render_output(render_current_image_capture, &app);

    g_assert_null(g_strstr_len(output, -1, "File Info"));

    g_free(output);
    destroy_render_test_app(&app);
}

static void test_info_overlay_uses_full_terminal_height_for_fit(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 2;
    app.info_visible = TRUE;
    app.term_width = 80;
    app.term_height = 12;

    gchar *output = capture_render_output(render_current_image_capture, &app);

    g_assert_nonnull(g_strstr_len(output, -1, "File Info"));

    g_free(output);
    destroy_render_test_app(&app);
}

static void test_info_overlay_pauses_animated_gif_updates(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app_single_render_reset_stubs();
    app.current_index = 1;
    app.info_visible = TRUE;
    app.gif_player->is_playing = TRUE;

    g_assert_cmpint(app_render_current_image(&app), ==, ERROR_NONE);
    g_assert_false(app.gif_player->is_playing);
    g_assert_cmpint(g_app_single_render_stub_state.gif_play_calls, ==, 0);

    destroy_render_test_app(&app);
}

static void test_info_overlay_bypasses_preloader_cache(void) {
    PixelTermApp app = {0};
    if (!init_render_test_app(&app)) {
        g_test_skip("media players unavailable");
        return;
    }

    app.preloader = preloader_create();
    g_assert_nonnull(app.preloader);
    app.preload_enabled = TRUE;

    gint target_width = 0;
    gint target_height = 0;
    app_get_image_target_dimensions(&app, &target_width, &target_height);

    GString *cached_graphics = g_string_new("\033_Gf=100;cached\033\\");
    preloader_cache_add(app.preloader,
                        "still.png",
                        cached_graphics,
                        target_width,
                        target_height,
                        TRUE,
                        target_width,
                        target_height);
    g_string_free(cached_graphics, TRUE);

    app_single_render_reset_stubs();
    app.current_index = 2;
    app.info_visible = TRUE;
    app.force_kitty = TRUE;

    g_assert_cmpint(app_render_current_image(&app), ==, ERROR_NONE);
    g_assert_cmpint(g_app_single_render_stub_state.renderer_render_file_calls, ==, 1);
    g_assert_true(g_app_single_render_stub_state.last_force_text);

    destroy_render_test_app(&app);
}

void register_app_single_render_integration_tests(void) {
    g_test_add_func("/app_single_render/file_size_display/clamps_missing_values",
                    test_file_size_display_clamps_missing_values);
    g_test_add_func("/app_single_render/single_view/switches_media_players",
                    test_single_view_render_switches_media_players);
    g_test_add_func("/app_single_render/single_view/video_load_failure_does_not_fallback_to_image_render",
                    test_single_view_video_load_failure_does_not_fallback_to_image_render);
    g_test_add_func("/app_single_render/single_view/visible_shell_preserves_current_layout_contract",
                    test_single_view_visible_shell_preserves_current_layout_contract);
    g_test_add_func("/app_single_render/info_overlay/preserves_visibility_on_redraw",
                    test_single_view_render_preserves_info_visibility);
    g_test_add_func("/app_single_render/info_overlay/renders_even_when_ui_text_is_hidden",
                    test_info_overlay_renders_even_when_ui_text_is_hidden);
    g_test_add_func("/app_single_render/info_overlay/forces_text_rendering_over_graphics_modes",
                    test_info_overlay_forces_text_rendering_over_graphics_modes);
    g_test_add_func("/app_single_render/help_overlay/forces_text_rendering_over_graphics_modes",
                    test_help_overlay_forces_text_rendering_over_graphics_modes);
    g_test_add_func("/app_single_render/help_overlay/rerenders_during_single_view_redraw",
                    test_help_overlay_rerenders_during_single_view_redraw);
    g_test_add_func("/app_single_render/help_overlay/clears_when_terminal_too_small_to_render",
                    test_help_overlay_clears_when_terminal_too_small_to_render);
    g_test_add_func("/app_single_render/help_overlay/uses_full_clear_even_when_suppressed",
                    test_help_overlay_uses_full_clear_even_when_suppressed);
    g_test_add_func("/app_single_render/help_overlay/pauses_video_and_skips_playback",
                    test_help_overlay_pauses_video_and_skips_playback);
    g_test_add_func("/app_single_render/info_overlay/pauses_video_and_renders_panel",
                    test_info_overlay_pauses_video_and_renders_panel);
    g_test_add_func("/app_single_render/info_overlay/does_not_render_when_terminal_too_short",
                    test_info_overlay_does_not_render_when_terminal_too_short);
    g_test_add_func("/app_single_render/info_overlay/uses_full_terminal_height_for_fit",
                    test_info_overlay_uses_full_terminal_height_for_fit);
    g_test_add_func("/app_single_render/info_overlay/pauses_animated_gif_updates",
                    test_info_overlay_pauses_animated_gif_updates);
    g_test_add_func("/app_single_render/info_overlay/bypasses_preloader_cache",
                    test_info_overlay_bypasses_preloader_cache);
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
    g_input_dispatch_stub_state.book_page_render_calls++;
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
    return g_app_single_render_stub_state.video_load_result;
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
    g_app_single_render_stub_state.last_force_text = config->force_text;
    g_app_single_render_stub_state.last_force_kitty = config->force_kitty;
    g_app_single_render_stub_state.last_force_iterm2 = config->force_iterm2;
    g_app_single_render_stub_state.last_force_sixel = config->force_sixel;
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
    g_app_single_render_stub_state.clear_screen_calls++;
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
    g_app_single_render_stub_state.clear_area_calls++;
}
