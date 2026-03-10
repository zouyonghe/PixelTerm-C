#ifndef APP_SINGLE_RENDER_INTERNAL_H
#define APP_SINGLE_RENDER_INTERNAL_H

#include "app.h"

static inline void app_clear_async_render_state(PixelTermApp *app) {
    if (!app) {
        return;
    }
    app->async.image_pending = FALSE;
    app->async.image_index = -1;
    g_clear_pointer(&app->async.image_path, g_free);
}

static inline gdouble app_single_render_file_size_mb_for_display(gint64 file_size) {
    return file_size > 0 ? file_size / (1024.0 * 1024.0) : 0.0;
}

#endif // APP_SINGLE_RENDER_INTERNAL_H
