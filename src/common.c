#include "common.h"
#include <sys/ioctl.h>
#include <fcntl.h>

// Check if a file is an image based on its extension
gboolean is_image_file(const char *filename) {
    if (!filename) {
        return FALSE;
    }

    const char *ext = get_file_extension(filename);
    if (!ext) {
        // No extension, check by content
        return is_image_by_content(filename);
    }

    for (int i = 0; SUPPORTED_EXTENSIONS[i] != NULL; i++) {
        if (g_ascii_strcasecmp(ext, SUPPORTED_EXTENSIONS[i]) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

// Check if a file is an image by reading its magic numbers (for files without extensions)
gboolean is_image_by_content(const char *filepath) {
    if (!filepath) {
        return FALSE;
    }

    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return FALSE;
    }

    unsigned char header[16];
    size_t bytes_read = fread(header, 1, sizeof(header), file);
    fclose(file);

    if (bytes_read < 4) {
        return FALSE;
    }

    // JPEG (FF D8 FF)
    if (header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
        return TRUE;
    }

    // PNG (89 50 4E 47)
    if (bytes_read >= 8 && 
        header[0] == 0x89 && header[1] == 0x50 && 
        header[2] == 0x4E && header[3] == 0x47) {
        return TRUE;
    }

    // GIF (GIF87a or GIF89a)
    if (bytes_read >= 6 && 
        header[0] == 'G' && header[1] == 'I' && header[2] == 'F' &&
        header[3] == '8' && (header[4] == '7' || header[4] == '9') && 
        header[5] == 'a') {
        return TRUE;
    }

    // WebP (RIFF....WEBP)
    if (bytes_read >= 12 && 
        header[0] == 'R' && header[1] == 'I' && 
        header[2] == 'F' && header[3] == 'F' &&
        header[8] == 'W' && header[9] == 'E' && 
        header[10] == 'B' && header[11] == 'P') {
        return TRUE;
    }

    // BMP (BM)
    if (header[0] == 'B' && header[1] == 'M') {
        return TRUE;
    }

    // TIFF (II*\0 or MM\0*)
    if (bytes_read >= 4 && (
        (header[0] == 'I' && header[1] == 'I' && header[2] == '*' && header[3] == '\0') ||
        (header[0] == 'M' && header[1] == 'M' && header[2] == '\0' && header[3] == '*')
    )) {
        return TRUE;
    }

    return FALSE;
}

// Get file extension
const char* get_file_extension(const char *filename) {
    if (!filename) {
        return NULL;
    }

    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return NULL;
    }

    return dot;
}

// Check if file exists
gboolean file_exists(const char *path) {
    if (!path) {
        return FALSE;
    }

    struct stat st;
    return (stat(path, &st) == 0);
}

// Get file size
gint64 get_file_size(const char *path) {
    if (!path) {
        return -1;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }

    return st.st_size;
}

// Get file modification time
gint64 get_file_mtime(const char *path) {
    if (!path) {
        return -1;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }

    return st.st_mtime;
}

// Cleanup string helper
void cleanup_string(gchar **str) {
    if (str && *str) {
        g_free(*str);
        *str = NULL;
    }
}

// Cleanup GString helper
void cleanup_gstring(GString **str) {
    if (str && *str) {
        g_string_free(*str, TRUE);
        *str = NULL;
    }
}

// GString destroyer for GHashTable
void gstring_destroy(gpointer data) {
    if (data) {
        g_string_free((GString*)data, TRUE);
    }
}

// Terminal utilities
void get_terminal_size(gint *width, gint *height) {
    if (!width || !height) {
        return;
    }

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *width = ws.ws_col > 0 ? ws.ws_col : 80;
        *height = ws.ws_row > 0 ? ws.ws_row : 24;
    } else {
        *width = 80;
        *height = 24;
    }
}

// Enhanced terminal size detection including pixel dimensions
void get_terminal_size_pixels(gint *width, gint *height, gint *pixel_width, gint *pixel_height) {
    if (!width || !height) {
        return;
    }

    // Start with safe defaults
    *width = 80;
    *height = 24;
    if (pixel_width) *pixel_width = -1;
    if (pixel_height) *pixel_height = -1;

    // Upper bounds to filter out clearly bogus ioctl responses
    const gint pixel_extent_max = 8192 * 3;
    const gint cell_extent_px_max = 8192;

    struct winsize ws;
    gboolean have_winsz = FALSE;

    // Try stdout, then stderr, then stdin — mirrors chafa's order
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0
        || ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0
        || ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        have_winsz = TRUE;
    }

    // Fall back to the controlling TTY if all of the above failed (e.g. in pipes)
    if (!have_winsz) {
        const char *term_path = ctermid(NULL);
        if (term_path) {
            int fd = open(term_path, O_RDONLY);
            if (fd >= 0) {
                if (ioctl(fd, TIOCGWINSZ, &ws) == 0) {
                    have_winsz = TRUE;
                }
                close(fd);
            }
        }
    }

    if (have_winsz) {
        if (ws.ws_col > 0) *width = ws.ws_col;
        if (ws.ws_row > 0) *height = ws.ws_row;

        if (pixel_width && pixel_height) {
            gint xpx = ws.ws_xpixel;
            gint ypx = ws.ws_ypixel;

            // Sanity-check pixel values just like chafa does
            if (xpx > pixel_extent_max || ypx > pixel_extent_max
                || xpx <= 0 || ypx <= 0) {
                xpx = -1;
                ypx = -1;
            }

            *pixel_width = xpx;
            *pixel_height = ypx;
        }
    }
}

// Derive terminal cell geometry; falls back to 10x20 when pixel metrics are unavailable
void get_terminal_cell_geometry(gint *cell_width, gint *cell_height) {
    if (cell_width) *cell_width = 10;
    if (cell_height) *cell_height = 20;

    gint width = 0, height = 0, pixel_width = -1, pixel_height = -1;
    const gint cell_extent_px_max = 8192;

    get_terminal_size_pixels(&width, &height, &pixel_width, &pixel_height);

    if (pixel_width > 0 && pixel_height > 0 && width > 0 && height > 0) {
        gint cw = pixel_width / width;
        gint ch = pixel_height / height;

        if (cw > 0 && ch > 0 && cw < cell_extent_px_max && ch < cell_extent_px_max) {
            if (cell_width) *cell_width = cw;
            if (cell_height) *cell_height = ch;
        }
    }
}

// Calculate terminal cell aspect ratio from pixel dimensions
gdouble get_terminal_cell_aspect_ratio(void) {
    gint width, height, pixel_width, pixel_height;
    const gchar *konsole_ver = g_getenv("KONSOLE_VERSION");
    const gboolean is_konsole = (konsole_ver && *konsole_ver);
    const gdouble fallback = is_konsole ? 0.55 : 0.5; // Konsole tends to need a slightly wider glyph

    get_terminal_size_pixels(&width, &height, &pixel_width, &pixel_height);
    
    // If pixel dimensions are available and look sane, calculate aspect ratio
    if (pixel_width > 0 && pixel_height > 0 && width > 0 && height > 0) {
        gdouble pixel_width_per_cell = (gdouble)pixel_width / width;
        gdouble pixel_height_per_cell = (gdouble)pixel_height / height;

        // Guard against bogus terminal reports (e.g., zero or extreme values)
        if (pixel_width_per_cell > 0.0 && pixel_height_per_cell > 0.0 &&
            pixel_width_per_cell < 64.0 && pixel_height_per_cell < 64.0) {
            gdouble ratio = pixel_width_per_cell / pixel_height_per_cell;

            // Only accept reasonable ratios; otherwise fall back
            if (ratio > 0.25 && ratio < 4.0) {
                // Konsole sometimes reports a noticeably narrow ratio; gently widen it
                if (is_konsole && ratio < 0.6) {
                    return fallback;
                }
                return ratio;
            }
        }
    }
    
    // Fallback to default aspect ratio
    // Most terminals have character cells that are taller than they are wide
    return fallback;
}

// Error handling utilities
const gchar* error_code_to_string(ErrorCode error) {
    switch (error) {
        case ERROR_NONE: return "No error";
        case ERROR_FILE_NOT_FOUND: return "File not found";
        case ERROR_INVALID_IMAGE: return "Invalid image format";
        case ERROR_MEMORY_ALLOC: return "Memory allocation failed";
        case ERROR_CHAFA_INIT: return "Chafa initialization failed";
        case ERROR_THREAD_CREATE: return "Thread creation failed";
        case ERROR_TERMINAL_SIZE: return "Terminal size error";
        case ERROR_HELP_EXIT: return "Help requested";
        case ERROR_VERSION_EXIT: return "Version requested";
        case ERROR_INVALID_ARGS: return "Invalid arguments";
        default: return "Unknown error";
    }
}
