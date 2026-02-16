#include "input_dispatch.h"
#include "input_dispatch_core.h"

// Implementation lives in src/input_dispatch_core.c; this file keeps the public API stable.

void input_dispatch_handle_event(PixelTermApp *app,
                                 InputHandler *input_handler,
                                 const InputEvent *event) {
    input_dispatch_core_handle_event(app, input_handler, event);
}

void input_dispatch_process_pending(PixelTermApp *app) {
    input_dispatch_core_process_pending(app);
}

void input_dispatch_process_animations(PixelTermApp *app) {
    input_dispatch_core_process_animations(app);
}

void input_dispatch_pause_video_for_resize(PixelTermApp *app) {
    input_dispatch_core_pause_video_for_resize(app);
}
