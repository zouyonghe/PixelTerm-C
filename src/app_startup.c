#include "app_startup.h"

#include <errno.h>

static ErrorCode validate_path(const char *path, gboolean *is_directory, gint *failure_errno) {
    if (!path) {
        return ERROR_FILE_NOT_FOUND;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        if (failure_errno) {
            *failure_errno = errno;
        }
        return ERROR_FILE_NOT_FOUND;
    }

    *is_directory = S_ISDIR(st.st_mode);
    return ERROR_NONE;
}

ErrorCode app_startup_classify_path(const char *requested_path,
                                    AppStartupPathDecision *decision) {
    g_autofree gchar *current_dir = NULL;
    gboolean is_directory = FALSE;
    gint failure_errno = 0;
    const char *path = requested_path;

    if (!decision) {
        return ERROR_INVALID_ARGS;
    }

    decision->kind = APP_STARTUP_PATH_DIRECTORY;
    decision->path = NULL;
    decision->failure_errno = 0;

    if (!path) {
        current_dir = g_get_current_dir();
        path = current_dir;
    }

    ErrorCode error = validate_path(path, &is_directory, &failure_errno);
    if (error != ERROR_NONE) {
        decision->failure_errno = failure_errno;
        return error;
    }

    if (is_directory) {
        decision->kind = APP_STARTUP_PATH_DIRECTORY;
        decision->path = g_strdup(path);
        return ERROR_NONE;
    }

    if (is_valid_book_file(path)) {
        decision->kind = APP_STARTUP_PATH_BOOK;
        decision->path = g_strdup(path);
        return ERROR_NONE;
    }

    if (is_valid_media_file(path)) {
        decision->kind = APP_STARTUP_PATH_MEDIA;
        decision->path = g_strdup(path);
        return ERROR_NONE;
    }

    return ERROR_INVALID_IMAGE;
}
