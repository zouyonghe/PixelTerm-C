#ifndef APP_STARTUP_H
#define APP_STARTUP_H

#include <glib.h>

#include "common.h"

typedef enum {
    APP_STARTUP_PATH_DIRECTORY,
    APP_STARTUP_PATH_BOOK,
    APP_STARTUP_PATH_MEDIA,
    APP_STARTUP_PATH_PARENT_DIRECTORY,
} AppStartupPathKind;

typedef struct {
    AppStartupPathKind kind;
    gchar *path;
} AppStartupPathDecision;

ErrorCode app_startup_classify_path(const char *requested_path,
                                    AppStartupPathDecision *decision);

#endif
