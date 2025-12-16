#ifndef PRELOADER_H
#define PRELOADER_H

#include "common.h"

// Preload task structure
typedef struct {
    gchar *filepath;
    gint priority;
    gint64 timestamp;
    gint target_width;
    gint target_height;
} PreloadTask;

// Cached image data with dimensions
typedef struct {
    GString *rendered;
    gint width;
    gint height;
} CachedImageData;

// Preloader status
typedef enum {
    PRELOADER_IDLE,
    PRELOADER_ACTIVE,
    PRELOADER_PAUSED,
    PRELOADER_STOPPING
} PreloaderStatus;

// Preloader structure
typedef struct {
    GThread *thread;
    GMutex mutex;
    GCond condition;
    GQueue *task_queue;
    GHashTable *preload_cache;
    GQueue *lru_queue;
    PreloaderStatus status;
    gboolean enabled;
    gint max_queue_size;
    gint max_cache_size;
    gint active_tasks;
    
    // Terminal dimensions for rendering
    gint term_width;
    gint term_height;
    
    // Dithering setting
    gboolean dither_enabled;
} ImagePreloader;

// Preloader lifecycle functions
ImagePreloader* preloader_create(void);
void preloader_destroy(ImagePreloader *preloader);
ErrorCode preloader_initialize(ImagePreloader *preloader, gboolean dither_enabled);
ErrorCode preloader_start(ImagePreloader *preloader);
ErrorCode preloader_stop(ImagePreloader *preloader);

// Configuration
void preloader_update_terminal_size(ImagePreloader *preloader, gint width, gint height);

// Task management
ErrorCode preloader_add_task(ImagePreloader *preloader, const char *filepath, gint priority, gint target_width, gint target_height);
ErrorCode preloader_add_tasks_for_directory(ImagePreloader *preloader, GList *files, gint current_index, gint target_width, gint target_height);
ErrorCode preloader_clear_queue(ImagePreloader *preloader);
gboolean preloader_has_pending_tasks(const ImagePreloader *preloader);

// Cache management
GString* preloader_get_cached_image(ImagePreloader *preloader, const char *filepath, gint target_width, gint target_height);
gboolean preloader_get_cached_image_dimensions(ImagePreloader *preloader, const char *filepath, gint target_width, gint target_height, gint *width, gint *height);
void preloader_cache_add(ImagePreloader *preloader, const char *filepath, GString *rendered, gint rendered_width, gint rendered_height, gint target_width, gint target_height);
void preloader_cache_remove(ImagePreloader *preloader, const char *filepath);
void preloader_cache_clear(ImagePreloader *preloader);
void preloader_cache_cleanup(ImagePreloader *preloader);

// Status and control
void preloader_enable(ImagePreloader *preloader);
void preloader_disable(ImagePreloader *preloader);
void preloader_pause(ImagePreloader *preloader);
void preloader_resume(ImagePreloader *preloader);
gboolean preloader_is_enabled(const ImagePreloader *preloader);
PreloaderStatus preloader_get_status(const ImagePreloader *preloader);

// Configuration
void preloader_set_max_queue_size(ImagePreloader *preloader, gint max_size);
void preloader_set_max_cache_size(ImagePreloader *preloader, gint max_size);
gint preloader_get_queue_size(const ImagePreloader *preloader);
gint preloader_get_cache_size(const ImagePreloader *preloader);

// Statistics
gint preloader_get_active_tasks(const ImagePreloader *preloader);
gfloat preloader_get_cache_hit_rate(const ImagePreloader *preloader);
gint64 preloader_get_total_processed(const ImagePreloader *preloader);

// Worker thread function
gpointer preloader_worker_thread(gpointer data);

#endif // PRELOADER_H
