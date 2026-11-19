#ifndef APP_PREVIEW_TRANSACTION_INTERNAL_H
#define APP_PREVIEW_TRANSACTION_INTERNAL_H

#include "app.h"

/* Filtering owns its input list; retain the original until the first render succeeds. */
static inline ErrorCode app_enter_preview_and_render(PixelTermApp *app) {
    if (!app) return ERROR_MEMORY_ALLOC;
    PixelTermApp staged = *app;
    staged.image_files = NULL;
    for (GList *file = app->image_files; file; file = file->next)
        staged.image_files = g_list_prepend(staged.image_files, g_strdup(file->data));
    staged.image_files = g_list_reverse(staged.image_files);
    staged.preview.selected_link = NULL;
    staged.preview.selected_link_index = -1;
    /* Keep the live preload queue untouched until output succeeds. */
    staged.preloader = NULL;
    staged.preload_enabled = FALSE;
    if (staged.return_to_mode == RETURN_MODE_PREVIEW_VIRTUAL)
        staged.return_to_mode = RETURN_MODE_PREVIEW;
    ErrorCode error = app_enter_preview(&staged);
    if (error == ERROR_NONE) error = app_render_preview_grid(&staged);
    if (error != ERROR_NONE) {
        g_list_free_full(staged.image_files, g_free);
        app->needs_redraw = TRUE;
        app->needs_screen_clear = TRUE;
        return error;
    }
    staged.preloader = app->preloader;
    staged.preload_enabled = app->preload_enabled;
    if (app->preloader && app->preload_enabled) preloader_clear_queue(app->preloader);
    g_list_free_full(app->image_files, g_free);
    *app = staged;
    return ERROR_NONE;
}

#endif
