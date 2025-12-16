#ifndef INPUT_H
#define INPUT_H

#include "common.h"
#include <termios.h>
#include <sys/time.h>

// Input event types
typedef enum {
    INPUT_KEY_PRESS,
    INPUT_KEY_RELEASE,

    INPUT_MOUSE_PRESS,
    INPUT_MOUSE_RELEASE,
    INPUT_MOUSE_DOUBLE_CLICK,
    INPUT_MOUSE_SCROLL,

    INPUT_RESIZE
} InputEventType;

// Mouse buttons
typedef enum {
    MOUSE_BUTTON_LEFT = 0,
    MOUSE_BUTTON_MIDDLE = 1,
    MOUSE_BUTTON_RIGHT = 2,
    MOUSE_SCROLL_UP = 64,
    MOUSE_SCROLL_DOWN = 65
} MouseButton;

// Key codes
typedef enum {
    KEY_UNKNOWN = 0,
    KEY_ESCAPE = 27,
    KEY_ENTER = 10,
    KEY_TAB = 9,
    KEY_BACKSPACE = 127,
    KEY_DELETE = 512,
    KEY_UP = 513,
    KEY_DOWN = 514,
    KEY_LEFT = 515,
    KEY_RIGHT = 516,
    KEY_HOME = 517,
    KEY_END = 518,
    KEY_PAGE_UP = 519,
    KEY_PAGE_DOWN = 520,
    KEY_F1 = 521,
    KEY_F2 = 522,
    KEY_F3 = 523,
    KEY_F4 = 524,
    KEY_F5 = 525,
    KEY_F6 = 526,
    KEY_F7 = 527,
    KEY_F8 = 528,
    KEY_F9 = 529,
    KEY_F10 = 530,
    KEY_F11 = 531,
    KEY_F12 = 532
} KeyCode;

// Input event structure
typedef struct {
    InputEventType type;
    KeyCode key_code;
    guint32 modifiers;  // SHIFT, CTRL, ALT flags

    // Mouse data
    MouseButton mouse_button;
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
    struct termios orig_termios;
    gboolean has_orig_termios;

    // Double-click tracking
    struct timeval last_click_time;
    gint last_click_x;
    gint last_click_y;
    MouseButton last_click_button;

    // Scroll debouncing
    struct timeval last_scroll_time;
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

gchar input_read_char(InputHandler *handler);
gchar input_read_char_with_timeout(InputHandler *handler, gint timeout_ms);

// Terminal size handling
ErrorCode input_update_terminal_size(InputHandler *handler);
gint input_get_terminal_width(const InputHandler *handler);
gint input_get_terminal_height(const InputHandler *handler);

// Utility functions
const gchar* input_key_code_to_string(KeyCode key);
// Check if key is for navigation
gboolean input_is_navigation_key(KeyCode key);
void input_print_key_bindings(void);

#endif // INPUT_H
