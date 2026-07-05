#ifndef APP_STARTUP_H
#define APP_STARTUP_H

#include <glib.h>

#include "common.h"

typedef enum {
    APP_STARTUP_PATH_DIRECTORY,
    APP_STARTUP_PATH_BOOK,
    APP_STARTUP_PATH_MEDIA,
} AppStartupPathKind;

typedef struct {
    AppStartupPathKind kind;
    gchar *path;
    gint failure_errno;
} AppStartupPathDecision;

ErrorCode app_startup_classify_path(const char *requested_path,
                                    AppStartupPathDecision *decision);

#endif
