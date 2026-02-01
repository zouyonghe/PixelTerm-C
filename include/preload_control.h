#ifndef PRELOAD_CONTROL_H
#define PRELOAD_CONTROL_H

#include "app.h"

void app_preloader_reset(PixelTermApp *app);
ErrorCode app_preloader_enable(PixelTermApp *app, gboolean queue_tasks);
void app_preloader_disable(PixelTermApp *app);
void app_preloader_clear_queue(PixelTermApp *app);
void app_preloader_queue_directory(PixelTermApp *app);
void app_preloader_update_terminal(PixelTermApp *app);

#endif
