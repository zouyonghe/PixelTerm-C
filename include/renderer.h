#ifndef RENDERER_H
#define RENDERER_H

#include "common.h"

// Renderer configuration
typedef struct {
    gint max_width;
    gint max_height;
    gboolean preserve_aspect_ratio;
    gboolean dither;
    ChafaColorSpace color_space;
    ChafaPixelMode pixel_mode;
    gint work_factor;
} RendererConfig;

// Renderer structure
typedef struct {
    ChafaCanvas *canvas;
    ChafaCanvasConfig *canvas_config;
    ChafaTermInfo *term_info;
    RendererConfig config;
    GHashTable *cache;
    GMutex cache_mutex;
} ImageRenderer;

// Renderer lifecycle functions
ImageRenderer* renderer_create(void);
void renderer_destroy(ImageRenderer *renderer);
ErrorCode renderer_initialize(ImageRenderer *renderer, const RendererConfig *config);

// Rendering functions
GString* renderer_render_image_file(ImageRenderer *renderer, const char *filepath);
GString* renderer_render_image_data(ImageRenderer *renderer, 
                                   const guint8 *pixel_data, 
                                   gint width, 
                                   gint height, 
                                   gint rowstride);
ErrorCode renderer_setup_canvas(ImageRenderer *renderer, gint width, gint height);

// Cache management
void renderer_cache_add(ImageRenderer *renderer, const char *filepath, GString *rendered);
GString* renderer_cache_get(ImageRenderer *renderer, const char *filepath);
void renderer_cache_clear(ImageRenderer *renderer);
void renderer_cache_cleanup(ImageRenderer *renderer);

// Configuration functions
ErrorCode renderer_update_terminal_size(ImageRenderer *renderer);
ErrorCode renderer_set_max_dimensions(ImageRenderer *renderer, gint width, gint height);
ErrorCode renderer_set_pixel_mode(ImageRenderer *renderer, ChafaPixelMode mode);
ErrorCode renderer_set_color_space(ImageRenderer *renderer, ChafaColorSpace space);

// Utility functions
gboolean renderer_is_image_supported(const char *filepath);
ErrorCode renderer_get_image_dimensions(const char *filepath, gint *width, gint *height);
void renderer_print_usage_info(void);

// Terminal detection
gboolean terminal_supports_graphics(void);
gboolean terminal_supports_sixel(void);
gboolean terminal_supports_kitty(void);
gint renderer_get_terminal_width(void);
gint renderer_get_terminal_height(void);

#endif // RENDERER_H