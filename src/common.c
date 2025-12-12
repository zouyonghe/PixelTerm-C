#include "common.h"
#include <sys/ioctl.h>

// Check if a file is an image based on its extension
gboolean is_image_file(const char *filename) {
    if (!filename) {
        return FALSE;
    }

    const char *ext = get_file_extension(filename);
    if (!ext) {
        return FALSE;
    }

    for (int i = 0; SUPPORTED_EXTENSIONS[i] != NULL; i++) {
        if (g_ascii_strcasecmp(ext, SUPPORTED_EXTENSIONS[i]) == 0) {
            return TRUE;
        }
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
        default: return "Unknown error";
    }
}