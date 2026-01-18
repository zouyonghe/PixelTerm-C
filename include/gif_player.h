#ifndef GIF_PLAYER_H
#define GIF_PLAYER_H

#include "common.h"
#include "renderer.h"
#include <gdk-pixbuf/gdk-pixbuf.h>

// GIF 动画播放器结构
typedef struct {
    gboolean is_playing;
    gboolean is_animated;
    gint current_frame;
    gint total_frames;
    gint frame_delay;  // in milliseconds
    gint loop_count;
    gint current_loop;
    guint timer_id;    // For storing timer source ID
    ChafaCanvas *canvas;  // Canvas for rendering frames
    gchar *filepath;
    
    // Animation state
    GdkPixbufAnimation *animation;
    GdkPixbufAnimationIter *iter;
    
    // Renderer reference
    ImageRenderer *renderer;
    gboolean owns_renderer;

    // Render layout for single image mode
    gint render_area_top_row;
    gint render_area_height;
    gint render_max_width;
    gint render_max_height;
    gint render_term_width;
    gint render_term_height;
    gboolean render_layout_valid;
    gint last_frame_top_row;
    gint last_frame_height;
    gint fixed_frame_top_row;
    gboolean fixed_frame_valid;
} GifPlayer;

// GIF Player functions
/**
 * @brief Creates a new `GifPlayer` instance.
 * 
 * Allocates memory for a new `GifPlayer` structure and initializes its
 * members to default values (e.g., NULL pointers, zero counts).
 * 
 * @return A pointer to the newly created `GifPlayer` instance on success,
 *         or NULL if memory allocation fails.
 */
GifPlayer* gif_player_new(gint work_factor, gboolean force_text, gboolean force_sixel, gboolean force_kitty,
                          gboolean force_iterm2, gdouble gamma);
/**
 * @brief Destroys a `GifPlayer` instance and frees all associated resources.
 * 
 * This function stops any active GIF playback and cleans up all allocated
 * memory, including the GdkPixbufAnimation and ImageRenderer.
 * 
 * @param player A pointer to the `GifPlayer` instance to destroy.
 */
void gif_player_destroy(GifPlayer *player);
/**
 * @brief Sets the image renderer to be used by the GIF player.
 * 
 * This allows the GIF player to use a specific `ImageRenderer` instance
 * for processing and rendering GIF frames. The caller retains ownership
 * of the renderer provided here.
 * 
 * @param player A pointer to the `GifPlayer` instance.
 * @param renderer A pointer to the `ImageRenderer` instance to set.
 */
void gif_player_set_renderer(GifPlayer *player, ImageRenderer *renderer);
/**
 * @brief Sets the render area to avoid overwriting UI text during playback.
 *
 * @param player A pointer to the `GifPlayer` instance.
 * @param term_width Terminal width in characters.
 * @param term_height Terminal height in characters.
 * @param area_top_row Top row (1-based) of the image area.
 * @param area_height Height of the image area in rows.
 * @param max_width Maximum render width in characters.
 * @param max_height Maximum render height in characters.
 */
void gif_player_set_render_area(GifPlayer *player,
                                gint term_width,
                                gint term_height,
                                gint area_top_row,
                                gint area_height,
                                gint max_width,
                                gint max_height);
/**
 * @brief Loads a GIF file for playback.
 * 
 * This function loads the specified GIF file, determines if it's animated,
 * and extracts relevant animation properties such as total frames, frame delays,
 * and loop count. Any previously loaded GIF is stopped and unloaded.
 * 
 * @param player A pointer to the `GifPlayer` instance.
 * @param filepath The path to the GIF file to load.
 * @return `ERROR_NONE` on successful loading, or an appropriate `ErrorCode`
 *         if the file is not found, is not a valid GIF, or loading fails.
 */
ErrorCode gif_player_load(GifPlayer *player, const gchar *filepath);
/**
 * @brief Starts or resumes playback of the loaded animated GIF.
 * 
 * If a GIF is loaded and is animated, this function initiates the timer
 * to sequentially display frames, creating an animation effect.
 * 
 * @param player A pointer to the `GifPlayer` instance.
 * @return `ERROR_NONE` on success, or `ERROR_INVALID_IMAGE` if no animated
 *         GIF is loaded.
 */
ErrorCode gif_player_play(GifPlayer *player);
/**
 * @brief Pauses playback of the loaded animated GIF.
 * 
 * This function stops the timer responsible for advancing GIF frames,
 * effectively pausing the animation at its current frame.
 * 
 * @param player A pointer to the `GifPlayer` instance.
 * @return `ERROR_NONE` on success.
 */
ErrorCode gif_player_pause(GifPlayer *player);
/**
 * @brief Stops playback of the loaded animated GIF and resets its state.
 * 
 * This function halts the animation, resets the current frame to the beginning,
 * and clears any animation-related internal state.
 * 
 * @param player A pointer to the `GifPlayer` instance.
 * @return `ERROR_NONE` on success.
 */
ErrorCode gif_player_stop(GifPlayer *player);
/**
 * @brief Checks if the GIF player is currently playing an animation.
 * 
 * @param player A pointer to the constant `GifPlayer` instance.
 * @return `TRUE` if an animation is active, `FALSE` otherwise.
 */
gboolean gif_player_is_playing(const GifPlayer *player);
/**
 * @brief Checks if the currently loaded GIF is animated.
 * 
 * @param player A pointer to the constant `GifPlayer` instance.
 * @return `TRUE` if an animated GIF is loaded, `FALSE` otherwise.
 */
gboolean gif_player_is_animated(const GifPlayer *player);
/**
 * @brief Updates the GIF player's internal terminal size information.
 * 
 * This function should be called when the terminal dimensions change to ensure
 * that GIF frames are rendered correctly according to the new size.
 * 
 * @param player A pointer to the `GifPlayer` instance.
 * @return `ERROR_NONE` on success.
 */
ErrorCode gif_player_update_terminal_size(GifPlayer *player);

#endif // GIF_PLAYER_H
