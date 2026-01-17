#include "input.h"
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <string.h>
#include <stdlib.h>

// Create a new input handler
InputHandler* input_handler_create(void) {
    InputHandler *handler = g_new0(InputHandler, 1);
    if (!handler) {
        return NULL;
    }

    handler->raw_mode_enabled = FALSE;
    handler->mouse_enabled = FALSE;
    handler->use_alt_screen = TRUE;
    handler->alt_screen_enabled = FALSE;
    handler->terminal_width = 80;
    handler->terminal_height = 24;
    handler->should_exit = FALSE;
    handler->has_orig_termios = FALSE;
    handler->last_scroll_button = (MouseButton)0;
    handler->last_scroll_x = 0;
    handler->last_scroll_y = 0;
    handler->has_pending_event = FALSE;

    return handler;
}

// Destroy input handler
void input_handler_destroy(InputHandler *handler) {
    if (!handler) {
        return;
    }

    // Ensure terminal is restored to normal mode
    if (handler->raw_mode_enabled) {
        input_disable_raw_mode(handler);
    }

    g_free(handler);
}

// Initialize input handler
ErrorCode input_handler_initialize(InputHandler *handler) {
    if (!handler) {
        return ERROR_MEMORY_ALLOC;
    }

    // Get initial terminal size
    ErrorCode error = input_update_terminal_size(handler);
    if (error != ERROR_NONE) {
        return error;
    }

    return ERROR_NONE;
}

// Enable raw terminal mode
ErrorCode input_enable_raw_mode(InputHandler *handler) {
    if (!handler || handler->raw_mode_enabled) {
        return ERROR_NONE;
    }

    // Check if stdin is a terminal
    if (!isatty(STDIN_FILENO)) {
        return ERROR_NONE;
    }

    struct termios term;
    if (tcgetattr(STDIN_FILENO, &term) != 0) {
        return ERROR_TERMINAL_SIZE;
    }

    // Save original settings for restoration
    handler->orig_termios = term;
    handler->has_orig_termios = TRUE;

    // Modify terminal settings for raw mode but keep ISIG for Ctrl+C
    term.c_lflag &= ~(ICANON | ECHO | IEXTEN);
    term.c_lflag |= ISIG; // Keep signal handling for Ctrl+C
    term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    term.c_cflag |= CS8;
    // Keep OPOST enabled for proper text formatting and alignment
    // term.c_oflag &= ~OPOST;  // Commented out to allow proper text formatting
    term.c_cc[VMIN] = 1;  // Minimum number of characters for non-canonical read
    term.c_cc[VTIME] = 0; // Timeout in deciseconds for non-canonical read

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &term) != 0) {
        return ERROR_TERMINAL_SIZE;
    }

    // Hide cursor for TUI; optionally enter alternate screen (if enabled).
    if (isatty(STDOUT_FILENO)) {
        if (handler->use_alt_screen) {
            printf("\033[?1049h");
            handler->alt_screen_enabled = TRUE;
        }
        printf("\033[?25l");
        fflush(stdout);
    }

    handler->raw_mode_enabled = TRUE;
    return ERROR_NONE;
}

// Disable raw terminal mode
ErrorCode input_disable_raw_mode(InputHandler *handler) {
    if (!handler || !handler->raw_mode_enabled) {
        return ERROR_NONE;
    }

    // Only restore if stdin is a terminal
    if (isatty(STDIN_FILENO)) {
        if (handler->has_orig_termios) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &handler->orig_termios);
        } else {
            struct termios term;
            if (tcgetattr(STDIN_FILENO, &term) == 0) {
                term.c_lflag |= (ICANON | ECHO);
                tcsetattr(STDIN_FILENO, TCSAFLUSH, &term);
            }
        }
    }

    // Restore main screen (if we switched), then show cursor
    if (handler->alt_screen_enabled && isatty(STDOUT_FILENO)) {
        printf("\033[?1049l");
        handler->alt_screen_enabled = FALSE;
    }
    printf("\033[0m\033[?25h");
    fflush(stdout);

    handler->raw_mode_enabled = FALSE;
    return ERROR_NONE;
}

// Enable mouse tracking
ErrorCode input_enable_mouse(InputHandler *handler) {
    if (!handler || handler->mouse_enabled) {
        return ERROR_NONE;
    }

    if (!isatty(STDOUT_FILENO)) {
        return ERROR_NONE;
    }

    // Enable mouse tracking with button event tracking (supports scroll wheel)
    printf("\033[?1002;1006h");
    fflush(stdout);

    handler->mouse_enabled = TRUE;
    return ERROR_NONE;
}

// Disable mouse tracking
ErrorCode input_disable_mouse(InputHandler *handler) {
    if (!handler || !handler->mouse_enabled) {
        return ERROR_NONE;
    }

    if (!isatty(STDOUT_FILENO)) {
        handler->mouse_enabled = FALSE;
        return ERROR_NONE;
    }

    // Disable mouse tracking
    printf("\033[?1002;1006l");
    fflush(stdout);

    handler->mouse_enabled = FALSE;
    return ERROR_NONE;
}

// Read and parse input event
ErrorCode input_get_event(InputHandler *handler, InputEvent *event) {
    if (!handler || !event) {
        return ERROR_MEMORY_ALLOC;
    }

    if (handler->has_pending_event) {
        *event = handler->pending_event;
        handler->has_pending_event = FALSE;
        return ERROR_NONE;
    }

    // Initialize event
    memset(event, 0, sizeof(InputEvent));
    event->type = INPUT_KEY_PRESS;
    event->key_code = KEY_UNKNOWN;

    // Read first character (treat as unsigned byte to avoid sign-extension issues)
    gint c = input_read_key(handler);
    if (c < 0) {
        c = (unsigned char)c;
    }
    if (c < 0) {
        return ERROR_TERMINAL_SIZE;
    }

    // Handle escape sequences
    if (c == '\033') {
        // Check if it's an escape sequence or just ESC key
        // We use a timeout to wait for potential following characters
        gint next = input_read_char_with_timeout(handler, 50);

        if (next != 0) {
            // We have a character following ESC
            if (next == '[' || next == 'O') {
                // ANSI escape sequence or Application Cursor Keys
                gchar buffer[32] = {0};
                gint buf_idx = 0;
                gchar terminator = 0;

                // Read the sequence until we hit a terminator
                while (buf_idx < sizeof(buffer) - 1) {
                    gchar ch = input_read_char_with_timeout(handler, 50);
                    if (ch != 0) {
                        buffer[buf_idx++] = ch;
                        // Stop reading if we hit a terminator (letter, ~, M, m)
                        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '~' || ch == 'M' || ch == 'm') {
                            terminator = ch;
                            break;
                        }
                    } else {
                        break; // No more data available
                    }
                }

                // Parse mouse events first (terminator M or m)
                if (terminator == 'M' || terminator == 'm') {
                    // Mouse event: \033[<button>;<x>;<y>M or \033[<button>;<x>;<y>m (SGR)
                    // Or older format: \033[M... (but we look for terminator M/m so likely SGR or URXVT)
                    
                    gchar *params[3] = {0};
                    gint param_count = 0;
                    gchar *token = strtok(buffer, ";");

                    while (token && param_count < 3) {
                        params[param_count++] = token;
                        token = strtok(NULL, ";");
                    }

                    if (param_count >= 3) {
                        // Skip '<' if present in first parameter (SGR format)
                        gchar *btn_str = params[0];
                        if (btn_str[0] == '<') btn_str++;
                        
                        gint button = atoi(btn_str);
                        gint x = atoi(params[1]);
                        gint y = atoi(params[2]);

                        event->mouse_button = (MouseButton)button;
                        event->mouse_x = x;
                        event->mouse_y = y;

                        if (button >= 64) {
                            // Scroll event
                            // Only handle Press ('M'), ignore Release ('m') for scroll
                            if (terminator == 'M') {
                                struct timeval now;
                                gettimeofday(&now, NULL);
                                glong diff_ms = (now.tv_sec - handler->last_scroll_time.tv_sec) * 1000 + 
                                              (now.tv_usec - handler->last_scroll_time.tv_usec) / 1000;

                                // Debounce: many terminals emit multiple scroll events per wheel notch.
                                // Filter very fast duplicates (especially for preview page scrolling).
                                const glong debounce_ms = 150;
                                gboolean is_fast_duplicate = (diff_ms >= 0 && diff_ms < debounce_ms &&
                                                             handler->last_scroll_button == (MouseButton)button);
                                if (!is_fast_duplicate) {
                                    event->type = INPUT_MOUSE_SCROLL;
                                    handler->last_scroll_time = now;
                                    handler->last_scroll_button = (MouseButton)button;
                                    handler->last_scroll_x = x;
                                    handler->last_scroll_y = y;
                                } else {
                                    // Treat as ignored/release to avoid double processing
                                    event->type = INPUT_MOUSE_RELEASE;
                                }
                            } else {
                                event->type = INPUT_MOUSE_RELEASE;
                            }
                        } else if (terminator == 'M') {
                            // Press event
                            event->type = INPUT_MOUSE_PRESS;
                            
                            // Check for double click
                            struct timeval now;
                            gettimeofday(&now, NULL);
                            
                            glong diff_ms = (now.tv_sec - handler->last_click_time.tv_sec) * 1000 + 
                                          (now.tv_usec - handler->last_click_time.tv_usec) / 1000;
                            
                            // Threshold: 400ms and same position (or very close)
                            if (diff_ms < 400 && 
                                abs(x - handler->last_click_x) <= 1 && 
                                abs(y - handler->last_click_y) <= 0 &&
                                button == handler->last_click_button) {
                                
                                event->type = INPUT_MOUSE_DOUBLE_CLICK;
                                // Reset last click to avoid triple->double
                                handler->last_click_time.tv_sec = 0;
                            } else {
                                // Update last click
                                handler->last_click_time = now;
                                handler->last_click_x = x;
                                handler->last_click_y = y;
                                handler->last_click_button = (MouseButton)button;
                            }
                        } else {
                            // Release event
                            event->type = INPUT_MOUSE_RELEASE;
                        }
                    } else {
                        event->type = INPUT_KEY_PRESS;
                        event->key_code = KEY_UNKNOWN;
                    }
                } else {
                    // Handle other ANSI sequences
                    // Parse the buffer as before
                    gint seq[3] = {0};
                    gint i = 0;
                    for (gint j = 0; j < buf_idx && i < 3; j++) {
                        if (buffer[j] != ';') {
                            seq[i++] = buffer[j];
                        }
                    }

                    // Parse specific sequences
                    if (i == 1) {
                        switch (seq[0]) {
                            case 'A': event->key_code = KEY_UP; break;
                            case 'B': event->key_code = KEY_DOWN; break;
                            case 'C': event->key_code = KEY_RIGHT; break;
                            case 'D': event->key_code = KEY_LEFT; break;
                            case 'H': event->key_code = KEY_HOME; break;
                            case 'F': event->key_code = KEY_END; break;
                            default: event->key_code = KEY_UNKNOWN; break;
                        }
                    } else if (i == 2 && seq[0] == '5' && seq[1] == '~') {
                        event->key_code = KEY_PAGE_UP;
                    } else if (i == 2 && seq[0] == '6' && seq[1] == '~') {
                        event->key_code = KEY_PAGE_DOWN;
                    } else if (i == 3 && seq[0] == '1' && seq[1] == '5' && seq[2] == '~') {
                        event->key_code = KEY_F5;
                    } else if (i == 3 && seq[2] >= 'A' && seq[2] <= 'D') {
                        // Arrow keys with modifiers
                        switch (seq[2]) {
                            case 'A': event->key_code = KEY_UP; break;
                            case 'B': event->key_code = KEY_DOWN; break;
                            case 'C': event->key_code = KEY_RIGHT; break;
                            case 'D': event->key_code = KEY_LEFT; break;
                            default: event->key_code = KEY_UNKNOWN; break;
                        }
                        // Parse modifiers from seq[1]
                        if (seq[1] >= '2' && seq[1] <= '8') {
                            event->modifiers = seq[1] - '1';
                        }
                    }
                }
            } else {
                // Other escape sequence (ESC + something else)
                event->key_code = KEY_UNKNOWN;
            }
        } else {
            // Just ESC key (timeout reached without new data)
            event->key_code = KEY_ESCAPE;
        }
    } else {
        // Regular character (including basic UTF-8 punctuation we care about)
        // Some IMEs/terminals send a fullwidth tilde '～' (U+FF5E) which is UTF-8: EF BD 9E.
        if (c == 0xEF) {
            gint b2 = input_read_char_with_timeout(handler, 5);
            if (b2 < 0) b2 = (unsigned char)b2;
            gint b3 = input_read_char_with_timeout(handler, 5);
            if (b3 < 0) b3 = (unsigned char)b3;
            if (b2 == 0xBD && b3 == 0x9E) {
                event->key_code = (KeyCode)'~';
            } else {
                event->key_code = KEY_UNKNOWN;
            }
        } else {
            event->key_code = (KeyCode)c;
        }
    }

    // Update terminal size in event
    event->terminal_width = handler->terminal_width;
    event->terminal_height = handler->terminal_height;

    return ERROR_NONE;
}

// Check if there's pending input
gboolean input_has_pending_input(InputHandler *handler) {
    if (!handler) {
        return FALSE;
    }
    if (handler->has_pending_event) {
        return TRUE;
    }

    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

// Flush input buffer
ErrorCode input_flush_buffer(InputHandler *handler) {
    if (!handler) {
        return ERROR_MEMORY_ALLOC;
    }

    handler->has_pending_event = FALSE;
    tcflush(STDIN_FILENO, TCIFLUSH);
    return ERROR_NONE;
}

ErrorCode input_unget_event(InputHandler *handler, const InputEvent *event) {
    if (!handler || !event) {
        return ERROR_MEMORY_ALLOC;
    }
    if (handler->has_pending_event) {
        return ERROR_NONE;
    }

    handler->pending_event = *event;
    handler->has_pending_event = TRUE;
    return ERROR_NONE;
}

// Read a key (handles escape sequences)
gint input_read_key(InputHandler *handler) {
    if (!handler) {
        return -1;
    }

    return input_read_char(handler);
}



// Read a single character
gchar input_read_char(InputHandler *handler) {
    if (!handler) {
        return 0;
    }

    gchar c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    return (n == 1) ? c : 0;
}

// Read a single character with timeout (in milliseconds)
gchar input_read_char_with_timeout(InputHandler *handler, gint timeout_ms) {
    if (!handler) {
        return 0;
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    
    int result = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (result <= 0) {
        return 0; // Timeout or error
    }
    
    if (FD_ISSET(STDIN_FILENO, &fds)) {
        gchar c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        return (n == 1) ? c : 0;
    }
    
    return 0;
}

static gboolean input_response_has_sixel(const char *buffer) {
    if (!buffer) {
        return FALSE;
    }

    const char *cursor = strchr(buffer, '?');
    if (!cursor) {
        cursor = strchr(buffer, '[');
    }
    if (!cursor) {
        cursor = buffer;
    }

    for (; *cursor != '\0'; cursor++) {
        if (!g_ascii_isdigit(*cursor)) {
            continue;
        }

        gint value = 0;
        while (g_ascii_isdigit(*cursor)) {
            value = value * 10 + (*cursor - '0');
            cursor++;
        }

        if (value == 4) {
            return TRUE;
        }

        if (*cursor == '\0') {
            break;
        }
    }

    return FALSE;
}

gboolean input_probe_sixel_support(InputHandler *handler, gint timeout_ms) {
    if (!handler || timeout_ms <= 0) {
        return FALSE;
    }

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return FALSE;
    }

    input_flush_buffer(handler);

    const char query[] = "\033[c";
    (void)write(STDOUT_FILENO, query, sizeof(query) - 1);

    gint64 deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;
    char buffer[128];
    gint length = 0;

    while (g_get_monotonic_time() < deadline && length < (gint)(sizeof(buffer) - 1)) {
        gint ch = input_read_char_with_timeout(handler, 20);
        if (ch == 0) {
            continue;
        }
        buffer[length++] = (char)ch;
        if (ch == 'c') {
            break;
        }
    }

    buffer[length] = '\0';
    if (length == 0) {
        return FALSE;
    }

    return input_response_has_sixel(buffer);
}

// Update terminal size
ErrorCode input_update_terminal_size(InputHandler *handler) {
    if (!handler) {
        return ERROR_MEMORY_ALLOC;
    }

    // Check if stdout is a terminal
    if (!isatty(STDOUT_FILENO)) {
        handler->terminal_width = 80;
        handler->terminal_height = 24;
        return ERROR_NONE;
    }

    get_terminal_size(&handler->terminal_width, &handler->terminal_height);
    return ERROR_NONE;
}
