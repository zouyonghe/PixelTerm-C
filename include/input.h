#ifndef INPUT_H
#define INPUT_H

#include "common.h"

// Input event types
typedef enum {
    INPUT_KEY_PRESS,
    INPUT_KEY_RELEASE,
    INPUT_MOUSE_PRESS,
    INPUT_MOUSE_RELEASE,
    INPUT_MOUSE_MOVE,
    INPUT_RESIZE
} InputEventType;

// Key codes
typedef enum {
    KEY_UNKNOWN = -1,
    KEY_ESCAPE = APP_KEY_ESCAPE,
    KEY_ENTER = APP_KEY_ENTER,
    KEY_TAB = APP_KEY_TAB,
    KEY_BACKSPACE = APP_KEY_BACKSPACE,
    KEY_DELETE = APP_KEY_DELETE,
    KEY_UP = APP_KEY_UP,
    KEY_DOWN = APP_KEY_DOWN,
    KEY_LEFT = APP_KEY_LEFT,
    KEY_RIGHT = APP_KEY_RIGHT,
    KEY_HOME = APP_KEY_HOME,
    KEY_END = APP_KEY_END,
    KEY_PAGE_UP = APP_KEY_PAGE_UP,
    KEY_PAGE_DOWN = APP_KEY_PAGE_DOWN,
    KEY_F1 = APP_KEY_F1,
    KEY_F2 = APP_KEY_F2,
    KEY_F3 = APP_KEY_F3,
    KEY_F4 = APP_KEY_F4,
    KEY_F5 = APP_KEY_F5,
    KEY_F6 = APP_KEY_F6,
    KEY_F7 = APP_KEY_F7,
    KEY_F8 = APP_KEY_F8,
    KEY_F9 = APP_KEY_F9,
    KEY_F10 = APP_KEY_F10,
    KEY_F11 = APP_KEY_F11,
    KEY_F12 = APP_KEY_F12
} KeyCode;

// Input event structure
typedef struct {
    InputEventType type;
    KeyCode key_code;
    guint32 modifiers;  // SHIFT, CTRL, ALT flags
    gint mouse_x;
    gint mouse_y;
    gint terminal_width;
    gint terminal_height;
} InputEvent;

// Input handler structure
typedef struct {
    gboolean raw_mode_enabled;
    gboolean mouse_enabled;
    gint terminal_width;
    gint terminal_height;
    gboolean should_exit;
} InputHandler;

// Input handler lifecycle
InputHandler* input_handler_create(void);
void input_handler_destroy(InputHandler *handler);
ErrorCode input_handler_initialize(InputHandler *handler);

// Terminal mode management
ErrorCode input_enable_raw_mode(InputHandler *handler);
ErrorCode input_disable_raw_mode(InputHandler *handler);
ErrorCode input_enable_mouse(InputHandler *handler);
ErrorCode input_disable_mouse(InputHandler *handler);
ErrorCode input_clear_screen(void);

// Input processing
ErrorCode input_get_event(InputHandler *handler, InputEvent *event);
gboolean input_has_pending_input(InputHandler *handler);
ErrorCode input_flush_buffer(InputHandler *handler);

// Key reading functions
gint input_read_key(InputHandler *handler);
gboolean input_is_key_pressed(InputHandler *handler, KeyCode key);
gchar input_read_char(InputHandler *handler);

// Terminal size handling
ErrorCode input_update_terminal_size(InputHandler *handler);
gint input_get_terminal_width(const InputHandler *handler);
gint input_get_terminal_height(const InputHandler *handler);

// Utility functions
const gchar* input_key_code_to_string(KeyCode key);
gboolean input_is_navigation_key(KeyCode key);
gboolean input_is_quit_key(KeyCode key);
void input_print_key_bindings(void);

#endif // INPUT_H