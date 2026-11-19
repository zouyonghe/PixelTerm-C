#ifndef APP_MEDIA_SESSION_H
#define APP_MEDIA_SESSION_H

#include "app_state.h"
#include "media_utils.h"

/* Stop both inactive producers even if one fails; return the first failure. */
ErrorCode app_media_stop_inactive_players(PixelTermApp *app, MediaKind active_kind);

#endif // APP_MEDIA_SESSION_H
