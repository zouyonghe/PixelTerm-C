#ifndef INPUT_DISPATCH_DELETE_INTERNAL_H
#define INPUT_DISPATCH_DELETE_INTERNAL_H

#include <glib.h>

#include "app.h"
#include "input.h"

gboolean input_dispatch_handle_delete_request(PixelTermApp *app, const InputEvent *event);

#endif
