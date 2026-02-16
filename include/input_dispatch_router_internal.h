#ifndef INPUT_DISPATCH_ROUTER_INTERNAL_H
#define INPUT_DISPATCH_ROUTER_INTERNAL_H

#include "app.h"
#include "input.h"

typedef void (*ModeKeyPressHandler)(PixelTermApp *app,
                                    InputHandler *input_handler,
                                    const InputEvent *event);
typedef void (*ModeMouseHandler)(PixelTermApp *app, const InputEvent *event);

typedef struct {
    ModeKeyPressHandler key_press;
    ModeMouseHandler mouse_press;
    ModeMouseHandler mouse_double_click;
    ModeMouseHandler mouse_scroll;
} ModeInputHandlers;

#endif
