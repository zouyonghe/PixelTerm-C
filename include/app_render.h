#ifndef APP_RENDER_H
#define APP_RENDER_H

#include "app_state.h"

ErrorCode app_render_current_image(PixelTermApp *app);
ErrorCode app_display_image_info(PixelTermApp *app);
ErrorCode app_refresh_display(PixelTermApp *app);
ErrorCode app_render_by_mode(PixelTermApp *app);
void app_process_async_render(PixelTermApp *app);
void app_get_image_target_dimensions(const PixelTermApp *app, gint *max_width, gint *max_height);

#endif // APP_RENDER_H
