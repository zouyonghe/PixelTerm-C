#ifndef APP_SINGLE_RENDER_TEST_INTERNAL_H
#define APP_SINGLE_RENDER_TEST_INTERNAL_H

#include "app.h"
#include "media_utils.h"
#include "renderer.h"

/* Test-only hooks for app_single_render internals.
 * These are not part of the supported production API surface.
 */
typedef struct {
    MediaKind (*media_classify)(const char *path);
    void (*get_terminal_cell_geometry)(gint *cell_width, gint *cell_height);
    ErrorCode (*gif_player_load)(GifPlayer *player, const gchar *filepath);
    gboolean (*gif_player_is_animated)(const GifPlayer *player);
    ErrorCode (*gif_player_play)(GifPlayer *player);
    ErrorCode (*video_player_load)(VideoPlayer *player, const gchar *filepath);
    ErrorCode (*video_player_play)(VideoPlayer *player);
    ImageRenderer *(*renderer_create)(void);
    void (*renderer_destroy)(ImageRenderer *renderer);
    ErrorCode (*renderer_initialize)(ImageRenderer *renderer, const RendererConfig *config);
    GString *(*renderer_render_image_file)(ImageRenderer *renderer, const char *filepath);
    void (*renderer_get_rendered_dimensions)(ImageRenderer *renderer, gint *width, gint *height);
    void (*ui_begin_sync_update)(void);
    void (*ui_end_sync_update)(void);
    void (*ui_clear_screen_for_refresh)(const PixelTermApp *app);
    void (*ui_clear_kitty_images)(const PixelTermApp *app);
    void (*ui_clear_single_view_lines)(const PixelTermApp *app);
    void (*ui_clear_area)(const PixelTermApp *app, gint top_row, gint height);
} AppSingleRenderTestHooks;

void app_single_render_set_test_hooks(const AppSingleRenderTestHooks *hooks);
void app_single_render_reset_test_hooks(void);

#endif // APP_SINGLE_RENDER_TEST_INTERNAL_H
