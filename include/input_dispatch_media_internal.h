#ifndef INPUT_DISPATCH_MEDIA_INTERNAL_H
#define INPUT_DISPATCH_MEDIA_INTERNAL_H

#include <glib.h>

#include "app.h"

gboolean input_dispatch_current_is_video(const PixelTermApp *app);
gboolean input_dispatch_current_is_animated_image(const PixelTermApp *app);

#endif
