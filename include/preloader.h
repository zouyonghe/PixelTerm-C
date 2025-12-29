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
    gint work_factor;
} ImagePreloader;

// Preloader lifecycle functions
/**
 * @brief Creates a new `ImagePreloader` instance.
 * 
 * Allocates memory for a new `ImagePreloader` structure and initializes its
 * members to default values, including mutexes, conditions, and queues.
 * 
 * @return A pointer to the newly created `ImagePreloader` instance on success,
 *         or NULL if memory allocation or initialization fails.
 */
ImagePreloader* preloader_create(void);
/**
 * @brief Destroys an `ImagePreloader` instance and frees all associated resources.
 * 
 * This function stops the preloader thread if it's running, clears the task
 * queue and cache, and then frees all allocated memory, including mutexes and
 * condition variables.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance to destroy.
 */
void preloader_destroy(ImagePreloader *preloader);
/**
 * @brief Initializes the preloader's internal state and configuration.
 * 
 * Sets the dithering option and ensures internal structures are ready for use.
 * This should be called after `preloader_create` but before `preloader_start`.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @param dither_enabled A boolean indicating whether dithering should be enabled
 *                       for preloaded images.
 * @return `ERROR_NONE` on success.
 */
ErrorCode preloader_initialize(ImagePreloader *preloader, gboolean dither_enabled, gint work_factor);
/**
 * @brief Starts the preloader's worker thread.
 * 
 * This function creates and detaches a new thread that will process
 * preload tasks from the queue. The preloader transitions to `PRELOADER_ACTIVE` status.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @return `ERROR_NONE` on success, or `ERROR_THREAD_CREATE` if the
 *         worker thread cannot be started.
 */
ErrorCode preloader_start(ImagePreloader *preloader);
/**
 * @brief Stops the preloader's worker thread and clears its task queue.
 * 
 * This function signals the worker thread to terminate gracefully, waits
 * for its completion, and then clears any remaining tasks in the queue.
 * The preloader transitions to `PRELOADER_IDLE` status.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @return `ERROR_NONE` on success.
 */
ErrorCode preloader_stop(ImagePreloader *preloader);

// Configuration
/**
 * @brief Updates the terminal dimensions used by the preloader for rendering.
 * 
 * This function should be called when the terminal size changes to ensure that
 * preloaded images are rendered correctly for the new dimensions.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @param width The new terminal width in characters.
 * @param height The new terminal height in characters.
 */
void preloader_update_terminal_size(ImagePreloader *preloader, gint width, gint height);

// Task management
/**
 * @brief Adds a new image preload task to the queue.
 * 
 * The task specifies an image file to be preloaded, its priority, and the
 * target dimensions for rendering. The preloader will process tasks from
 * the queue in order of priority.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @param filepath The path to the image file to preload.
 * @param priority An integer representing the task's priority (lower value means higher priority).
 * @param target_width The target width (in characters) for rendering the image.
 * @param target_height The target height (in characters) for rendering the image.
 * @return `ERROR_NONE` on success, or `ERROR_MEMORY_ALLOC` if memory allocation fails.
 */
ErrorCode preloader_add_task(ImagePreloader *preloader, const char *filepath, gint priority, gint target_width, gint target_height);
/**
 * @brief Adds preload tasks for multiple files, typically from a directory listing.
 * 
 * This function is designed to efficiently add multiple preload tasks. It
 * prioritizes files around the `current_index` to optimize for user experience.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @param files A `GList` of `gchar*` filepaths to add as tasks.
 * @param current_index The index of the currently displayed file, used for prioritizing tasks.
 * @param target_width The target width (in characters) for rendering the images.
 * @param target_height The target height (in characters) for rendering the images.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if task creation fails.
 */
ErrorCode preloader_add_tasks_for_directory(ImagePreloader *preloader, GList *files, gint current_index, gint target_width, gint target_height);
/**
 * @brief Clears all pending preload tasks from the queue.
 * 
 * This function removes any tasks that have not yet been processed by the
 * preloader's worker thread.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @return `ERROR_NONE` on success.
 */
ErrorCode preloader_clear_queue(ImagePreloader *preloader);
/**
 * @brief Checks if there are any pending tasks in the preloader's queue.
 * 
 * @param preloader A pointer to the constant `ImagePreloader` instance.
 * @return `TRUE` if the task queue is not empty, `FALSE` otherwise.
 */
gboolean preloader_has_pending_tasks(const ImagePreloader *preloader);

// Cache management
// Cache management
/**
 * @brief Retrieves a rendered image from the cache.
 * 
 * If a matching rendered image (based on filepath and target dimensions)
 * is found in the cache, it is returned. The image's position in the
 * Least Recently Used (LRU) queue is updated.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @param filepath The path of the image file.
 * @param target_width The target width (in characters) the image was rendered for.
 * @param target_height The target height (in characters) the image was rendered for.
 * @return A `GString` containing the ANSI-rendered image on cache hit, or NULL on cache miss.
 *         The returned `GString` is owned by the cache and should not be freed by the caller.
 */
GString* preloader_get_cached_image(ImagePreloader *preloader, const char *filepath, gint target_width, gint target_height);
/**
 * @brief Retrieves the original dimensions of a rendered image from the cache.
 * 
 * This function looks up a cached image by its filepath and target dimensions
 * and, if found, returns its original pixel width and height.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @param filepath The path of the image file.
 * @param target_width The target width (in characters) the image was rendered for.
 * @param target_height The target height (in characters) the image was rendered for.
 * @param width A pointer to an integer where the original pixel width will be stored.
 * @param height A pointer to an integer where the original pixel height will be stored.
 * @return `TRUE` if the dimensions were successfully retrieved from cache, `FALSE` otherwise.
 */
gboolean preloader_get_cached_image_dimensions(ImagePreloader *preloader, const char *filepath, gint target_width, gint target_height, gint *width, gint *height);
/**
 * @brief Adds a rendered image to the preloader's cache.
 * 
 * Stores the `GString` containing the ANSI-rendered image along with its
 * original and target dimensions. This function also manages the Least
 * Recently Used (LRU) queue to ensure the cache stays within its maximum size.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @param filepath The path of the image file.
 * @param rendered A `GString` containing the ANSI-rendered image. The preloader
 *                 takes ownership of this `GString` and will free it.
 * @param rendered_width The original pixel width of the image.
 * @param rendered_height The original pixel height of the image.
 * @param target_width The target width (in characters) the image was rendered for.
 * @param target_height The target height (in characters) the image was rendered for.
 */
void preloader_cache_add(ImagePreloader *preloader, const char *filepath, GString *rendered, gint rendered_width, gint rendered_height, gint target_width, gint target_height);
/**
 * @brief Removes a specific rendered image from the preloader's cache.
 * 
 * This function deletes the specified image from both the cache hash table
 * and the LRU queue.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @param filepath The path of the image file to remove from cache.
 */
void preloader_cache_remove(ImagePreloader *preloader, const char *filepath);
/**
 * @brief Clears all entries from the preloader's cache.
 * 
 * This effectively empties the entire image cache.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 */
void preloader_cache_clear(ImagePreloader *preloader);
/**
 * @brief Performs cleanup on the preloader's cache, enforcing size limits.
 * 
 * This function is typically called internally by the preloader to remove
 * least recently used (LRU) items when the cache exceeds its configured
 * maximum size.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 */
void preloader_cache_cleanup(ImagePreloader *preloader);

// Status and control
/**
 * @brief Enables the preloader.
 * 
 * Sets the `enabled` flag to TRUE, allowing preload tasks to be processed.
 * This does not start the thread if it's stopped, but allows it to resume
 * if it was paused.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 */
void preloader_enable(ImagePreloader *preloader);
/**
 * @brief Disables the preloader.
 * 
 * Sets the `enabled` flag to FALSE, preventing any new preload tasks from
 * being processed until re-enabled. The worker thread might continue to
 * finish its current task before pausing.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 */
void preloader_disable(ImagePreloader *preloader);
/**
 * @brief Pauses the preloader's worker thread.
 * 
 * Sets the preloader's status to `PRELOADER_PAUSED`, causing the worker
 * thread to temporarily stop processing tasks until `preloader_resume` is called.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 */
void preloader_pause(ImagePreloader *preloader);
/**
 * @brief Resumes the preloader's worker thread if it was paused.
 * 
 * Sets the preloader's status back to `PRELOADER_ACTIVE` and signals the
 * worker thread to continue processing tasks.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 */
void preloader_resume(ImagePreloader *preloader);
/**
 * @brief Checks if the preloader is currently enabled.
 * 
 * @param preloader A pointer to the constant `ImagePreloader` instance.
 * @return `TRUE` if the preloader is enabled, `FALSE` otherwise.
 */
gboolean preloader_is_enabled(const ImagePreloader *preloader);
/**
 * @brief Retrieves the current operational status of the preloader.
 * 
 * @param preloader A pointer to the constant `ImagePreloader` instance.
 * @return The current `PreloaderStatus` (IDLE, ACTIVE, PAUSED, STOPPING).
 */
PreloaderStatus preloader_get_status(const ImagePreloader *preloader);

// Configuration
/**
 * @brief Sets the maximum number of tasks allowed in the preload queue.
 * 
 * If the queue already contains more tasks than the new maximum size,
 * older tasks will be removed to adhere to the limit.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @param max_size The new maximum size for the task queue.
 */
void preloader_set_max_queue_size(ImagePreloader *preloader, gint max_size);
/**
 * @brief Sets the maximum number of rendered images allowed in the cache.
 * 
 * If the cache already contains more entries than the new maximum size,
 * least recently used entries will be removed to adhere to the limit.
 * 
 * @param preloader A pointer to the `ImagePreloader` instance.
 * @param max_size The new maximum size for the cache.
 */
void preloader_set_max_cache_size(ImagePreloader *preloader, gint max_size);
/**
 * @brief Retrieves the current number of pending tasks in the preload queue.
 * 
 * @param preloader A pointer to the constant `ImagePreloader` instance.
 * @return The number of tasks currently in the queue.
 */
gint preloader_get_queue_size(const ImagePreloader *preloader);
/**
 * @brief Retrieves the current number of items in the preloader's cache.
 * 
 * @param preloader A pointer to the constant `ImagePreloader` instance.
 * @return The number of items currently stored in the cache.
 */
gint preloader_get_cache_size(const ImagePreloader *preloader);

// Statistics
/**
 * @brief Retrieves the number of tasks currently being processed by the preloader.
 * 
 * @param preloader A pointer to the constant `ImagePreloader` instance.
 * @return The count of active (in-progress) preload tasks.
 */
gint preloader_get_active_tasks(const ImagePreloader *preloader);
/**
 * @brief Calculates the cache hit rate for the preloader.
 * 
 * @param preloader A pointer to the constant `ImagePreloader` instance.
 * @return The cache hit rate as a floating-point number (e.g., 0.85 for 85%),
 *         or 0.0 if no cache requests have been made.
 */
gfloat preloader_get_cache_hit_rate(const ImagePreloader *preloader);
/**
 * @brief Retrieves the total number of images processed (rendered and cached) by the preloader.
 * 
 * @param preloader A pointer to the constant `ImagePreloader` instance.
 * @return The cumulative count of processed images.
 */
gint64 preloader_get_total_processed(const ImagePreloader *preloader);

// Worker thread function
/**
 * @brief The entry point function for the preloader's worker thread.
 * 
 * This function continuously dequeues preload tasks, processes them (renders
 * the image), and stores the result in the cache. It respects pause/stop
 * signals and manages thread synchronization.
 * 
 * @param data A `gpointer` to the `ImagePreloader` instance that this
 *             thread will manage.
 * @return A `gpointer` (always NULL upon thread exit).
 */
gpointer preloader_worker_thread(gpointer data);

#endif // PRELOADER_H
