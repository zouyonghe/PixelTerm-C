#include "renderer.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <sys/ioctl.h>

// Load pixbuf using a GInputStream to avoid path-length limits in gdk_pixbuf_new_from_file
static GdkPixbuf* renderer_load_pixbuf_from_stream(const char *filepath, GError **error) {
    if (!filepath) {
        return NULL;
    }

    GFile *file = g_file_new_for_path(filepath);
    if (!file) {
        return NULL;
    }

    GFileInputStream *stream = g_file_read(file, NULL, error);
    g_object_unref(file);
    if (!stream) {
        return NULL;
    }

    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream(G_INPUT_STREAM(stream), NULL, error);
    g_object_unref(stream);
    return pixbuf;
}

// Create a new renderer
ImageRenderer* renderer_create(void) {
    ImageRenderer *renderer = g_new0(ImageRenderer, 1);
    if (!renderer) {
        return NULL;
    }

    renderer->canvas = NULL;
    renderer->canvas_config = NULL;
    renderer->term_info = NULL;
    renderer->cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, 
                                           (GDestroyNotify)gstring_destroy);
    
    g_mutex_init(&renderer->cache_mutex);

    // Set default configuration
    renderer->config.max_width = 80;
    renderer->config.max_height = 24;
    renderer->config.preserve_aspect_ratio = TRUE;
    renderer->config.dither = TRUE;
    renderer->config.color_space = CHAFA_COLOR_SPACE_RGB;
    renderer->config.pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS;
    renderer->config.work_factor = 1;

    return renderer;
}

// Destroy renderer and free resources
void renderer_destroy(ImageRenderer *renderer) {
    if (!renderer) {
        return;
    }

    if (renderer->canvas) {
        chafa_canvas_unref(renderer->canvas);
    }
    
    if (renderer->canvas_config) {
        chafa_canvas_config_unref(renderer->canvas_config);
    }
    
    if (renderer->term_info) {
        chafa_term_info_unref(renderer->term_info);
    }

    if (renderer->cache) {
        g_hash_table_destroy(renderer->cache);
    }

    g_mutex_clear(&renderer->cache_mutex);
    g_free(renderer);
}

// Initialize renderer with configuration
ErrorCode renderer_initialize(ImageRenderer *renderer, const RendererConfig *config) {
    if (!renderer) {
        return ERROR_MEMORY_ALLOC;
    }

    // Update configuration if provided
    if (config) {
        renderer->config = *config;
    }

    // Get terminal information and detect capabilities
    ChafaTermDb *term_db = chafa_term_db_get_default();
    if (!term_db) {
        return ERROR_CHAFA_INIT;
    }

    gchar **envp = g_get_environ();
    renderer->term_info = chafa_term_db_detect(term_db, envp);
    g_strfreev(envp);
    
    if (!renderer->term_info) {
        return ERROR_CHAFA_INIT;
    }

    // Create canvas configuration
    renderer->canvas_config = chafa_canvas_config_new();
    if (!renderer->canvas_config) {
        return ERROR_CHAFA_INIT;
    }

    // Configure canvas with terminal-adaptive settings
    ChafaCanvasMode mode = chafa_term_info_get_best_canvas_mode(renderer->term_info);
    ChafaPixelMode pixel_mode = chafa_term_info_get_best_pixel_mode(renderer->term_info);
    
    chafa_canvas_config_set_canvas_mode(renderer->canvas_config, mode);
    chafa_canvas_config_set_pixel_mode(renderer->canvas_config, pixel_mode);
    chafa_canvas_config_set_geometry(renderer->canvas_config, 
                                    renderer->config.max_width, 
                                    renderer->config.max_height);
    chafa_canvas_config_set_color_space(renderer->canvas_config, renderer->config.color_space);
    
    // Set symbol map with safe symbols for the terminal
    ChafaSymbolMap *symbol_map = chafa_symbol_map_new();
    chafa_symbol_map_add_by_tags(symbol_map, chafa_term_info_get_safe_symbol_tags(renderer->term_info));
    chafa_canvas_config_set_symbol_map(renderer->canvas_config, symbol_map);
    chafa_symbol_map_unref(symbol_map);

    // Create canvas
    renderer->canvas = chafa_canvas_new(renderer->canvas_config);
    if (!renderer->canvas) {
        return ERROR_CHAFA_INIT;
    }

    return ERROR_NONE;
}

// Render an image file
GString* renderer_render_image_file(ImageRenderer *renderer, const char *filepath) {
    if (!renderer || !filepath) {
        return NULL;
    }

    // Check cache first
    g_mutex_lock(&renderer->cache_mutex);
    GString *cached = renderer_cache_get(renderer, filepath);
    g_mutex_unlock(&renderer->cache_mutex);
    
    if (cached) {
        // Return a copy so callers can own/free it without affecting cache
        return g_string_new_len(cached->str, cached->len);
    }

    // Load image using stream-based loader to support very long paths
    GError *error = NULL;
    GdkPixbuf *pixbuf = renderer_load_pixbuf_from_stream(filepath, &error);
    if (!pixbuf) {
        if (error) {
            g_error_free(error);
        }
        return NULL;
    }

    // Get image properties
    gint width = gdk_pixbuf_get_width(pixbuf);
    gint height = gdk_pixbuf_get_height(pixbuf);
    gint rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    gint n_channels = gdk_pixbuf_get_n_channels(pixbuf);
    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);

    // Always use RGBA format for consistent rendering
    GdkPixbuf *rgba_pixbuf = NULL;
    guchar *rgba_pixels = NULL;
    gint rgba_rowstride = 0;
    
    if (n_channels != 4) {
        // Convert to RGBA if needed
        rgba_pixbuf = gdk_pixbuf_add_alpha(pixbuf, 0, 0, width, height);
        if (!rgba_pixbuf) {
            g_object_unref(pixbuf);
            return NULL;
        }
        
        rgba_pixels = gdk_pixbuf_get_pixels(rgba_pixbuf);
        rgba_rowstride = gdk_pixbuf_get_rowstride(rgba_pixbuf);
        g_object_unref(pixbuf);
        
        width = gdk_pixbuf_get_width(rgba_pixbuf);
        height = gdk_pixbuf_get_height(rgba_pixbuf);
        rowstride = rgba_rowstride;
        pixels = rgba_pixels;
        n_channels = 4;
    }

    // Render the image
    GString *result = renderer_render_image_data(renderer, pixels, width, height, rowstride);
    
    if (rgba_pixbuf) {
        g_object_unref(rgba_pixbuf);
    } else {
        g_object_unref(pixbuf);
    }

    if (result) {
        // Cache the result
        g_mutex_lock(&renderer->cache_mutex);
        renderer_cache_add(renderer, filepath, result);
        g_mutex_unlock(&renderer->cache_mutex);
    }

    return result;
}

// Render image data directly
GString* renderer_render_image_data(ImageRenderer *renderer, 
                                   const guint8 *pixel_data, 
                                   gint width, 
                                   gint height, 
                                   gint rowstride) {
    if (!renderer || !pixel_data || width <= 0 || height <= 0) {
        return NULL;
    }

    // Setup canvas for this image
    ErrorCode error = renderer_setup_canvas(renderer, width, height);
    if (error != ERROR_NONE) {
        return NULL;
    }

    // Draw pixels to canvas
    chafa_canvas_draw_all_pixels(renderer->canvas, CHAFA_PIXEL_RGBA8_UNASSOCIATED, 
                                pixel_data, width, height, rowstride);

    // Generate output
    GString *output = chafa_canvas_print(renderer->canvas, renderer->term_info);
    
    return output;
}

// Setup canvas for this image
ErrorCode renderer_setup_canvas(ImageRenderer *renderer, gint width, gint height) {
    if (!renderer) {
        return ERROR_MEMORY_ALLOC;
    }

    // Calculate output dimensions preserving aspect ratio if requested
    gint output_width = renderer->config.max_width;
    gint output_height = renderer->config.max_height;

    if (renderer->config.preserve_aspect_ratio) {
        // Calculate font aspect ratio dynamically based on terminal pixel dimensions
        gdouble font_ratio = get_terminal_cell_aspect_ratio();
        
        // Calculate geometry that fits within bounds
        chafa_calc_canvas_geometry(width, height, &output_width, &output_height, 
                                   font_ratio, TRUE, FALSE);
        
        // Ensure we don't exceed the maximum dimensions
        if (output_width > renderer->config.max_width) {
            output_width = renderer->config.max_width;
            output_height = (gint)(height * (gdouble)output_width / width);
        }
        if (output_height > renderer->config.max_height) {
            output_height = renderer->config.max_height;
            output_width = (gint)(width * (gdouble)output_height / height);
        }
    }

    // Update canvas geometry
    chafa_canvas_config_set_geometry(renderer->canvas_config, output_width, output_height);
    // Set cell geometry so chafa can respect real pixel sizes when available
    gint cell_w = 0, cell_h = 0;
    get_terminal_cell_geometry(&cell_w, &cell_h);
    if (cell_w > 0 && cell_h > 0) {
        chafa_canvas_config_set_cell_geometry(renderer->canvas_config, cell_w, cell_h);
    }
    
    // Re-create canvas with new configuration
    if (renderer->canvas) {
        chafa_canvas_unref(renderer->canvas);
    }
    renderer->canvas = chafa_canvas_new(renderer->canvas_config);

    return ERROR_NONE;
}

// Add rendered image to cache
void renderer_cache_add(ImageRenderer *renderer, const char *filepath, GString *rendered) {
    if (!renderer || !filepath || !rendered) {
        return;
    }

    gchar *key = g_strdup(filepath);
    GString *value = g_string_new_len(rendered->str, rendered->len);
    
    g_hash_table_insert(renderer->cache, key, value);
}

// Get rendered image from cache
GString* renderer_cache_get(ImageRenderer *renderer, const char *filepath) {
    if (!renderer || !filepath) {
        return NULL;
    }

    return g_hash_table_lookup(renderer->cache, filepath);
}

// Clear all cached images
void renderer_cache_clear(ImageRenderer *renderer) {
    if (!renderer) {
        return;
    }

    g_hash_table_remove_all(renderer->cache);
}

// Cleanup old cache entries (LRU eviction)
void renderer_cache_cleanup(ImageRenderer *renderer) {
    if (!renderer) {
        return;
    }

    guint size = g_hash_table_size(renderer->cache);
    if (size <= MAX_CACHE_SIZE) {
        return;
    }

    // Remove oldest entries to maintain cache size limit
    guint target_size = MAX_CACHE_SIZE * 3 / 4; // Remove 25% when over limit
    guint to_remove = size - target_size;
    
    GHashTableIter iter;
    gpointer key, value;
    guint removed = 0;

    g_hash_table_iter_init(&iter, renderer->cache);
    while (g_hash_table_iter_next(&iter, &key, &value) && removed < to_remove) {
        g_hash_table_iter_remove(&iter);
        removed++;
    }
}

// Update terminal size information
ErrorCode renderer_update_terminal_size(ImageRenderer *renderer) {
    if (!renderer) {
        return ERROR_MEMORY_ALLOC;
    }

    if (renderer->term_info) {
        chafa_term_info_unref(renderer->term_info);
    }

    ChafaTermDb *term_db = chafa_term_db_get_default();
    if (!term_db) {
        return ERROR_CHAFA_INIT;
    }

    renderer->term_info = chafa_term_db_detect(term_db, NULL);
    if (!renderer->term_info) {
        return ERROR_CHAFA_INIT;
    }

    // Update canvas configuration with new terminal size
    gint width, height;
    get_terminal_size(&width, &height);
    
    renderer->config.max_width = width;
    renderer->config.max_height = height - 3; // Leave space for UI

    return ERROR_NONE;
}

// Check if image file is supported
gboolean renderer_is_image_supported(const char *filepath) {
    return is_image_file(filepath);
}

// Get image dimensions
ErrorCode renderer_get_image_dimensions(const char *filepath, gint *width, gint *height) {
    if (!filepath || !width || !height) {
        return ERROR_INVALID_IMAGE;
    }

    GError *error = NULL;
    GdkPixbuf *pixbuf = renderer_load_pixbuf_from_stream(filepath, &error);
    if (!pixbuf) {
        if (error) {
            g_error_free(error);
        }
        return ERROR_INVALID_IMAGE;
    }

    *width = gdk_pixbuf_get_width(pixbuf);
    *height = gdk_pixbuf_get_height(pixbuf);
    
    g_object_unref(pixbuf);
    return ERROR_NONE;
}





// Get rendered image dimensions
void renderer_get_rendered_dimensions(ImageRenderer *renderer, gint *width, gint *height) {
    if (!renderer || !width || !height) {
        return;
    }
    
    if (renderer->canvas) {
        chafa_canvas_config_get_geometry(renderer->canvas_config, width, height);
    } else {
        *width = renderer->config.max_width;
        *height = renderer->config.max_height;
    }
}
