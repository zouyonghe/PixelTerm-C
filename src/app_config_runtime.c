#include "app_config_runtime.h"

RendererConfig app_renderer_config_from_app(const PixelTermApp *app, gint max_width, gint max_height) {
    if (!app) {
        g_warning("app_renderer_config_from_app called with NULL app; using safe renderer defaults");
    }

    RendererConfig config = {
        .max_width = max_width,
        .max_height = max_height,
        .preserve_aspect_ratio = TRUE,
        .dither = app ? app->dither_enabled : FALSE,
        .color_space = CHAFA_COLOR_SPACE_RGB,
        .work_factor = app ? app->render_work_factor : 4,
        .force_text = app ? app->force_text : TRUE,
        .force_sixel = app ? app->force_sixel : FALSE,
        .force_kitty = app ? app->force_kitty : FALSE,
        .force_iterm2 = app ? app->force_iterm2 : FALSE,
        .text_symbol_mode = app ? app->text_symbol_mode : TEXT_SYMBOL_MODE_AUTO,
        .gamma = app ? app->gamma : 1.0,
        .color_enhance = app ? app->color_enhance : COLOR_ENHANCE_OFF,
        .dither_mode = (app && app->dither_enabled) ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
        .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
        .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
    };
    return config;
}

void app_config_apply_runtime(PixelTermApp *app, const AppConfig *config) {
    if (!app || !config) {
        return;
    }

    app->preload_enabled = config->preload_enabled;
    app->dither_enabled = config->dither_enabled;
    app->clear_workaround_enabled = config->clear_workaround_enabled;
    app->render_work_factor = config->work_factor;
    app->gamma = config->gamma;
    app->force_text = config->force_text;
    app->force_sixel = config->force_sixel;
    app->force_kitty = config->force_kitty;
    app->force_iterm2 = config->force_iterm2;
    app->text_symbol_mode = config->text_symbol_mode;
    app->color_enhance = config->color_enhance;
}
