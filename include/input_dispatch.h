#ifndef INPUT_DISPATCH_H
#define INPUT_DISPATCH_H

#include "app.h"
#include "input.h"

void input_dispatch_handle_event(PixelTermApp *app,
                                 InputHandler *input_handler,
                                 const InputEvent *event);
void input_dispatch_process_pending(PixelTermApp *app);
void input_dispatch_process_animations(PixelTermApp *app);
void input_dispatch_pause_video_for_resize(PixelTermApp *app);

#endif
