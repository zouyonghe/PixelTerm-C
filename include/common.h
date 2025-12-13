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
#include <pthread.h>
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

// Cached image structure
typedef struct {
    gchar *filepath;
    GString *rendered_data;
    gint64 timestamp;
    gint access_count;
    gboolean is_valid;
} CachedImage;

// Function declarations
gboolean is_image_file(const char *filename);
const char* get_file_extension(const char *filename);
gboolean file_exists(const char *path);
gint64 get_file_size(const char *path);
gint64 get_file_mtime(const char *path);
void cleanup_string(gchar **str);
// Cleanup GString helper
void cleanup_gstring(GString **str);

// GString destroyer for GHashTable
void gstring_destroy(gpointer data);

// Terminal utilities
void get_terminal_size(gint *width, gint *height);

// Error handling utilities
const gchar* error_code_to_string(ErrorCode error);

#endif // COMMON_H