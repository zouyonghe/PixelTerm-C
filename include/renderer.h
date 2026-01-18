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
    gint work_factor;
    gboolean force_sixel;
    gboolean force_kitty;
    
    // Advanced quality settings
    ChafaDitherMode dither_mode;
    ChafaColorExtractor color_extractor;
    ChafaOptimizations optimizations;
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
/**
 * @brief Creates a new `ImageRenderer` instance.
 * 
 * Allocates memory for a new `ImageRenderer` structure and initializes its
 * members to default values, including the cache and its mutex.
 * 
 * @return A pointer to the newly created `ImageRenderer` instance on success,
 *         or NULL if memory allocation fails.
 */
ImageRenderer* renderer_create(void);
/**
 * @brief Destroys an `ImageRenderer` instance and frees all associated resources.
 * 
 * This includes unreferencing Chafa objects (canvas, config, term info) and
 * clearing and destroying the internal cache.
 * 
 * @param renderer A pointer to the `ImageRenderer` instance to destroy.
 */
void renderer_destroy(ImageRenderer *renderer);
/**
 * @brief Initializes an `ImageRenderer` instance with specified configuration.
 * 
 * Sets up the Chafa terminal information and creates the Chafa canvas and
 * its configuration based on the provided `RendererConfig`.
 * 
 * @param renderer A pointer to the `ImageRenderer` instance to initialize.
 * @param config A pointer to the `RendererConfig` to use for setup.
 * @return `ERROR_NONE` on success, or `ERROR_CHAFA_INIT` if Chafa initialization fails.
 */
ErrorCode renderer_initialize(ImageRenderer *renderer, const RendererConfig *config);

// Rendering functions
/**
 * @brief Renders an image from a file to an ANSI string.
 * 
 * This function loads the image from the specified filepath, scales it
 * according to the renderer's configuration, and converts it into an
 * ANSI escape sequence string suitable for terminal display.
 * 
 * @param renderer A pointer to the `ImageRenderer` instance.
 * @param filepath The path to the image file to render.
 * @return A newly allocated `GString` containing the ANSI-rendered image,
 *         or NULL if the image cannot be loaded or rendered. The caller
 *         is responsible for freeing the returned `GString`.
 */
GString* renderer_render_image_file(ImageRenderer *renderer, const char *filepath);
/**
 * @brief Renders raw pixel data to an ANSI string.
 * 
 * This function takes raw pixel data, scales it according to the renderer's
 * configuration, and converts it into an ANSI escape sequence string
 * suitable for terminal display.
 * 
 * @param renderer A pointer to the `ImageRenderer` instance.
 * @param pixel_data A pointer to the raw pixel data.
 * @param width The width of the pixel data in pixels.
 * @param height The height of the pixel data in pixels.
 * @param rowstride The number of bytes per row in `pixel_data`.
 * @param n_channels The number of color channels per pixel (e.g., 3 for RGB, 4 for RGBA).
 * @return A newly allocated `GString` containing the ANSI-rendered image,
 *         or NULL if rendering fails. The caller is responsible for freeing
 *         the returned `GString`.
 */
GString* renderer_render_image_data(ImageRenderer *renderer, 
                                   const guint8 *pixel_data, 
                                   gint width, 
                                   gint height, 
                                   gint rowstride,
                                   gint n_channels);
/**
 * @brief Sets up or updates the Chafa canvas for rendering.
 * 
 * This function configures the Chafa canvas with the given dimensions,
 * which typically correspond to the usable terminal area for image display.
 * 
 * @param renderer A pointer to the `ImageRenderer` instance.
 * @param width The width of the canvas in characters.
 * @param height The height of the canvas in characters.
 * @return `ERROR_NONE` on success.
 */
ErrorCode renderer_setup_canvas(ImageRenderer *renderer, gint width, gint height);

// Cache management
/**
 * @brief Adds a rendered image to the renderer's internal cache.
 * 
 * This cache stores `GString` representations of rendered images,
 * typically used to avoid redundant rendering operations.
 * 
 * @param renderer A pointer to the `ImageRenderer` instance.
 * @param filepath The path to the image file.
 * @param rendered A `GString` containing the ANSI-rendered image. The cache
 *                 takes ownership of this `GString`.
 */
void renderer_cache_add(ImageRenderer *renderer, const char *filepath, GString *rendered);
/**
 * @brief Retrieves a rendered image from the renderer's internal cache.
 * 
 * @param renderer A pointer to the `ImageRenderer` instance.
 * @param filepath The path to the image file.
 * @return A `GString` containing the ANSI-rendered image on cache hit, or NULL on cache miss.
 *         The returned `GString` is owned by the cache and should not be freed by the caller.
 */
GString* renderer_cache_get(ImageRenderer *renderer, const char *filepath);
/**
 * @brief Clears all entries from the renderer's internal cache.
 * 
 * @param renderer A pointer to the `ImageRenderer` instance.
 */
void renderer_cache_clear(ImageRenderer *renderer);
// Configuration functions
/**
 * @brief Updates the terminal size information used by the renderer.
 * 
 * This function should be called when the terminal dimensions change to ensure
 * that images are rendered appropriately for the new size. It updates the
 * internal `term_info` and `canvas_config` with the latest terminal metrics.
 * 
 * @param renderer A pointer to the `ImageRenderer` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if
 *         terminal information cannot be updated.
 */
ErrorCode renderer_update_terminal_size(ImageRenderer *renderer);

// Utility functions
/**
 * @brief Checks if an image file is supported for rendering.
 * 
 * This function uses the `is_valid_image_file` utility to determine
 * if the file is a recognized and supported image format.
 * 
 * @param filepath The path to the image file.
 * @return `TRUE` if the image is supported, `FALSE` otherwise.
 */
gboolean renderer_is_image_supported(const char *filepath);
/**
 * @brief Retrieves the original pixel dimensions of an image file.
 * 
 * @param filepath The path to the image file.
 * @param width A pointer to an integer where the image width (in pixels) will be stored.
 * @param height A pointer to an integer where the image height (in pixels) will be stored.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         file is not found or its dimensions cannot be determined.
 */
ErrorCode renderer_get_image_dimensions(const char *filepath, gint *width, gint *height);
ErrorCode renderer_get_media_dimensions(const char *filepath, gint *width, gint *height);
/**
 * @brief Retrieves the actual dimensions (in characters) that the last image was rendered to.
 * 
 * This function provides the effective character width and height used for the
 * last rendering operation, taking into account scaling and terminal constraints.
 * 
 * @param renderer A pointer to the `ImageRenderer` instance.
 * @param width A pointer to an integer where the rendered width (in characters) will be stored.
 * @param height A pointer to an integer where the rendered height (in characters) will be stored.
 */
void renderer_get_rendered_dimensions(ImageRenderer *renderer, gint *width, gint *height);




#endif // RENDERER_H
