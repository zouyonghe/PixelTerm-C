#include "input.h"
#include <termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/select.h>

// Create a new input handler
InputHandler* input_handler_create(void) {
    InputHandler *handler = g_new0(InputHandler, 1);
    if (!handler) {
        return NULL;
    }

    handler->raw_mode_enabled = FALSE;
    handler->mouse_enabled = FALSE;
    handler->terminal_width = 80;
    handler->terminal_height = 24;
    handler->should_exit = FALSE;

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
        handler->raw_mode_enabled = TRUE; // Pretend it's enabled for non-TTY
        return ERROR_NONE;
    }

    struct termios term;
    if (tcgetattr(STDIN_FILENO, &term) != 0) {
        return ERROR_TERMINAL_SIZE;
    }

    // Save original settings for restoration
    static struct termios orig_term;
    tcgetattr(STDIN_FILENO, &orig_term);

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
        struct termios term;
        if (tcgetattr(STDIN_FILENO, &term) == 0) {
            term.c_lflag |= (ICANON | ECHO);
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &term);
        }
    }

    handler->raw_mode_enabled = FALSE;
    return ERROR_NONE;
}

// Enable mouse tracking
ErrorCode input_enable_mouse(InputHandler *handler) {
    if (!handler || handler->mouse_enabled) {
        return ERROR_NONE;
    }

    // Enable mouse tracking (X11 style)
    printf("\033[?1000h");
    fflush(stdout);
    
    handler->mouse_enabled = TRUE;
    return ERROR_NONE;
}

// Disable mouse tracking
ErrorCode input_disable_mouse(InputHandler *handler) {
    if (!handler || !handler->mouse_enabled) {
        return ERROR_NONE;
    }

    // Disable mouse tracking
    printf("\033[?1000l");
    fflush(stdout);
    
    handler->mouse_enabled = FALSE;
    return ERROR_NONE;
}

// Clear screen
ErrorCode input_clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
    return ERROR_NONE;
}

// Read and parse input event
ErrorCode input_get_event(InputHandler *handler, InputEvent *event) {
    if (!handler || !event) {
        return ERROR_MEMORY_ALLOC;
    }

    // Initialize event
    memset(event, 0, sizeof(InputEvent));
    event->type = INPUT_KEY_PRESS;
    event->key_code = KEY_UNKNOWN;

    // Read first character
    gint c = input_read_key(handler);
    if (c < 0) {
        return ERROR_TERMINAL_SIZE;
    }

    // Handle escape sequences
    if (c == '\033') {
        // Check if it's an escape sequence or just ESC key
        if (input_has_pending_input(handler)) {
            // Read next character
            gint next = input_read_char(handler);
            
            if (next == '[') {
                // ANSI escape sequence
                gint seq[3] = {0};
                gint i = 0;
                
                // Read the sequence
                while (i < 3 && input_has_pending_input(handler)) {
                    seq[i++] = input_read_char(handler);
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
            } else {
                // Other escape sequence
                event->key_code = KEY_UNKNOWN;
            }
        } else {
            // Just ESC key
            event->key_code = KEY_ESCAPE;
        }
    } else {
        // Regular character
        event->key_code = (KeyCode)c;
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

    tcflush(STDIN_FILENO, TCIFLUSH);
    return ERROR_NONE;
}

// Read a key (handles escape sequences)
gint input_read_key(InputHandler *handler) {
    if (!handler) {
        return -1;
    }

    return input_read_char(handler);
}

// Check if a specific key is pressed
gboolean input_is_key_pressed(InputHandler *handler, KeyCode key) {
    if (!handler) {
        return FALSE;
    }

    if (!input_has_pending_input(handler)) {
        return FALSE;
    }

    InputEvent event;
    ErrorCode error = input_get_event(handler, &event);
    return (error == ERROR_NONE && event.key_code == key);
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

// Get terminal width
gint input_get_terminal_width(const InputHandler *handler) {
    return handler ? handler->terminal_width : 80;
}

// Get terminal height
gint input_get_terminal_height(const InputHandler *handler) {
    return handler ? handler->terminal_height : 24;
}

// Convert key code to string
const gchar* input_key_code_to_string(KeyCode key) {
    switch (key) {
        case KEY_ESCAPE: return "ESC";
        case KEY_ENTER: return "ENTER";
        case KEY_TAB: return "TAB";
        case KEY_BACKSPACE: return "BACKSPACE";
        case KEY_DELETE: return "DELETE";
        case KEY_UP: return "UP";
        case KEY_DOWN: return "DOWN";
        case KEY_LEFT: return "LEFT";
        case KEY_RIGHT: return "RIGHT";
        case KEY_HOME: return "HOME";
        case KEY_END: return "END";
        case KEY_PAGE_UP: return "PAGE_UP";
        case KEY_PAGE_DOWN: return "PAGE_DOWN";
        case KEY_F1: return "F1";
        case KEY_F2: return "F2";
        case KEY_F3: return "F3";
        case KEY_F4: return "F4";
        case KEY_F5: return "F5";
        case KEY_F6: return "F6";
        case KEY_F7: return "F7";
        case KEY_F8: return "F8";
        case KEY_F9: return "F9";
        case KEY_F10: return "F10";
        case KEY_F11: return "F11";
        case KEY_F12: return "F12";
        default:
            if (key >= 32 && key <= 126) {
                static gchar buf[2] = {0};
                buf[0] = (gchar)key;
                return buf;
            }
            return "UNKNOWN";
    }
}

// Check if key is for navigation
gboolean input_is_navigation_key(KeyCode key) {
    return (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN ||
            key == KEY_HOME || key == KEY_END || key == KEY_PAGE_UP || key == KEY_PAGE_DOWN ||
            key == 'a' || key == 'd');
}

// Check if key is for quit
gboolean input_is_quit_key(KeyCode key) {
    return (key == 'q' || key == KEY_ESCAPE);
}

// Print key bindings help
void input_print_key_bindings(void) {
    printf("\nKey Bindings:\n");
    printf("  ←/→ or a/d     Previous/Next image\n");
    printf("  i              Toggle image information\n");
    printf("  r              Delete current image\n");
    printf("  q              Quit application\n");
    printf("  Ctrl+C         Force exit\n");
    printf("  ESC            Quit application\n");
}