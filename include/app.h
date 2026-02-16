#ifndef APP_H
#define APP_H

#include "app_state.h"

/* Public app API split by concern; keep app.h as compatibility umbrella header. */
#include "app_core.h"
#include "app_file_manager.h"
#include "app_preview.h"
#include "app_book_mode.h"
#include "app_render.h"
#include "app_runtime.h"

#endif // APP_H
