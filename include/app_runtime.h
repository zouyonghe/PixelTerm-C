#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#include "app_state.h"

void app_toggle_preload(PixelTermApp *app);
/* Stops media without committing view state; failures latch last_error, success preserves it.
 * Playback remains stopped if subsequent terminal output fails. When requested,
 * clears the live preload queue before view commit; callers staging a view must
 * detach its preloader to preserve that queue on failure. */
ErrorCode app_prepare_mode_entry(PixelTermApp *app, gboolean clear_preloader_queue);
ErrorCode app_transition_mode(PixelTermApp *app, AppMode mode);
void app_set_mode(PixelTermApp *app, AppMode mode);
gboolean app_should_exit(const PixelTermApp *app);

#endif // APP_RUNTIME_H
