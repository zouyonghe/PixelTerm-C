#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#include "app_state.h"

void app_toggle_preload(PixelTermApp *app);
ErrorCode app_transition_mode(PixelTermApp *app, AppMode mode);
void app_set_mode(PixelTermApp *app, AppMode mode);
gboolean app_should_exit(const PixelTermApp *app);

#endif // APP_RUNTIME_H
