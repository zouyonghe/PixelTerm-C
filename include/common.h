#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <glib.h>
#include <chafa.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <stdbool.h>

// Application constants
#define APP_NAME "PixelTerm-C"
#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif
#define MAX_PATH_LEN 4096
#define MAX_CACHE_SIZE 50
#define PRELOAD_QUEUE_SIZE 10

// Supported image formats
static const char* SUPPORTED_EXTENSIONS[] = {
    ".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp", ".tiff", ".tif", NULL
};

// Key codes are defined in input.h as KeyCode enum

// Error codes
typedef enum {
    ERROR_NONE = 0,
    ERROR_FILE_NOT_FOUND,
    ERROR_INVALID_IMAGE,
    ERROR_MEMORY_ALLOC,
    ERROR_CHAFA_INIT,
    ERROR_THREAD_CREATE,
    ERROR_TERMINAL_SIZE,
    ERROR_HELP_EXIT,
    ERROR_VERSION_EXIT,
    ERROR_INVALID_ARGS
} ErrorCode;

// Image information structure
typedef struct {
    gchar *filepath;
    gchar *filename;
    gint width;
    gint height;
    gint file_size;
    gchar *format;
    gint64 modification_time;
} ImageInfo;



// Function declarations
/**
 * @brief Checks if a file is an image based on its file extension.
 * 
 * Compares the file's extension against a list of supported image extensions.
 * If no extension is found, it falls back to `is_image_by_content` to check
 * the file's magic numbers.
 * 
 * @param filename The path to the file.
 * @return `TRUE` if the file is identified as an image, `FALSE` otherwise.
 */
gboolean is_image_file(const char *filename);

/**
 * @brief Checks if a file is an image by reading its magic numbers.
 * 
 * This function attempts to identify common image formats (JPEG, PNG, GIF,
 * WebP, BMP, TIFF) by inspecting the initial bytes (magic numbers) of the file.
 * This is useful for files without a recognizable extension.
 * 
 * @param filepath The path to the file.
 * @return `TRUE` if the file's content suggests it's a supported image format, `FALSE` otherwise.
 */
gboolean is_image_by_content(const char *filepath);

/**
 * @brief Performs a comprehensive check to determine if a file is a valid image.
 * 
 * This function verifies if the file exists, has a non-zero size, and
 * its content (magic numbers) indicates it's a supported image format.
 * It combines checks from `file_exists`, `get_file_size`, and `is_image_by_content`.
 * 
 * @param filepath The path to the file.
 * @return `TRUE` if the file is deemed a valid and supported image, `FALSE` otherwise.
 */
gboolean is_valid_image_file(const char *filepath);
/**
 * @brief Retrieves the file extension from a given filename.
 * 
 * The function returns a pointer to the start of the extension (including the '.')
 * within the provided filename string. It handles cases where there is no extension
 * or the filename starts with a dot.
 * 
 * @param filename The full path or name of the file.
 * @return A const pointer to the file extension (e.g., ".png"), or NULL if no
 *         extension is found or the filename is invalid. The returned pointer
 *         points into the original string and should not be freed.
 */
const char* get_file_extension(const char *filename);
/**
 * @brief Checks if a file or directory exists at the specified path.
 * 
 * This function uses `stat` to determine the existence of a file system entry.
 * 
 * @param path The path to the file or directory.
 * @return `TRUE` if the file or directory exists, `FALSE` otherwise.
 */
gboolean file_exists(const char *path);
/**
 * @brief Retrieves the size of a file in bytes.
 * 
 * @param path The path to the file.
 * @return The size of the file in bytes on success, or -1 if the file
 *         does not exist or its size cannot be determined.
 */
gint64 get_file_size(const char *path);
/**
 * @brief Retrieves the last modification time of a file.
 * 
 * @param path The path to the file.
 * @return The modification time as a `gint64` (seconds since epoch) on success,
 *         or -1 if the file does not exist or its modification time cannot be determined.
 */
gint64 get_file_mtime(const char *path);
/**
 * @brief Frees a dynamically allocated string and sets its pointer to NULL.
 * 
 * This is a utility function often used with `g_auto_free` or similar
 * cleanup mechanisms to safely free `gchar*` allocated strings.
 * 
 * @param str A pointer to the `gchar*` variable to be freed and nulled.
 */
void cleanup_string(gchar **str);
/**
 * @brief Frees a GString and sets its pointer to NULL.
 * 
 * This is a utility function often used with `g_auto_free` or similar
 * cleanup mechanisms to safely free `GString` instances.
 * 
 * @param str A pointer to the `GString*` variable to be freed and nulled.
 */
void cleanup_gstring(GString **str);

/**
 * @brief Destroyer function for GString pointers used in GHashTable or similar data structures.
 * 
 * This function is suitable for use as a `GDestroyNotify` callback to free
 * `GString` instances stored as `gpointer` data.
 * 
 * @param data A `gpointer` to the `GString` instance to be freed.
 */
void gstring_destroy(gpointer data);

// Terminal utilities
/**
 * @brief Retrieves the current dimensions of the terminal in characters.
 * 
 * Uses `ioctl` with `TIOCGWINSZ` to get the terminal's width and height.
 * Provides fallback default values if `ioctl` fails.
 * 
 * @param width A pointer to an integer where the terminal width will be stored.
 * @param height A pointer to an integer where the terminal height will be stored.
 */
void get_terminal_size(gint *width, gint *height);
/**
 * @brief Retrieves the current dimensions of the terminal in characters and pixels.
 * 
 * Attempts to get terminal dimensions using `ioctl(TIOCGWINSZ)`.
 * It also tries to get pixel dimensions, with sanity checks.
 * 
 * @param width A pointer to an integer where the terminal width (in characters) will be stored.
 * @param height A pointer to an integer where the terminal height (in characters) will be stored.
 * @param pixel_width A pointer to an integer where the terminal width (in pixels) will be stored, or -1 if unavailable.
 * @param pixel_height A pointer to an integer where the terminal height (in pixels) will be stored, or -1 if unavailable.
 */
void get_terminal_size_pixels(gint *width, gint *height, gint *pixel_width, gint *pixel_height);
/**
 * @brief Derives the dimensions of a single terminal character cell in pixels.
 * 
 * Attempts to calculate cell dimensions using `get_terminal_size_pixels`.
 * Falls back to default values (e.g., 10x20 pixels) if pixel metrics are unavailable
 * or invalid.
 * 
 * @param cell_width A pointer to an integer where the cell width (in pixels) will be stored.
 * @param cell_height A pointer to an integer where the cell height (in pixels) will be stored.
 */
void get_terminal_cell_geometry(gint *cell_width, gint *cell_height);
/**
 * @brief Calculates the aspect ratio of a single terminal character cell.
 * 
 * Determines the ratio of `cell_width / cell_height` using pixel dimensions
 * obtained from `get_terminal_size_pixels`. Provides a reasonable fallback
 * value if pixel metrics are not available or are nonsensical.
 * 
 * @return The aspect ratio (width / height) of a terminal character cell.
 */
gdouble get_terminal_cell_aspect_ratio(void);
// Error handling utilities
/**
 * @brief Converts an `ErrorCode` enum value to a human-readable string.
 * 
 * @param error The `ErrorCode` value to convert.
 * @return A constant string describing the error, or "Unknown error" if
 *         the code is not recognized. The returned string should not be freed.
 */
const gchar* error_code_to_string(ErrorCode error);

#endif // COMMON_H
