#define _GNU_SOURCE
#include <glib.h>

#include "input_dispatch.h"
#include "input_dispatch_core.h"
#include "input_dispatch_delete_internal.h"
#include "input_dispatch_key_modes_internal.h"
#include "input_dispatch_media_internal.h"
#include "input_dispatch_mouse_modes_internal.h"
#include "input_dispatch_pending_clicks_internal.h"
#include "common.h"

typedef void (*ModeKeyPressHandler)(PixelTermApp *app,
                                    InputHandler *input_handler,
                                    const InputEvent *event);
typedef void (*ModeMouseHandler)(PixelTermApp *app, const InputEvent *event);

typedef struct {
    ModeKeyPressHandler key_press;
    ModeMouseHandler mouse_press;
    ModeMouseHandler mouse_double_click;
    ModeMouseHandler mouse_scroll;
} ModeInputHandlers;

static void handle_mouse_press_book_toc(PixelTermApp *app, const InputEvent *event);
static void handle_mouse_double_click_book_toc(PixelTermApp *app, const InputEvent *event);
static const ModeInputHandlers* get_mode_input_handlers(const PixelTermApp *app);

static void app_pause_video_for_resize(PixelTermApp *app) {
    if (!app || !app->video_player) {
        return;
    }
    if (!input_dispatch_current_is_video(app)) {
        return;
    }
    if (video_player_is_playing(app->video_player)) {
        video_player_pause(app->video_player);
    }
}

static void handle_mouse_press_preview(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_press_preview(app, event);
}

static void handle_mouse_press_file_manager(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_press_file_manager(app, event);
}

static void handle_mouse_press_book_toc(PixelTermApp *app, const InputEvent *event) {
    gboolean redraw_needed = FALSE;
    app_handle_mouse_click_book_toc(app,
                                    event->mouse_x,
                                    event->mouse_y,
                                    &redraw_needed,
                                    NULL);
    if (redraw_needed) {
        app_render_book_toc(app);
    }
}

static void handle_mouse_press_single(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_press_single(app, event);
}

static void handle_mouse_press_book(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_press_book(app, event);
}

static void handle_mouse_press(PixelTermApp *app, const InputEvent *event) {
    if (app && app->book.toc_visible) {
        handle_mouse_press_book_toc(app, event);
        return;
    }
    const ModeInputHandlers *handlers = get_mode_input_handlers(app);
    if (handlers && handlers->mouse_press) {
        handlers->mouse_press(app, event);
    }
}

static void handle_mouse_double_click_preview(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_double_click_preview(app, event);
}

static void handle_mouse_double_click_book_preview(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_double_click_book_preview(app, event);
}

static void handle_mouse_double_click_file_manager(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_double_click_file_manager(app, event);
}

static void handle_mouse_double_click_book(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_double_click_book(app, event);
}

static void handle_mouse_double_click_book_toc(PixelTermApp *app, const InputEvent *event) {
    gboolean redraw_needed = FALSE;
    gboolean hit = FALSE;
    app_handle_mouse_click_book_toc(app,
                                    event->mouse_x,
                                    event->mouse_y,
                                    &redraw_needed,
                                    &hit);
    if (!hit) {
        return;
    }
    gint page = app_book_toc_get_selected_page(app);
    app->book.toc_visible = FALSE;
    if (page >= 0 && app_enter_book_page(app, page) == ERROR_NONE) {
        app_render_book_page(app);
    } else if (app_is_book_preview_mode(app)) {
        app_render_book_preview(app);
    } else {
        app_render_book_page(app);
    }
}

static void handle_mouse_double_click_single(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_double_click_single(app, event);
}

static void handle_mouse_double_click(PixelTermApp *app, const InputEvent *event) {
    if (app && app->book.toc_visible) {
        handle_mouse_double_click_book_toc(app, event);
        return;
    }
    const ModeInputHandlers *handlers = get_mode_input_handlers(app);
    if (handlers && handlers->mouse_double_click) {
        handlers->mouse_double_click(app, event);
    }
}

static void handle_mouse_scroll_preview(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_scroll_preview(app, event);
}

/* Moved to src/input_dispatch_book.c */

static void handle_mouse_scroll_book_preview(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_scroll_book_preview(app, event);
}

static void handle_mouse_scroll_file_manager(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_scroll_file_manager(app, event);
}

static void handle_mouse_scroll_single(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_scroll_single(app, event);
}

static void handle_mouse_scroll_book(PixelTermApp *app, const InputEvent *event) {
    input_dispatch_handle_mouse_scroll_book(app, event);
}

static void handle_mouse_scroll_book_toc(PixelTermApp *app, const InputEvent *event) {
    if (!app || !app->book.toc_visible) {
        return;
    }
    gint old_selected = app->book.toc_selected;
    gint old_scroll = app->book.toc_scroll;

    if (event->mouse_button == MOUSE_SCROLL_UP) {
        app_book_toc_move_selection(app, -1);
    } else if (event->mouse_button == MOUSE_SCROLL_DOWN) {
        app_book_toc_move_selection(app, 1);
    }

    if (app->book.toc_selected != old_selected || app->book.toc_scroll != old_scroll) {
        app_render_book_toc(app);
    }
}

static void handle_mouse_scroll(PixelTermApp *app, const InputEvent *event) {
    if (app && app->book.toc_visible) {
        handle_mouse_scroll_book_toc(app, event);
        return;
    }
    const ModeInputHandlers *handlers = get_mode_input_handlers(app);
    if (handlers && handlers->mouse_scroll) {
        handlers->mouse_scroll(app, event);
    }
}

static void drain_main_context_if_playing(gboolean is_playing) {
    if (!is_playing) {
        return;
    }

    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
}

static void process_animation_events(PixelTermApp *app) {
    if (!app) {
        return;
    }

    drain_main_context_if_playing(app->gif_player && gif_player_is_playing(app->gif_player));
    drain_main_context_if_playing(app->video_player && video_player_is_playing(app->video_player));
}

static gboolean handle_key_press_common(PixelTermApp *app,
                                        InputHandler *input_handler,
                                        const InputEvent *event) {
    switch (event->key_code) {
        case KEY_ESCAPE:
            app->running = FALSE;
            input_handler->should_exit = TRUE;
            return TRUE;
        case (KeyCode)'d':
        case (KeyCode)'D':
            if (!app_is_single_mode(app)) {
                return FALSE;
            }
            app->dither_enabled = !app->dither_enabled;
            if (app->preloader) {
                preloader_stop(app->preloader);
                preloader_cache_clear(app->preloader);
                preloader_initialize(app->preloader, app->dither_enabled, app->render_work_factor,
                                     app->force_text, app->force_sixel, app->force_kitty, app->force_iterm2,
                                     app->gamma);
                preloader_start(app->preloader);
            }
            app_render_by_mode(app);
            return TRUE;
        case (KeyCode)'i':
            if (input_dispatch_current_is_video(app) || app_is_book_mode(app) || app_is_book_preview_mode(app)) {
                return TRUE;
            }
            if (!app_is_preview_mode(app)) {
                if (app->ui_text_hidden) {
                    return TRUE;
                }
                if (app->info_visible) {
                    app->info_visible = FALSE;
                    app_render_current_image(app);
                } else {
                    app_display_image_info(app);
                }
            }
            return TRUE;
        case (KeyCode)'f':
        case (KeyCode)'F':
            input_dispatch_key_modes_toggle_video_fps(app);
            return TRUE;
        case (KeyCode)'~':
        case (KeyCode)'`':
            if (!app_is_file_manager_mode(app)) {
                gboolean info_was_visible = app->info_visible;
                app->ui_text_hidden = !app->ui_text_hidden;
                if (app->ui_text_hidden) {
                    app->info_visible = FALSE;
                }
                if (app_is_book_preview_mode(app)) {
                    app->suppress_full_clear = TRUE;
                    app->needs_screen_clear = FALSE;
                } else if (app_is_preview_mode(app)) {
                    app->needs_screen_clear = TRUE;
                } else if (!info_was_visible) {
                    app->suppress_full_clear = TRUE;
                }
                app_render_by_mode(app);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

static void handle_key_press_preview(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    input_dispatch_handle_key_press_preview(app, input_handler, event);
}

static void handle_key_press_book_preview(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    input_dispatch_handle_key_press_book_preview(app, input_handler, event);
}

static void handle_key_press_book(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    input_dispatch_handle_key_press_book(app, input_handler, event);
}

static void handle_key_press_file_manager(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    input_dispatch_handle_key_press_file_manager(app, input_handler, event);
}

static void handle_key_press_single(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    input_dispatch_handle_key_press_single(app, input_handler, event);
}

static const ModeInputHandlers* get_mode_input_handlers(const PixelTermApp *app) {
    static const ModeInputHandlers k_single = {
        .key_press = handle_key_press_single,
        .mouse_press = handle_mouse_press_single,
        .mouse_double_click = handle_mouse_double_click_single,
        .mouse_scroll = handle_mouse_scroll_single,
    };
    static const ModeInputHandlers k_preview = {
        .key_press = handle_key_press_preview,
        .mouse_press = handle_mouse_press_preview,
        .mouse_double_click = handle_mouse_double_click_preview,
        .mouse_scroll = handle_mouse_scroll_preview,
    };
    static const ModeInputHandlers k_file_manager = {
        .key_press = handle_key_press_file_manager,
        .mouse_press = handle_mouse_press_file_manager,
        .mouse_double_click = handle_mouse_double_click_file_manager,
        .mouse_scroll = handle_mouse_scroll_file_manager,
    };
    static const ModeInputHandlers k_book = {
        .key_press = handle_key_press_book,
        .mouse_press = handle_mouse_press_book,
        .mouse_double_click = handle_mouse_double_click_book,
        .mouse_scroll = handle_mouse_scroll_book,
    };
    static const ModeInputHandlers k_book_preview = {
        .key_press = handle_key_press_book_preview,
        .mouse_press = handle_mouse_press_preview,
        .mouse_double_click = handle_mouse_double_click_book_preview,
        .mouse_scroll = handle_mouse_scroll_book_preview,
    };

    if (!app) {
        return &k_single;
    }
    if (app_is_book_preview_mode(app)) {
        return &k_book_preview;
    }
    if (app_is_book_mode(app)) {
        return &k_book;
    }
    if (app_is_preview_mode(app)) {
        return &k_preview;
    }
    if (app_is_file_manager_mode(app)) {
        return &k_file_manager;
    }
    return &k_single;
}

static void handle_key_press(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    if (input_dispatch_handle_delete_request(app, event)) {
        return;
    }
    if (app && app->book.jump_active && (app_is_book_mode(app) || app_is_book_preview_mode(app))) {
        if (input_dispatch_key_modes_handle_book_jump_input(app, event)) {
            return;
        }
    }
    if (handle_key_press_common(app, input_handler, event)) {
        return;
    }
    const ModeInputHandlers *handlers = get_mode_input_handlers(app);
    if (handlers && handlers->key_press) {
        handlers->key_press(app, input_handler, event);
    }
}

static void handle_input_event(PixelTermApp *app, InputHandler *input_handler, const InputEvent *event) {
    if (!event) {
        return;
    }

    if (app) {
        if (event->type == INPUT_MOUSE_PRESS ||
            event->type == INPUT_MOUSE_DOUBLE_CLICK ||
            event->type == INPUT_MOUSE_SCROLL) {
            app->input.last_mouse_x = event->mouse_x;
            app->input.last_mouse_y = event->mouse_y;
        }
    }
    switch (event->type) {
        case INPUT_MOUSE_PRESS:
            handle_mouse_press(app, event);
            break;
        case INPUT_MOUSE_DOUBLE_CLICK:
            handle_mouse_double_click(app, event);
            break;
        case INPUT_MOUSE_SCROLL:
            handle_mouse_scroll(app, event);
            break;
        case INPUT_KEY_PRESS:
            handle_key_press(app, input_handler, event);
            break;
        default:
            break;
    }
}

// Main application loop

void input_dispatch_core_handle_event(PixelTermApp *app,
                                 InputHandler *input_handler,
                                 const InputEvent *event) {
    handle_input_event(app, input_handler, event);
}

void input_dispatch_core_process_pending(PixelTermApp *app) {
    input_dispatch_process_pending_clicks(app);
}

void input_dispatch_core_process_animations(PixelTermApp *app) {
    process_animation_events(app);
}

void input_dispatch_core_pause_video_for_resize(PixelTermApp *app) {
    app_pause_video_for_resize(app);
}
