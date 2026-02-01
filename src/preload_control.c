#include "preload_control.h"

#include "preloader.h"

void app_preloader_reset(PixelTermApp *app) {
    if (!app || !app->preloader) {
        return;
    }

    preloader_stop(app->preloader);
    preloader_destroy(app->preloader);
    app->preloader = NULL;
}

ErrorCode app_preloader_enable(PixelTermApp *app, gboolean queue_tasks) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app->preload_enabled) {
        return ERROR_NONE;
    }

    gboolean created = FALSE;
    if (!app->preloader) {
        app->preloader = preloader_create();
        if (!app->preloader) {
            return ERROR_MEMORY_ALLOC;
        }
        preloader_initialize(app->preloader, app->dither_enabled, app->render_work_factor,
                             app->force_text, app->force_sixel, app->force_kitty, app->force_iterm2,
                             app->gamma);
        created = TRUE;
    }

    preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);

    if (created) {
        ErrorCode preload_err = preloader_start(app->preloader);
        if (preload_err != ERROR_NONE) {
            preloader_destroy(app->preloader);
            app->preloader = NULL;
            app->preload_enabled = FALSE;
            return preload_err;
        }
    } else {
        preloader_enable(app->preloader);
        preloader_resume(app->preloader);
    }

    if (queue_tasks) {
        app_preloader_queue_directory(app);
    }

    return ERROR_NONE;
}

void app_preloader_disable(PixelTermApp *app) {
    if (!app || !app->preloader) {
        return;
    }

    preloader_disable(app->preloader);
    preloader_clear_queue(app->preloader);
}

void app_preloader_clear_queue(PixelTermApp *app) {
    if (!app || !app->preloader || !app->preload_enabled) {
        return;
    }
    preloader_clear_queue(app->preloader);
}

void app_preloader_queue_directory(PixelTermApp *app) {
    if (!app || !app->preloader || !app->preload_enabled || !app_has_images(app)) {
        return;
    }

    gint target_width = 0, target_height = 0;
    app_get_image_target_dimensions(app, &target_width, &target_height);
    preloader_clear_queue(app->preloader);
    preloader_add_tasks_for_directory(app->preloader, app->image_files,
                                      app->current_index, target_width, target_height);
}

void app_preloader_update_terminal(PixelTermApp *app) {
    if (!app || !app->preloader || !app->preload_enabled) {
        return;
    }
    preloader_update_terminal_size(app->preloader, app->term_width, app->term_height);
}
