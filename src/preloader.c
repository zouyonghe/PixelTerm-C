#include "preloader.h"
#include "renderer.h"

typedef struct {
    gchar *filepath;
    gint target_width;
    gint target_height;
} PreloadCacheKey;

static guint preload_cache_key_hash(gconstpointer data) {
    const PreloadCacheKey *key = (const PreloadCacheKey*)data;
    guint hash = g_str_hash(key->filepath);
    hash = hash * 31 + (guint)key->target_width;
    hash = hash * 31 + (guint)key->target_height;
    return hash;
}

static gboolean preload_cache_key_equal(gconstpointer a, gconstpointer b) {
    const PreloadCacheKey *ka = (const PreloadCacheKey*)a;
    const PreloadCacheKey *kb = (const PreloadCacheKey*)b;
    return ka->target_width == kb->target_width &&
           ka->target_height == kb->target_height &&
           g_strcmp0(ka->filepath, kb->filepath) == 0;
}

static void preload_cache_key_destroy(gpointer data) {
    PreloadCacheKey *key = (PreloadCacheKey*)data;
    if (key) {
        g_free(key->filepath);
        g_free(key);
    }
}

static PreloadCacheKey* preload_cache_key_new(const char *filepath, gint width, gint height) {
    PreloadCacheKey *key = g_new0(PreloadCacheKey, 1);
    if (!key) {
        return NULL;
    }
    key->filepath = g_strdup(filepath);
    key->target_width = width;
    key->target_height = height;
    return key;
}

static void preloader_normalize_dims(ImagePreloader *preloader, gint *width, gint *height) {
    if (!preloader || !width || !height) {
        return;
    }
    if (*width <= 0) *width = preloader->term_width > 0 ? preloader->term_width : 80;
    if (*height <= 0) *height = preloader->term_height > 0 ? preloader->term_height : 24;
    if (*width < 1) *width = 1;
    if (*height < 1) *height = 1;
}

// Destroy cached image data
void cached_image_data_destroy(CachedImageData *data) {
    if (data) {
        if (data->rendered) {
            g_string_free(data->rendered, TRUE);
        }
        g_free(data);
    }
}

// Create a new preloader
ImagePreloader* preloader_create(void) {
    ImagePreloader *preloader = g_new0(ImagePreloader, 1);
    if (!preloader) {
        return NULL;
    }

    preloader->thread = NULL;
    preloader->task_queue = g_queue_new();
    preloader->preload_cache = g_hash_table_new_full(preload_cache_key_hash,
                                                    preload_cache_key_equal,
                                                    preload_cache_key_destroy,
                                                    (GDestroyNotify)cached_image_data_destroy);
    preloader->lru_queue = g_queue_new();
    
    g_mutex_init(&preloader->mutex);
    g_cond_init(&preloader->condition);
    
    preloader->status = PRELOADER_IDLE;
    preloader->enabled = TRUE;
    preloader->max_queue_size = PRELOAD_QUEUE_SIZE;
    preloader->max_cache_size = MAX_CACHE_SIZE;
    preloader->active_tasks = 0;
    preloader->work_factor = 9;
    
    // Default terminal dimensions
    preloader->term_width = 80;
    preloader->term_height = 24;

    return preloader;
}

// Destroy preloader and free resources
void preloader_destroy(ImagePreloader *preloader) {
    if (!preloader) {
        return;
    }

    // Stop the preloader
    preloader_stop(preloader);

    // Cleanup queue
    if (preloader->task_queue) {
        // Free any remaining tasks
        while (!g_queue_is_empty(preloader->task_queue)) {
            PreloadTask *task = (PreloadTask*)g_queue_pop_head(preloader->task_queue);
            g_free(task->filepath);
            g_free(task);
        }
        g_queue_free(preloader->task_queue);
    }

    // Cleanup cache
    if (preloader->preload_cache) {
        g_hash_table_destroy(preloader->preload_cache);
    }
    if (preloader->lru_queue) {
        g_queue_free(preloader->lru_queue);
    }

    // Cleanup synchronization objects
    g_mutex_clear(&preloader->mutex);
    g_cond_clear(&preloader->condition);

    g_free(preloader);
}

// Initialize preloader
ErrorCode preloader_initialize(ImagePreloader *preloader, gboolean dither_enabled, gint work_factor, gboolean force_sixel) {
    if (!preloader) {
        return ERROR_MEMORY_ALLOC;
    }
    preloader->dither_enabled = dither_enabled;
    if (work_factor < 1) {
        work_factor = 1;
    } else if (work_factor > 9) {
        work_factor = 9;
    }
    preloader->work_factor = work_factor;
    preloader->force_sixel = force_sixel;

    return ERROR_NONE;
}

// Start preloader thread
ErrorCode preloader_start(ImagePreloader *preloader) {
    if (!preloader || preloader->thread) {
        return ERROR_NONE;
    }

    g_mutex_lock(&preloader->mutex);
    preloader->status = PRELOADER_ACTIVE;
    g_mutex_unlock(&preloader->mutex);

    preloader->thread = g_thread_new("preloader", preloader_worker_thread, preloader);
    
    if (!preloader->thread) {
        g_mutex_lock(&preloader->mutex);
        preloader->status = PRELOADER_IDLE;
        g_mutex_unlock(&preloader->mutex);
        
        return ERROR_THREAD_CREATE;
    }

    return ERROR_NONE;
}

// Stop preloader thread
ErrorCode preloader_stop(ImagePreloader *preloader) {
    if (!preloader) {
        return ERROR_MEMORY_ALLOC;
    }

    g_mutex_lock(&preloader->mutex);
    
    if (preloader->status == PRELOADER_ACTIVE) {
        preloader->status = PRELOADER_STOPPING;
        g_cond_signal(&preloader->condition);
    }
    
    g_mutex_unlock(&preloader->mutex);

    // Wait for thread to finish
    if (preloader->thread) {
        g_thread_join(preloader->thread);
        preloader->thread = NULL;
    }

    g_mutex_lock(&preloader->mutex);
    preloader->status = PRELOADER_IDLE;
    g_mutex_unlock(&preloader->mutex);

    return ERROR_NONE;
}

// Add a preload task
ErrorCode preloader_add_task(ImagePreloader *preloader, const char *filepath, gint priority, gint target_width, gint target_height) {
    if (!preloader || !filepath || !preloader->enabled) {
        return ERROR_MEMORY_ALLOC;
    }

    if (!is_image_file(filepath)) {
        return ERROR_NONE;
    }

    g_mutex_lock(&preloader->mutex);

    gint task_width = target_width;
    gint task_height = target_height;
    preloader_normalize_dims(preloader, &task_width, &task_height);

    // Check if already in cache
    PreloadCacheKey lookup_key = {(gchar*)filepath, task_width, task_height};
    if (g_hash_table_lookup(preloader->preload_cache, &lookup_key)) {
        g_mutex_unlock(&preloader->mutex);
        return ERROR_NONE; // Already cached
    }

    // Avoid enqueueing duplicate work for the same target size
    for (GList *node = preloader->task_queue->head; node; node = node->next) {
        PreloadTask *queued = (PreloadTask*)node->data;
        if (queued &&
            queued->target_width == task_width &&
            queued->target_height == task_height &&
            g_strcmp0(queued->filepath, filepath) == 0) {
            g_mutex_unlock(&preloader->mutex);
            return ERROR_NONE;
        }
    }

    // Check if we're at capacity
    if (g_queue_get_length(preloader->task_queue) >= preloader->max_queue_size) {
        g_mutex_unlock(&preloader->mutex);
        return ERROR_MEMORY_ALLOC; // Queue is full
    }

    // Create new task
    PreloadTask *task = g_new0(PreloadTask, 1);
    if (!task) {
        g_mutex_unlock(&preloader->mutex);
        return ERROR_MEMORY_ALLOC;
    }

    task->filepath = g_strdup(filepath);
    task->priority = priority;
    task->timestamp = g_get_real_time();
    task->target_width = task_width;
    task->target_height = task_height;

    // Insert task based on priority (higher priority = lower number)
    if (priority <= 0 || g_queue_is_empty(preloader->task_queue)) {
        g_queue_push_head(preloader->task_queue, task);
    } else {
        // Find insertion point based on priority
        GList *current = preloader->task_queue->head;
        gint position = 0;
        
        while (current) {
            PreloadTask *current_task = (PreloadTask*)current->data;
            if (current_task->priority > priority) {
                break;
            }
            current = current->next;
            position++;
        }
        
        g_queue_push_nth(preloader->task_queue, task, position);
    }

    g_cond_signal(&preloader->condition);
    g_mutex_unlock(&preloader->mutex);

    return ERROR_NONE;
}

// Add tasks for adjacent images
ErrorCode preloader_add_tasks_for_directory(ImagePreloader *preloader, GList *files, gint current_index, gint target_width, gint target_height) {
    if (!preloader || !files || !preloader->enabled) {
        return ERROR_MEMORY_ALLOC;
    }

    gint task_width = target_width;
    gint task_height = target_height;
    preloader_normalize_dims(preloader, &task_width, &task_height);

    gint total_files = g_list_length(files);
    if (total_files <= 1 || current_index < 0 || current_index >= total_files) {
        return ERROR_NONE;
    }

    GList *current = g_list_nth(files, current_index);
    GList *walker = current ? current->next : NULL;
    for (gint priority = 1; walker && priority <= 3; priority++) {
        if (is_image_file((gchar*)walker->data)) {
            preloader_add_task(preloader, (gchar*)walker->data, priority, task_width, task_height);
        }
        walker = walker->next;
    }

    walker = current ? current->prev : NULL;
    for (gint distance = 1; walker && distance <= 2; distance++) {
        if (is_image_file((gchar*)walker->data)) {
            preloader_add_task(preloader, (gchar*)walker->data, 10 + distance, task_width, task_height);
        }
        walker = walker->prev;
    }

    return ERROR_NONE;
}

// Clear all pending tasks
ErrorCode preloader_clear_queue(ImagePreloader *preloader) {
    if (!preloader) {
        return ERROR_MEMORY_ALLOC;
    }

    g_mutex_lock(&preloader->mutex);

    while (!g_queue_is_empty(preloader->task_queue)) {
        PreloadTask *task = (PreloadTask*)g_queue_pop_head(preloader->task_queue);
        g_free(task->filepath);
        g_free(task);
    }

    g_mutex_unlock(&preloader->mutex);

    return ERROR_NONE;
}

// Check if there are pending tasks
// Get cached image
GString* preloader_get_cached_image(ImagePreloader *preloader, const char *filepath, gint target_width, gint target_height) {
    if (!preloader || !filepath) {
        return NULL;
    }

    g_mutex_lock(&preloader->mutex);
    gint lookup_width = target_width;
    gint lookup_height = target_height;
    preloader_normalize_dims(preloader, &lookup_width, &lookup_height);
    gpointer stored_key = NULL;
    gpointer stored_value = NULL;
    CachedImageData *cached_data = NULL;
    PreloadCacheKey lookup = {(gchar*)filepath, lookup_width, lookup_height};
    if (g_hash_table_lookup_extended(preloader->preload_cache, &lookup, &stored_key, &stored_value)) {
        cached_data = (CachedImageData*)stored_value;
        // Move to front of LRU
        if (stored_key) {
            g_queue_remove(preloader->lru_queue, stored_key);
            g_queue_push_head(preloader->lru_queue, stored_key);
        }
    }
    GString *rendered = NULL;
    if (cached_data && cached_data->rendered) {
        rendered = g_string_new_len(cached_data->rendered->str, cached_data->rendered->len);
    }
    g_mutex_unlock(&preloader->mutex);

    return rendered;
}

// Get cached image dimensions
gboolean preloader_get_cached_image_dimensions(ImagePreloader *preloader, const char *filepath, gint target_width, gint target_height, gint *width, gint *height) {
    if (!preloader || !filepath || !width || !height) {
        return FALSE;
    }

    g_mutex_lock(&preloader->mutex);
    gint lookup_width = target_width;
    gint lookup_height = target_height;
    preloader_normalize_dims(preloader, &lookup_width, &lookup_height);
    gpointer stored_key = NULL;
    gpointer stored_value = NULL;
    CachedImageData *cached_data = NULL;
    PreloadCacheKey lookup = {(gchar*)filepath, lookup_width, lookup_height};
    if (g_hash_table_lookup_extended(preloader->preload_cache, &lookup, &stored_key, &stored_value)) {
        cached_data = (CachedImageData*)stored_value;
        if (stored_key) {
            g_queue_remove(preloader->lru_queue, stored_key);
            g_queue_push_head(preloader->lru_queue, stored_key);
        }
    }
    if (cached_data) {
        *width = cached_data->width;
        *height = cached_data->height;
        g_mutex_unlock(&preloader->mutex);
        return TRUE;
    }
    g_mutex_unlock(&preloader->mutex);

    return FALSE;
}

// Add rendered image to cache
void preloader_cache_add(ImagePreloader *preloader, const char *filepath, GString *rendered, gint rendered_width, gint rendered_height, gint target_width, gint target_height) {
    if (!preloader || !filepath || !rendered) {
        return;
    }

    g_mutex_lock(&preloader->mutex);
    gint key_width = target_width;
    gint key_height = target_height;
    preloader_normalize_dims(preloader, &key_width, &key_height);

    // If already cached, replace content and move to front
    gpointer stored_key = NULL;
    gpointer stored_value = NULL;
    PreloadCacheKey lookup = {(gchar*)filepath, key_width, key_height};
    if (g_hash_table_lookup_extended(preloader->preload_cache, &lookup, &stored_key, &stored_value)) {
        CachedImageData *existing = (CachedImageData*)stored_value;
        if (existing->rendered) {
            g_string_free(existing->rendered, TRUE);
        }
        existing->rendered = g_string_new_len(rendered->str, rendered->len);
        existing->width = (rendered_width > 0) ? rendered_width : preloader->term_width;
        if (rendered_height > 0) {
            existing->height = rendered_height;
        } else {
            existing->height = 1;
            for (gsize i = 0; i < rendered->len; i++) {
                if (rendered->str[i] == '\n') {
                    existing->height++;
                }
            }
        }
        if (stored_key) {
            g_queue_remove(preloader->lru_queue, stored_key);
            g_queue_push_head(preloader->lru_queue, stored_key);
        }
        g_mutex_unlock(&preloader->mutex);
        return;
    }

    // Check cache size and cleanup if necessary before inserting new
    if (g_hash_table_size(preloader->preload_cache) >= preloader->max_cache_size) {
        preloader_cache_cleanup(preloader);
    }

    // Add to cache with dimensions
    PreloadCacheKey *key = preload_cache_key_new(filepath, key_width, key_height);
    CachedImageData *value = g_new0(CachedImageData, 1);
    if (value) {
        value->rendered = g_string_new_len(rendered->str, rendered->len);
        value->width = (rendered_width > 0) ? rendered_width : preloader->term_width;
        if (rendered_height > 0) {
            value->height = rendered_height;
        } else {
            value->height = 1;
            for (gsize i = 0; i < rendered->len; i++) {
                if (rendered->str[i] == '\n') {
                    value->height++;
                }
            }
        }
    }
    if (!key || !value) {
        preload_cache_key_destroy(key);
        if (value && value->rendered) {
            g_string_free(value->rendered, TRUE);
        }
        g_free(value);
        g_mutex_unlock(&preloader->mutex);
        return;
    }
    g_hash_table_insert(preloader->preload_cache, key, value);
    g_queue_remove(preloader->lru_queue, key);
    g_queue_push_head(preloader->lru_queue, key);

    g_mutex_unlock(&preloader->mutex);
}

// Remove specific image from cache
void preloader_cache_remove(ImagePreloader *preloader, const char *filepath) {
    if (!preloader || !filepath) {
        return;
    }

    g_mutex_lock(&preloader->mutex);
    for (GList *link = preloader->lru_queue->head; link != NULL; ) {
        GList *next = link->next;
        PreloadCacheKey *key = (PreloadCacheKey*)link->data;
        if (key && g_strcmp0(key->filepath, filepath) == 0) {
            g_queue_delete_link(preloader->lru_queue, link);
            g_hash_table_remove(preloader->preload_cache, key);
        }
        link = next;
    }
    g_mutex_unlock(&preloader->mutex);
}

// Clear all cached images
void preloader_cache_clear(ImagePreloader *preloader) {
    if (!preloader) {
        return;
    }

    g_mutex_lock(&preloader->mutex);
    while (!g_queue_is_empty(preloader->lru_queue)) {
        gpointer key = g_queue_pop_head(preloader->lru_queue);
        g_hash_table_remove(preloader->preload_cache, key);
    }
    g_mutex_unlock(&preloader->mutex);
}

// Cleanup old cache entries
void preloader_cache_cleanup(ImagePreloader *preloader) {
    if (!preloader) {
        return;
    }

    guint size = g_hash_table_size(preloader->preload_cache);
    if (size <= preloader->max_cache_size) {
        return;
    }

    // Remove oldest entries from LRU tail
    while (g_hash_table_size(preloader->preload_cache) > preloader->max_cache_size) {
        gpointer key = g_queue_pop_tail(preloader->lru_queue);
        if (!key) {
            break;
        }
        g_hash_table_remove(preloader->preload_cache, key);
    }
}

// Enable preloader
void preloader_enable(ImagePreloader *preloader) {
    if (preloader) {
        g_mutex_lock(&preloader->mutex);
        preloader->enabled = TRUE;
        g_cond_signal(&preloader->condition);
        g_mutex_unlock(&preloader->mutex);
    }
}

// Disable preloader
void preloader_disable(ImagePreloader *preloader) {
    if (preloader) {
        g_mutex_lock(&preloader->mutex);
        preloader->enabled = FALSE;
        g_mutex_unlock(&preloader->mutex);
    }
}

// Pause preloader
void preloader_pause(ImagePreloader *preloader) {
    if (preloader) {
        g_mutex_lock(&preloader->mutex);
        if (preloader->status == PRELOADER_ACTIVE) {
            preloader->status = PRELOADER_PAUSED;
        }
        g_mutex_unlock(&preloader->mutex);
    }
}

// Resume preloader
void preloader_resume(ImagePreloader *preloader) {
    if (preloader) {
        g_mutex_lock(&preloader->mutex);
        if (preloader->status == PRELOADER_PAUSED) {
            preloader->status = PRELOADER_ACTIVE;
            g_cond_signal(&preloader->condition);
        }
        g_mutex_unlock(&preloader->mutex);
    }
}

// Check if preloader is enabled
// Update terminal dimensions for rendering
void preloader_update_terminal_size(ImagePreloader *preloader, gint width, gint height) {
    if (preloader && width > 0 && height > 0) {
        g_mutex_lock(&preloader->mutex);
        preloader->term_width = width;
        preloader->term_height = height;
        g_mutex_unlock(&preloader->mutex);
    }
}

// Worker thread function
gpointer preloader_worker_thread(gpointer data) {
    ImagePreloader *preloader = (ImagePreloader*)data;
    
    if (!preloader) {
        return NULL;
    }

    // Create renderer for this thread
    ImageRenderer *renderer = renderer_create();
    if (!renderer) {
        return NULL;
    }

    // Get current terminal dimensions
    gint term_width, term_height;
    g_mutex_lock(&preloader->mutex);
    term_width = preloader->term_width;
    term_height = preloader->term_height;
    g_mutex_unlock(&preloader->mutex);

    RendererConfig config = {
        .max_width = term_width,
        .max_height = term_height,
        .preserve_aspect_ratio = TRUE,
        .dither = preloader->dither_enabled,
        .color_space = CHAFA_COLOR_SPACE_RGB,
        .work_factor = preloader->work_factor,
        .force_sixel = preloader->force_sixel,
        .dither_mode = preloader->dither_enabled ? CHAFA_DITHER_MODE_ORDERED : CHAFA_DITHER_MODE_NONE,
        .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
        .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
    };

    ErrorCode init_result = renderer_initialize(renderer, &config);
    if (init_result != ERROR_NONE) {
        // Failed to initialize renderer, set status to failed and exit thread
        g_mutex_lock(&preloader->mutex);
        preloader->status = PRELOADER_STOPPING;
        g_mutex_unlock(&preloader->mutex);
        renderer_destroy(renderer);
        return NULL;
    }
    gint current_max_width = config.max_width;
    gint current_max_height = config.max_height;

    while (TRUE) {
        g_mutex_lock(&preloader->mutex);

        // Wait when paused/disabled or when there is no work; wake on resume/enable/stop
        while (TRUE) {
            if (preloader->status == PRELOADER_STOPPING) {
                break;
            }
            if (preloader->status == PRELOADER_PAUSED ||
                !preloader->enabled ||
                g_queue_is_empty(preloader->task_queue)) {
                g_cond_wait(&preloader->condition, &preloader->mutex);
                continue;
            }
            break;
        }

        if (preloader->status == PRELOADER_STOPPING) {
            g_mutex_unlock(&preloader->mutex);
            break;
        }

        // Get next task
        PreloadTask *task = NULL;
        if (preloader->status == PRELOADER_ACTIVE &&
            preloader->enabled &&
            !g_queue_is_empty(preloader->task_queue)) {
            task = (PreloadTask*)g_queue_pop_head(preloader->task_queue);
            preloader->active_tasks++;
        }

        g_mutex_unlock(&preloader->mutex);

        // Process task
        if (task) {
            gint task_width = task->target_width;
            gint task_height = task->target_height;
            preloader_normalize_dims(preloader, &task_width, &task_height);

            // Reconfigure target size when tasks demand different geometry
            if (task_width != current_max_width || task_height != current_max_height) {
                current_max_width = task_width;
                current_max_height = task_height;
                renderer->config.max_width = current_max_width;
                renderer->config.max_height = current_max_height;
                renderer_cache_clear(renderer); // avoid serving mismatched cached renders
            }

            // Render the image
            GString *rendered = renderer_render_image_file(renderer, task->filepath);
            
            if (rendered) {
                // Get the actual rendered dimensions
                gint rendered_width, rendered_height;
                renderer_get_rendered_dimensions(renderer, &rendered_width, &rendered_height);
                
                // Add to cache with dimensions (makes its own copy)
                preloader_cache_add(preloader, task->filepath, rendered, rendered_width, rendered_height, task_width, task_height);

                // Free the original GString
                g_string_free(rendered, TRUE);
            }

            // Cleanup task
            g_free(task->filepath);
            g_free(task);

            // Update active task count
            g_mutex_lock(&preloader->mutex);
            preloader->active_tasks--;
            g_mutex_unlock(&preloader->mutex);
        }
    }

    // Cleanup renderer
    renderer_destroy(renderer);

    return NULL;
}
