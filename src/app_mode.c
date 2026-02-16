#include "app.h"

#define APP_MODE_COUNT (APP_MODE_BOOK_PREVIEW + 1)
#define APP_MODE_ALL_MASK ((1u << APP_MODE_COUNT) - 1u)

static gboolean app_mode_is_valid(AppMode mode) {
    return mode >= APP_MODE_SINGLE && mode <= APP_MODE_BOOK_PREVIEW;
}

static gboolean app_mode_transition_allowed(AppMode from, AppMode to) {
    /*
     * Explicit transition table (mask per "from" mode).
     *
     * Guardrail note:
     * - To keep behavior unchanged during refactors, the current default is to
     *   allow all transitions between valid modes.
     * - Tighten this table only after we have mode-transition regression tests.
     */
    static const guint32 k_allowed_to_mask[APP_MODE_COUNT] = {
        [APP_MODE_SINGLE] = APP_MODE_ALL_MASK,
        [APP_MODE_PREVIEW] = APP_MODE_ALL_MASK,
        [APP_MODE_FILE_MANAGER] = APP_MODE_ALL_MASK,
        [APP_MODE_BOOK] = APP_MODE_ALL_MASK,
        [APP_MODE_BOOK_PREVIEW] = APP_MODE_ALL_MASK,
    };
    if (!app_mode_is_valid(from) || !app_mode_is_valid(to)) {
        return FALSE;
    }
    return (k_allowed_to_mask[from] & (1u << to)) != 0;
}

typedef void (*AppModeHook)(PixelTermApp *app);

static void app_mode_on_exit_single(PixelTermApp *app) {
    if (app) {
        app->input.single_click.pending = FALSE;
    }
}

static void app_mode_on_exit_preview(PixelTermApp *app) {
    if (app) {
        app->input.preview_click.pending = FALSE;
    }
}

static void app_mode_on_exit_file_manager(PixelTermApp *app) {
    if (app) {
        app->input.file_manager_click.pending = FALSE;
    }
}

static void app_mode_on_enter_single(PixelTermApp *app) {
    (void)app;
}

static void app_mode_on_enter_non_single(PixelTermApp *app) {
    if (!app) {
        return;
    }
    if (app->gif_player) {
        gif_player_stop(app->gif_player);
    }
    if (app->video_player) {
        video_player_stop(app->video_player);
    }
}

typedef struct {
    const char *name;
    AppModeHook on_enter;
    AppModeHook on_exit;
} AppModeDef;

static const AppModeDef k_modes[APP_MODE_COUNT] = {
    [APP_MODE_SINGLE] = {
        .name = "single",
        .on_enter = app_mode_on_enter_single,
        .on_exit = app_mode_on_exit_single,
    },
    [APP_MODE_PREVIEW] = {
        .name = "preview",
        .on_enter = app_mode_on_enter_non_single,
        .on_exit = app_mode_on_exit_preview,
    },
    [APP_MODE_FILE_MANAGER] = {
        .name = "file_manager",
        .on_enter = app_mode_on_enter_non_single,
        .on_exit = app_mode_on_exit_file_manager,
    },
    [APP_MODE_BOOK] = {
        .name = "book",
        .on_enter = app_mode_on_enter_non_single,
        .on_exit = app_mode_on_exit_single,
    },
    [APP_MODE_BOOK_PREVIEW] = {
        .name = "book_preview",
        .on_enter = app_mode_on_enter_non_single,
        .on_exit = app_mode_on_exit_preview,
    },
};

ErrorCode app_transition_mode(PixelTermApp *app, AppMode mode) {
    if (!app) {
        return ERROR_MEMORY_ALLOC;
    }
    if (!app_mode_is_valid(mode)) {
        return ERROR_INVALID_ARGS;
    }

    AppMode current = app->mode;
    if (!app_mode_is_valid(current)) {
        current = APP_MODE_SINGLE;
        app->mode = APP_MODE_SINGLE;
    }
    if (current == mode) {
        return ERROR_NONE;
    }
    if (!app_mode_transition_allowed(current, mode)) {
        return ERROR_INVALID_ARGS;
    }

    if (k_modes[current].on_exit) {
        k_modes[current].on_exit(app);
    }
    app->mode = mode;
    if (k_modes[mode].on_enter) {
        k_modes[mode].on_enter(app);
    }
    return ERROR_NONE;
}

void app_set_mode(PixelTermApp *app, AppMode mode) {
    if (!app || !app_mode_is_valid(mode)) {
        return;
    }
    app->mode = mode;
}
