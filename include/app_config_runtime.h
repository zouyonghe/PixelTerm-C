#ifndef APP_CONFIG_RUNTIME_H
#define APP_CONFIG_RUNTIME_H

#include "app_state.h"

RendererConfig app_renderer_config_from_app(const PixelTermApp *app, gint max_width, gint max_height);
void app_config_apply_runtime(PixelTermApp *app, const AppConfig *config);

#endif // APP_CONFIG_RUNTIME_H
