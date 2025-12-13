#include "preloader.h"
#include "renderer.h"

// Create a new preloader
ImagePreloader* preloader_create(void) {
    ImagePreloader *preloader = g_new0(ImagePreloader, 1);
    if (!preloader) {
        return NULL;
    }

    preloader->thread = NULL;
    preloader->task_queue = g_queue_new();
    preloader->preload_cache = g_hash_table_new_full(g_str_hash, g_str_equal, 
                                                    g_free, (GDestroyNotify)gstring_destroy);
    
    g_mutex_init(&preloader->mutex);
    g_cond_init(&preloader->condition);
    
    preloader->status = PRELOADER_IDLE;
    preloader->enabled = TRUE;
    preloader->max_queue_size = PRELOAD_QUEUE_SIZE;
    preloader->max_cache_size = MAX_CACHE_SIZE;
    preloader->active_tasks = 0;
    
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

    // Cleanup synchronization objects
    g_mutex_clear(&preloader->mutex);
    g_cond_clear(&preloader->condition);

    g_free(preloader);
}

// Initialize preloader
ErrorCode preloader_initialize(ImagePreloader *preloader) {
    if (!preloader) {
        return ERROR_MEMORY_ALLOC;
    }

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
ErrorCode preloader_add_task(ImagePreloader *preloader, const char *filepath, gint priority) {
    if (!preloader || !filepath || !preloader->enabled) {
        return ERROR_MEMORY_ALLOC;
    }

    g_mutex_lock(&preloader->mutex);

    // Check if we're at capacity
    if (g_queue_get_length(preloader->task_queue) >= preloader->max_queue_size) {
        g_mutex_unlock(&preloader->mutex);
        return ERROR_MEMORY_ALLOC; // Queue is full
    }

    // Check if already in cache
    if (g_hash_table_contains(preloader->preload_cache, filepath)) {
        g_mutex_unlock(&preloader->mutex);
        return ERROR_NONE; // Already cached
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
ErrorCode preloader_add_tasks_for_directory(ImagePreloader *preloader, GList *files, gint current_index) {
    if (!preloader || !files || !preloader->enabled) {
        return ERROR_MEMORY_ALLOC;
    }

    gint total_files = g_list_length(files);
    if (total_files <= 1) {
        return ERROR_NONE;
    }

    // Add next few images with high priority
    for (gint i = 1; i <= 3 && (current_index + i) < total_files; i++) {
        gchar *filepath = (gchar*)g_list_nth_data(files, current_index + i);
        preloader_add_task(preloader, filepath, 2 - i); // Priority 1, 2, 3...
    }

    // Add previous images with lower priority
    for (gint i = 1; i <= 2 && (current_index - i) >= 0; i++) {
        gchar *filepath = (gchar*)g_list_nth_data(files, current_index - i);
        preloader_add_task(preloader, filepath, 10 + i); // Lower priority
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
gboolean preloader_has_pending_tasks(const ImagePreloader *preloader) {
    if (!preloader) {
        return FALSE;
    }

    g_mutex_lock((GMutex*)&preloader->mutex);
    gboolean has_tasks = !g_queue_is_empty(preloader->task_queue);
    g_mutex_unlock((GMutex*)&preloader->mutex);

    return has_tasks;
}

// Get cached image
GString* preloader_get_cached_image(ImagePreloader *preloader, const char *filepath) {
    if (!preloader || !filepath) {
        return NULL;
    }

    g_mutex_lock(&preloader->mutex);
    GString *cached = g_hash_table_lookup(preloader->preload_cache, filepath);
    g_mutex_unlock(&preloader->mutex);

    return cached;
}

// Add rendered image to cache
void preloader_cache_add(ImagePreloader *preloader, const char *filepath, GString *rendered) {
    if (!preloader || !filepath || !rendered) {
        return;
    }

    g_mutex_lock(&preloader->mutex);

    // Check cache size and cleanup if necessary
    if (g_hash_table_size(preloader->preload_cache) >= preloader->max_cache_size) {
        preloader_cache_cleanup(preloader);
    }

    // Add to cache
    gchar *key = g_strdup(filepath);
    GString *value = g_string_new_len(rendered->str, rendered->len);
    g_hash_table_insert(preloader->preload_cache, key, value);

    g_mutex_unlock(&preloader->mutex);
}

// Remove specific image from cache
void preloader_cache_remove(ImagePreloader *preloader, const char *filepath) {
    if (!preloader || !filepath) {
        return;
    }

    g_mutex_lock(&preloader->mutex);
    g_hash_table_remove(preloader->preload_cache, filepath);
    g_mutex_unlock(&preloader->mutex);
}

// Clear all cached images
void preloader_cache_clear(ImagePreloader *preloader) {
    if (!preloader) {
        return;
    }

    g_mutex_lock(&preloader->mutex);
    g_hash_table_remove_all(preloader->preload_cache);
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

    // Remove oldest entries to maintain cache size limit
    guint target_size = preloader->max_cache_size * 3 / 4; // Remove 25% when over limit
    guint to_remove = size - target_size;
    
    GHashTableIter iter;
    gpointer key, value;
    guint removed = 0;

    g_hash_table_iter_init(&iter, preloader->preload_cache);
    while (g_hash_table_iter_next(&iter, &key, &value) && removed < to_remove) {
        g_hash_table_iter_remove(&iter);
        removed++;
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
gboolean preloader_is_enabled(const ImagePreloader *preloader) {
    if (!preloader) {
        return FALSE;
    }

    g_mutex_lock((GMutex*)&preloader->mutex);
    gboolean enabled = preloader->enabled;
    g_mutex_unlock((GMutex*)&preloader->mutex);

    return enabled;
}

// Get preloader status
PreloaderStatus preloader_get_status(const ImagePreloader *preloader) {
    if (!preloader) {
        return PRELOADER_IDLE;
    }

    g_mutex_lock((GMutex*)&preloader->mutex);
    PreloaderStatus status = preloader->status;
    g_mutex_unlock((GMutex*)&preloader->mutex);

    return status;
}

// Set maximum queue size
void preloader_set_max_queue_size(ImagePreloader *preloader, gint max_size) {
    if (preloader && max_size > 0) {
        g_mutex_lock(&preloader->mutex);
        preloader->max_queue_size = max_size;
        g_mutex_unlock(&preloader->mutex);
    }
}

// Set maximum cache size
void preloader_set_max_cache_size(ImagePreloader *preloader, gint max_size) {
    if (preloader && max_size > 0) {
        g_mutex_lock(&preloader->mutex);
        preloader->max_cache_size = max_size;
        g_mutex_unlock(&preloader->mutex);
    }
}

// Get queue size
gint preloader_get_queue_size(const ImagePreloader *preloader) {
    if (!preloader) {
        return 0;
    }

    g_mutex_lock((GMutex*)&preloader->mutex);
    gint size = g_queue_get_length(preloader->task_queue);
    g_mutex_unlock((GMutex*)&preloader->mutex);

    return size;
}

// Get cache size
gint preloader_get_cache_size(const ImagePreloader *preloader) {
    if (!preloader) {
        return 0;
    }

    g_mutex_lock((GMutex*)&preloader->mutex);
    gint size = g_hash_table_size(preloader->preload_cache);
    g_mutex_unlock((GMutex*)&preloader->mutex);

    return size;
}

// Get active task count
gint preloader_get_active_tasks(const ImagePreloader *preloader) {
    if (!preloader) {
        return 0;
    }

    g_mutex_lock((GMutex*)&preloader->mutex);
    gint active = preloader->active_tasks;
    g_mutex_unlock((GMutex*)&preloader->mutex);

    return active;
}

// Get cache hit rate (simplified implementation)
gfloat preloader_get_cache_hit_rate(const ImagePreloader *preloader) {
    // This would need more sophisticated tracking for accurate results
    (void)preloader; // Suppress unused parameter warning
    return 0.0f;
}

// Get total processed images
gint64 preloader_get_total_processed(const ImagePreloader *preloader) {
    // This would need a counter to track processed images
    (void)preloader; // Suppress unused parameter warning
    return 0;
}

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
        .max_height = term_height - 1, // Leave space for filename
        .preserve_aspect_ratio = TRUE,
        .dither = TRUE,
        .color_space = CHAFA_COLOR_SPACE_RGB,
        .pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS,
        .work_factor = 1
    };

    renderer_initialize(renderer, &config);

    while (TRUE) {
        g_mutex_lock(&preloader->mutex);

        // Wait for work or stop signal
        while (g_queue_is_empty(preloader->task_queue) && 
               preloader->status == PRELOADER_ACTIVE && 
               preloader->enabled) {
            g_cond_wait(&preloader->condition, &preloader->mutex);
        }

        // Check if we should exit
        if (preloader->status == PRELOADER_STOPPING) {
            g_mutex_unlock(&preloader->mutex);
            break;
        }

        // Get next task
        PreloadTask *task = NULL;
        if (!g_queue_is_empty(preloader->task_queue) && preloader->enabled) {
            task = (PreloadTask*)g_queue_pop_head(preloader->task_queue);
            preloader->active_tasks++;
        }

        g_mutex_unlock(&preloader->mutex);

        // Process task
        if (task) {
            // Render the image
            GString *rendered = renderer_render_image_file(renderer, task->filepath);
            
            if (rendered) {
                // Add to cache
                preloader_cache_add(preloader, task->filepath, rendered);
                // Free the original GString since cache_add makes a copy
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