#include "renderer.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <sys/ioctl.h>

#include "video_player.h"

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
    renderer->config.dither = FALSE;
    renderer->config.color_space = CHAFA_COLOR_SPACE_RGB;
    renderer->config.force_text = FALSE;
    renderer->config.force_sixel = FALSE;
    renderer->config.force_kitty = FALSE;
    renderer->config.force_iterm2 = FALSE;
    
    // Maximize quality settings
    renderer->config.work_factor = 9; // High CPU usage for best character matching
    renderer->config.dither_mode = CHAFA_DITHER_MODE_NONE;
    renderer->config.color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE;
    renderer->config.optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES;

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
    // Force TrueColor support detection by injecting/overriding environment variables
    envp = g_environ_setenv(envp, "COLORTERM", "truecolor", TRUE);
    // Ensure we have a capable TERM if not already set to something good
    const gchar *term_env = g_environ_getenv(envp, "TERM");
    if (!term_env || g_strcmp0(term_env, "dumb") == 0) {
        envp = g_environ_setenv(envp, "TERM", "xterm-256color", TRUE);
    }
    renderer->term_info = chafa_term_db_detect(term_db, envp);
    g_strfreev(envp);
    
    if (!renderer->term_info) {
        return ERROR_CHAFA_INIT;
    }

    gboolean force_text_mode = renderer->config.force_text;
    gboolean force_kitty_mode = renderer->config.force_kitty && !force_text_mode;
    gboolean force_iterm2_mode = renderer->config.force_iterm2 && !force_kitty_mode && !force_text_mode;
    gboolean force_sixel_mode = renderer->config.force_sixel && !force_kitty_mode && !force_iterm2_mode && !force_text_mode;
    if (force_sixel_mode) {
        ChafaTermInfo *fallback = chafa_term_db_get_fallback_info(term_db);
        if (fallback) {
            chafa_term_info_supplement(renderer->term_info, fallback);
            chafa_term_info_unref(fallback);
        }
    }

    // Create canvas configuration
    renderer->canvas_config = chafa_canvas_config_new();
    if (!renderer->canvas_config) {
        return ERROR_CHAFA_INIT;
    }

    // Configure canvas with terminal-adaptive settings
    ChafaCanvasMode mode = chafa_term_info_get_best_canvas_mode(renderer->term_info);
    ChafaPixelMode pixel_mode = chafa_term_info_get_best_pixel_mode(renderer->term_info);
    if (force_text_mode) {
        pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS;
        mode = CHAFA_CANVAS_MODE_TRUECOLOR;
    } else if (force_kitty_mode) {
        pixel_mode = CHAFA_PIXEL_MODE_KITTY;
        mode = CHAFA_CANVAS_MODE_TRUECOLOR;
    } else if (force_iterm2_mode) {
        pixel_mode = CHAFA_PIXEL_MODE_ITERM2;
        mode = CHAFA_CANVAS_MODE_TRUECOLOR;
    } else if (force_sixel_mode) {
        pixel_mode = CHAFA_PIXEL_MODE_SIXELS;
        mode = CHAFA_CANVAS_MODE_TRUECOLOR;
    } else if (pixel_mode != CHAFA_PIXEL_MODE_SYMBOLS) {
        mode = CHAFA_CANVAS_MODE_TRUECOLOR;
    }
    
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
    
    // Apply quality settings
    ChafaDitherMode dither_mode = CHAFA_DITHER_MODE_NONE;
    if (renderer->config.dither) {
        dither_mode = renderer->config.dither_mode;
    } else if (pixel_mode == CHAFA_PIXEL_MODE_SIXELS) {
        dither_mode = CHAFA_DITHER_MODE_NOISE;
    }
    chafa_canvas_config_set_dither_mode(renderer->canvas_config, dither_mode);
    if (pixel_mode != CHAFA_PIXEL_MODE_SYMBOLS) {
        chafa_canvas_config_set_dither_grain_size(renderer->canvas_config, 1, 1);
    }
    chafa_canvas_config_set_color_extractor(renderer->canvas_config, renderer->config.color_extractor);
    gint work_factor = renderer->config.work_factor;
    if (work_factor < 1) {
        work_factor = 1;
    } else if (work_factor > 9) {
        work_factor = 9;
    }
    // Normalize 1-9 input to chafa's 0.0-1.0 scale.
    chafa_canvas_config_set_work_factor(renderer->canvas_config, (float)(work_factor - 1) / 8.0f);
    chafa_canvas_config_set_optimizations(renderer->canvas_config, renderer->config.optimizations);

    // Create canvas from the configured settings
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

    // Render the image
    GString *result = renderer_render_image_data(renderer, pixels, width, height, rowstride, n_channels);
    
    g_object_unref(pixbuf);

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
                                   gint rowstride,
                                   gint n_channels) {
    if (!renderer || !pixel_data || width <= 0 || height <= 0) {
        return NULL;
    }

    // Setup canvas for this image
    ErrorCode error = renderer_setup_canvas(renderer, width, height);
    if (error != ERROR_NONE) {
        return NULL;
    }

    ChafaPixelType pixel_type;
    if (n_channels == 4) {
        pixel_type = CHAFA_PIXEL_RGBA8_UNASSOCIATED;
    } else if (n_channels == 3) {
        pixel_type = CHAFA_PIXEL_RGB8;
    } else {
        // Unsupported format
        return NULL;
    }

    // Draw pixels to canvas
    chafa_canvas_draw_all_pixels(renderer->canvas, pixel_type, 
                                pixel_data, width, height, rowstride);

    // Generate output - use NULL for term_info to force generic ANSI output with RGB
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

    // Force TrueColor support detection
    gchar **envp = g_get_environ();
    envp = g_environ_setenv(envp, "COLORTERM", "truecolor", TRUE);
    const gchar *term_env = g_environ_getenv(envp, "TERM");
    if (!term_env || g_strcmp0(term_env, "dumb") == 0) {
        envp = g_environ_setenv(envp, "TERM", "xterm-256color", TRUE);
    }
    renderer->term_info = chafa_term_db_detect(term_db, envp);
    g_strfreev(envp);

    if (!renderer->term_info) {
        return ERROR_CHAFA_INIT;
    }

    gboolean force_text_mode = renderer->config.force_text;
    gboolean force_kitty_mode = renderer->config.force_kitty && !force_text_mode;
    gboolean force_iterm2_mode = renderer->config.force_iterm2 && !force_kitty_mode && !force_text_mode;
    gboolean force_sixel_mode = renderer->config.force_sixel && !force_kitty_mode && !force_iterm2_mode && !force_text_mode;
    if (force_sixel_mode) {
        ChafaTermInfo *fallback = chafa_term_db_get_fallback_info(term_db);
        if (fallback) {
            chafa_term_info_supplement(renderer->term_info, fallback);
            chafa_term_info_unref(fallback);
        }
    }

    // Apply best modes from the new terminal info
    if (renderer->canvas_config) {
        ChafaCanvasMode mode = chafa_term_info_get_best_canvas_mode(renderer->term_info);
        ChafaPixelMode pixel_mode = chafa_term_info_get_best_pixel_mode(renderer->term_info);
        if (force_text_mode) {
            pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS;
            mode = CHAFA_CANVAS_MODE_TRUECOLOR;
        } else if (force_kitty_mode) {
            pixel_mode = CHAFA_PIXEL_MODE_KITTY;
            mode = CHAFA_CANVAS_MODE_TRUECOLOR;
        } else if (force_iterm2_mode) {
            pixel_mode = CHAFA_PIXEL_MODE_ITERM2;
            mode = CHAFA_CANVAS_MODE_TRUECOLOR;
        } else if (force_sixel_mode) {
            pixel_mode = CHAFA_PIXEL_MODE_SIXELS;
            mode = CHAFA_CANVAS_MODE_TRUECOLOR;
        } else if (pixel_mode != CHAFA_PIXEL_MODE_SYMBOLS) {
            mode = CHAFA_CANVAS_MODE_TRUECOLOR;
        }
        
        chafa_canvas_config_set_canvas_mode(renderer->canvas_config, mode);
        chafa_canvas_config_set_pixel_mode(renderer->canvas_config, pixel_mode);
        
        // Ensure color space is maintained
        chafa_canvas_config_set_color_space(renderer->canvas_config, renderer->config.color_space);

        // Refresh symbol map based on new terminal capabilities
        ChafaSymbolMap *symbol_map = chafa_symbol_map_new();
        chafa_symbol_map_add_by_tags(symbol_map, chafa_term_info_get_safe_symbol_tags(renderer->term_info));
        chafa_canvas_config_set_symbol_map(renderer->canvas_config, symbol_map);
        chafa_symbol_map_unref(symbol_map);

        ChafaDitherMode dither_mode = CHAFA_DITHER_MODE_NONE;
        if (renderer->config.dither) {
            dither_mode = renderer->config.dither_mode;
        } else if (pixel_mode == CHAFA_PIXEL_MODE_SIXELS) {
            dither_mode = CHAFA_DITHER_MODE_NOISE;
        }
        chafa_canvas_config_set_dither_mode(renderer->canvas_config, dither_mode);
        if (pixel_mode != CHAFA_PIXEL_MODE_SYMBOLS) {
            chafa_canvas_config_set_dither_grain_size(renderer->canvas_config, 1, 1);
        }
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
    return is_media_file(filepath);
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

ErrorCode renderer_get_media_dimensions(const char *filepath, gint *width, gint *height) {
    if (!filepath || !width || !height) {
        return ERROR_INVALID_IMAGE;
    }

    if (is_video_file(filepath)) {
        return video_player_get_dimensions(filepath, width, height);
    }

    ErrorCode image_error = renderer_get_image_dimensions(filepath, width, height);
    if (image_error == ERROR_NONE) {
        return ERROR_NONE;
    }

    if (is_valid_video_file(filepath)) {
        return video_player_get_dimensions(filepath, width, height);
    }

    return image_error;
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
