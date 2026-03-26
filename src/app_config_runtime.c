#include "app_config_runtime.h"

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
}
