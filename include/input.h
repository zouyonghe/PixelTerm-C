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
/**
 * @brief Creates a new `InputHandler` instance.
 * 
 * Allocates memory for a new `InputHandler` structure and initializes its
 * members to default values.
 * 
 * @return A pointer to the newly created `InputHandler` instance on success,
 *         or NULL if memory allocation fails.
 */
InputHandler* input_handler_create(void);
/**
 * @brief Destroys an `InputHandler` instance and frees associated resources.
 * 
 * Ensures that raw mode and mouse support are disabled before freeing the
 * `InputHandler` structure.
 * 
 * @param handler A pointer to the `InputHandler` instance to destroy.
 */
void input_handler_destroy(InputHandler *handler);
/**
 * @brief Initializes the `InputHandler` by saving the original terminal settings.
 * 
 * This function should be called before enabling raw mode or mouse support.
 * It stores the current terminal attributes so they can be restored later.
 * 
 * @param handler A pointer to the `InputHandler` instance to initialize.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if terminal
 *         settings cannot be read.
 */
ErrorCode input_handler_initialize(InputHandler *handler);

// Terminal mode management
/**
 * @brief Enables raw mode for terminal input.
 * 
 * In raw mode, input is read character by character without buffering
 * and special character handling (e.g., Ctrl+C for interrupt) is disabled.
 * This is essential for interactive terminal applications.
 * 
 * @param handler A pointer to the `InputHandler` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if terminal
 *         settings cannot be modified.
 */
ErrorCode input_enable_raw_mode(InputHandler *handler);
/**
 * @brief Disables raw mode and restores original terminal settings.
 * 
 * This function should be called to clean up terminal settings before the
 * application exits, ensuring the terminal behaves normally afterward.
 * 
 * @param handler A pointer to the `InputHandler` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if original
 *         terminal settings cannot be restored.
 */
ErrorCode input_disable_raw_mode(InputHandler *handler);
/**
 * @brief Enables mouse event reporting in the terminal.
 * 
 * Sends ANSI escape codes to activate mouse tracking, allowing the application
 * to receive mouse click and scroll events.
 * 
 * @param handler A pointer to the `InputHandler` instance.
 * @return `ERROR_NONE` on success.
 */
ErrorCode input_enable_mouse(InputHandler *handler);
/**
 * @brief Disables mouse event reporting in the terminal.
 * 
 * Sends ANSI escape codes to deactivate mouse tracking, restoring normal
 * terminal mouse behavior.
 * 
 * @param handler A pointer to the `InputHandler` instance.
 * @return `ERROR_NONE` on success.
 */
ErrorCode input_disable_mouse(InputHandler *handler);
/**
 * @brief Clears the terminal screen.
 * 
 * Sends an ANSI escape code to clear the entire terminal screen and moves
 * the cursor to the home position (top-left).
 * 
 * @return `ERROR_NONE` on success.
 */
ErrorCode input_clear_screen(void);

// Input processing
/**
 * @brief Reads the next available input event from the terminal.
 * 
 * This function blocks until an input event (key press, mouse event,
 * or terminal resize) is received. It parses the raw terminal input
 * into a structured `InputEvent`.
 * 
 * @param handler A pointer to the `InputHandler` instance.
 * @param event A pointer to an `InputEvent` structure where the parsed
 *              event data will be stored.
 * @return `ERROR_NONE` on successful event retrieval, or an appropriate
 *         `ErrorCode` if an error occurs during input reading.
 */
ErrorCode input_get_event(InputHandler *handler, InputEvent *event);
/**
 * @brief Checks if there is any pending input available from the terminal.
 * 
 * This function is non-blocking and can be used to poll for input without
 * waiting indefinitely.
 * 
 * @param handler A pointer to the `InputHandler` instance.
 * @return `TRUE` if there is pending input, `FALSE` otherwise.
 */
gboolean input_has_pending_input(InputHandler *handler);
/**
 * @brief Flushes the terminal input buffer.
 * 
 * Discards any unread input characters from the terminal's input buffer.
 * This can be useful to prevent old, unwanted input from affecting subsequent
 * operations.
 * 
 * @param handler A pointer to the `InputHandler` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         buffer cannot be flushed.
 */
ErrorCode input_flush_buffer(InputHandler *handler);

// Key reading functions
/**
 * @brief Reads a key press event from the terminal and returns its `KeyCode`.
 * 
 * This function attempts to read a key press from standard input. It handles
 * both standard characters and special keys (e.g., arrow keys, function keys)
 * by parsing ANSI escape sequences. This is a blocking call.
 * 
 * @param handler A pointer to the `InputHandler` instance.
 * @return The `KeyCode` of the pressed key, or `KEY_UNKNOWN` if an error
 *         occurs or the input cannot be parsed.
 */
gint input_read_key(InputHandler *handler);

/**
 * @brief Reads a single character from terminal input without a timeout.
 * 
 * This is a blocking call that waits indefinitely for a character to be available.
 * It's generally used when a character is expected immediately.
 * 
 * @param handler A pointer to the `InputHandler` instance.
 * @return The character read, or 0 if an error occurs.
 */
gchar input_read_char(InputHandler *handler);
/**
 * @brief Reads a single character from terminal input with a specified timeout.
 * 
 * This function waits for a character to be available for a maximum duration
 * specified by `timeout_ms`. It's non-blocking if no input arrives within
 * the timeout period.
 * 
 * @param handler A pointer to the `InputHandler` instance.
 * @param timeout_ms The maximum time (in milliseconds) to wait for input.
 * @return The character read, or 0 if no input is available within the timeout
 *         or an error occurs.
 */
gchar input_read_char_with_timeout(InputHandler *handler, gint timeout_ms);

// Terminal size handling
/**
 * @brief Updates the stored terminal dimensions within the `InputHandler`.
 * 
 * This function queries the system for the current terminal width and height
 * and updates the corresponding fields in the provided `InputHandler` instance.
 * 
 * @param handler A pointer to the `InputHandler` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         terminal size cannot be retrieved.
 */
ErrorCode input_update_terminal_size(InputHandler *handler);
/**
 * @brief Retrieves the stored terminal width from the `InputHandler`.
 * 
 * @param handler A pointer to the constant `InputHandler` instance.
 * @return The terminal width in characters, or 0 if the handler is NULL.
 */
gint input_get_terminal_width(const InputHandler *handler);
/**
 * @brief Retrieves the stored terminal height from the `InputHandler`.
 * 
 * @param handler A pointer to the constant `InputHandler` instance.
 * @return The terminal height in characters, or 0 if the handler is NULL.
 */
gint input_get_terminal_height(const InputHandler *handler);

// Utility functions
/**
 * @brief Converts a `KeyCode` enum value to a human-readable string.
 * 
 * @param key The `KeyCode` value to convert.
 * @return A constant string representation of the key code, or "Unknown Key"
 *         if the code is not recognized. The returned string should not be freed.
 */
const gchar* input_key_code_to_string(KeyCode key);
/**
 * @brief Checks if a given `KeyCode` corresponds to a navigation key.
 * 
 * Navigation keys typically include arrow keys, page up/down, and `h`, `j`, `k`, `l`
 * for Vim-style navigation.
 * 
 * @param key The `KeyCode` to check.
 * @return `TRUE` if the key is a navigation key, `FALSE` otherwise.
 */
gboolean input_is_navigation_key(KeyCode key);
/**
 * @brief Prints a list of key bindings and their corresponding actions to standard output.
 * 
 * This function provides a user-friendly overview of available keyboard shortcuts
 * and mouse controls within the application.
 */
void input_print_key_bindings(void);

#endif // INPUT_H
