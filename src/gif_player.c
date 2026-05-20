#define _GNU_SOURCE

#include "gif_player.h"
#include "common.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <chafa.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <gio/gio.h>

// Suppress deprecation warnings for GdkPixbufAnimation and GTimeVal
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static void gif_player_clear_line_cache(GifPlayer *player) {
    if (!player || !player->last_frame_lines) {
        return;
    }

    g_ptr_array_free(player->last_frame_lines, TRUE);
    player->last_frame_lines = NULL;
}

static void gif_player_present_rendered_frame(GifPlayer *player,
                                              const GString *result,
                                              gint rendered_w,
                                              gint rendered_h,
                                              gboolean graphics_mode) {
    if (!player || !result) {
        return;
    }

    if (player->render_layout_valid && player->render_area_top_row > 0 && player->render_area_height > 0) {
        gint term_w = player->render_term_width > 0 ? player->render_term_width : player->render_max_width;
        gint term_h = player->render_term_height;
        gint area_top = player->render_area_top_row;
        gint area_bottom = area_top + player->render_area_height - 1;
        if (term_h > 0 && area_bottom > term_h) {
            area_bottom = term_h;
        }

        gint effective_w = rendered_w > 0 ? rendered_w : player->render_max_width;
        if (term_w > 0 && effective_w > term_w) {
            effective_w = term_w;
        }
        gint left_pad = (term_w > effective_w) ? (term_w - effective_w) / 2 : 0;
        if (left_pad < 0) {
            left_pad = 0;
        }

        gint image_top_row = area_top;
        if (player->fixed_frame_valid) {
            image_top_row = player->fixed_frame_top_row;
        } else if (player->render_area_height > 0 && rendered_h > 0 && rendered_h < player->render_area_height) {
            gint vpad = (player->render_area_height - rendered_h) / 2;
            if (vpad > 0) {
                image_top_row = area_top + vpad;
            }
            player->fixed_frame_top_row = image_top_row;
            player->fixed_frame_valid = TRUE;
        } else {
            player->fixed_frame_top_row = image_top_row;
            player->fixed_frame_valid = TRUE;
        }

        gint row = image_top_row;
        gint lines_printed = 0;
        gboolean has_newline = !graphics_mode && memchr(result->str, '\n', result->len) != NULL;

        if (graphics_mode) {
            gif_player_clear_line_cache(player);
            gint col = 1 + left_pad;
            if (col < 1) {
                col = 1;
            }
            printf("\033[%d;%dH", row, col);
            if (result->len > 0) {
                fwrite(result->str, 1, result->len, stdout);
            }
            lines_printed = rendered_h > 0 ? rendered_h : 1;
        } else if (!has_newline) {
            gif_player_clear_line_cache(player);
            printf("\033[%d;1H", row);
            if (left_pad > 0) {
                gchar *pad_buffer = g_malloc(left_pad);
                memset(pad_buffer, ' ', left_pad);
                fwrite(pad_buffer, 1, left_pad, stdout);
                g_free(pad_buffer);
            }
            if (result->len > 0) {
                fwrite(result->str, 1, result->len, stdout);
            }
            lines_printed = rendered_h > 0 ? rendered_h : 1;
        } else {
            GPtrArray *new_lines = g_ptr_array_new_with_free_func(g_free);
            const gchar *line_ptr = result->str;
            const gchar *end = result->str + result->len;
            gint line_index = 0;

            while (line_ptr && line_ptr < end && row <= area_bottom) {
                const gchar *newline = memchr(line_ptr, '\n', end - line_ptr);
                gint line_len = newline ? (gint)(newline - line_ptr) : (gint)(end - line_ptr);
                gint full_len = left_pad + line_len;
                gchar *full_line = g_malloc(full_len + 1);
                if (left_pad > 0) {
                    memset(full_line, ' ', left_pad);
                }
                if (line_len > 0) {
                    memcpy(full_line + left_pad, line_ptr, line_len);
                }
                full_line[full_len] = '\0';

                const gchar *prev_line = NULL;
                if (player->last_frame_lines && line_index < (gint)player->last_frame_lines->len) {
                    prev_line = g_ptr_array_index(player->last_frame_lines, line_index);
                }
                if (!prev_line || strcmp(prev_line, full_line) != 0) {
                    printf("\033[%d;1H\033[2K", row);
                    if (full_len > 0) {
                        fwrite(full_line, 1, full_len, stdout);
                    }
                }

                g_ptr_array_add(new_lines, full_line);
                lines_printed++;
                line_index++;
                if (!newline) {
                    break;
                }
                line_ptr = newline + 1;
                row++;
            }

            gif_player_clear_line_cache(player);
            player->last_frame_lines = new_lines;
        }

        if (player->last_frame_height > 0) {
            gint prev_top = player->last_frame_top_row;
            gint prev_bottom = prev_top + player->last_frame_height - 1;
            gint new_top = image_top_row;
            gint new_bottom = image_top_row + (lines_printed > 0 ? (lines_printed - 1) : -1);
            if (prev_top < area_top) {
                prev_top = area_top;
            }
            if (prev_bottom > area_bottom) {
                prev_bottom = area_bottom;
            }
            for (gint r = prev_top; r <= prev_bottom; r++) {
                if (r < new_top || r > new_bottom) {
                    printf("\033[%d;1H\033[2K", r);
                }
            }
        }

        player->last_frame_top_row = image_top_row;
        player->last_frame_height = lines_printed > 0 ? lines_printed : 0;
    } else {
        gif_player_clear_line_cache(player);
        printf("\033[H");
        if (result->len > 0) {
            fwrite(result->str, 1, result->len, stdout);
        }
        printf("\033[J");
        player->last_frame_top_row = 0;
        player->last_frame_height = 0;
    }

    fflush(stdout);
}

// Create a new GIF player instance
GifPlayer* gif_player_new(gint work_factor, gboolean force_text, gboolean force_sixel, gboolean force_kitty,
                          gboolean force_iterm2, TextSymbolMode text_symbol_mode, gdouble gamma) {
    GifPlayer *player = g_new0(GifPlayer, 1);
    if (!player) {
        return NULL;
    }

    player->is_playing = FALSE;
    player->is_animated = FALSE;
    player->current_frame = 0;
    player->total_frames = 0;
    player->frame_delay = 100;
    player->loop_count = 0;
    player->current_loop = 0;
    player->timer_id = 0;
    player->canvas = NULL;
    player->filepath = NULL;
    player->animation = NULL;
    player->iter = NULL;
    player->render_area_top_row = 0;
    player->render_area_height = 0;
    player->render_max_width = 0;
    player->render_max_height = 0;
    player->render_term_width = 0;
    player->render_term_height = 0;
    player->render_layout_valid = FALSE;
    player->last_frame_top_row = 0;
    player->last_frame_height = 0;
    player->fixed_frame_top_row = 0;
    player->fixed_frame_valid = FALSE;
    player->last_frame_lines = NULL;
    player->owns_renderer = FALSE;
    
    // Initialize internal renderer
    player->renderer = renderer_create();
    if (work_factor < 1) {
        work_factor = 1;
    } else if (work_factor > 9) {
        work_factor = 9;
    }

    if (player->renderer) {
        RendererConfig config = {
            .max_width = 80, // Will be updated on play
            .max_height = 24,
            .preserve_aspect_ratio = TRUE,
            .dither = FALSE,
            .color_space = CHAFA_COLOR_SPACE_RGB,
            .work_factor = work_factor,
            .force_text = force_text,
            .force_sixel = force_sixel,
            .force_kitty = force_kitty,
            .force_iterm2 = force_iterm2,
            .text_symbol_mode = text_symbol_mode,
            .gamma = gamma,
            .color_enhance = COLOR_ENHANCE_OFF,
            .dither_mode = CHAFA_DITHER_MODE_NONE,
            .color_extractor = CHAFA_COLOR_EXTRACTOR_AVERAGE,
            .optimizations = CHAFA_OPTIMIZATION_REUSE_ATTRIBUTES
        };
        if (renderer_initialize(player->renderer, &config) == ERROR_NONE) {
            player->owns_renderer = TRUE;
        } else {
            renderer_destroy(player->renderer);
            player->renderer = NULL;
        }
    }

    return player;
}

// Set the renderer (optional; allows sharing/overriding)
void gif_player_set_renderer(GifPlayer *player, ImageRenderer *renderer) {
    if (player) {
        // If we had an internal renderer and we're replacing it, we should arguably destroy the old one
        // IF we knew we owned it. For simplicity, let's assume this is called right after new() if at all.
        if (player->renderer && player->renderer != renderer && player->owns_renderer) {
            renderer_destroy(player->renderer);
        }
        player->renderer = renderer;
        player->owns_renderer = FALSE;
    }
}

// Set the render area to avoid overwriting UI
void gif_player_set_render_area(GifPlayer *player,
                                gint term_width,
                                gint term_height,
                                gint area_top_row,
                                gint area_height,
                                gint max_width,
                                gint max_height) {
    if (!player) {
        return;
    }

    gboolean layout_changed = (player->render_term_width != term_width ||
                               player->render_term_height != term_height ||
                               player->render_area_top_row != area_top_row ||
                               player->render_area_height != area_height ||
                               player->render_max_width != max_width ||
                               player->render_max_height != max_height);

    player->render_term_width = term_width;
    player->render_term_height = term_height;
    player->render_area_top_row = area_top_row;
    player->render_area_height = area_height;
    player->render_max_width = max_width;
    player->render_max_height = max_height;
    player->render_layout_valid = (area_top_row > 0 && area_height > 0 && max_width > 0 && max_height > 0);
    if (layout_changed) {
        player->fixed_frame_valid = FALSE;
        player->last_frame_top_row = 0;
        player->last_frame_height = 0;
        gif_player_clear_line_cache(player);
    }
}

// Destroy GIF player
void gif_player_destroy(GifPlayer *player) {
    if (!player) {
        return;
    }

    gif_player_stop(player);
    
    g_free(player->filepath);
    if (player->canvas) {
        chafa_canvas_unref(player->canvas);
    }
    
    if (player->iter) {
        g_object_unref(player->iter);
    }
    
    if (player->animation) {
        g_object_unref(player->animation);
    }
    
    if (player->renderer && player->owns_renderer) {
        renderer_destroy(player->renderer);
    }

    gif_player_clear_line_cache(player);

    g_free(player);
}

// Load a GIF file and analyze animation info
ErrorCode gif_player_load(GifPlayer *player, const gchar *filepath) {
    if (!player || !filepath) {
        return ERROR_INVALID_IMAGE;
    }
    
    // Stop any previous playback
    gif_player_stop(player);
    
    // Clean up previous resources
    if (player->iter) {
        g_object_unref(player->iter);
        player->iter = NULL;
    }
    if (player->animation) {
        g_object_unref(player->animation);
        player->animation = NULL;
    }
    g_free(player->filepath);
    player->filepath = NULL;

    // Check that the file exists
    if (!file_exists(filepath)) {
        return ERROR_FILE_NOT_FOUND;
    }
    
    // Load animation with GdkPixbuf
    GError *error = NULL;
    GFile *file = g_file_new_for_path(filepath);
    GFileInputStream *stream = g_file_read(file, NULL, &error);
    g_object_unref(file);

    if (!stream) {
        if (error) g_error_free(error);
        return ERROR_FILE_NOT_FOUND;
    }

    player->animation = gdk_pixbuf_animation_new_from_stream(G_INPUT_STREAM(stream), NULL, &error);
    g_object_unref(stream);

    if (error) {
        g_error_free(error);
        return ERROR_INVALID_IMAGE;
    }
    
    player->is_animated = !gdk_pixbuf_animation_is_static_image(player->animation);
    player->filepath = g_strdup(filepath);
    player->fixed_frame_valid = FALSE;
    player->last_frame_top_row = 0;
    player->last_frame_height = 0;
    gif_player_clear_line_cache(player);
    
    // Initialize iterator
    player->iter = gdk_pixbuf_animation_get_iter(player->animation, NULL);
    
    return ERROR_NONE;
}

// Helper to render the current frame
static void render_current_frame_internal(GifPlayer *player) {
    if (!player || !player->iter || !player->renderer) {
        return;
    }

    // Update terminal size for renderer to ensure correct scaling
    renderer_update_terminal_size(player->renderer);
    if (player->render_layout_valid) {
        player->renderer->config.max_width = player->render_max_width;
        player->renderer->config.max_height = player->render_max_height;
    }

    GdkPixbuf *frame = gdk_pixbuf_animation_iter_get_pixbuf(player->iter);
    if (!frame) return;

    // Get image properties
    gint width = gdk_pixbuf_get_width(frame);
    gint height = gdk_pixbuf_get_height(frame);
    gint rowstride = gdk_pixbuf_get_rowstride(frame);
    gint n_channels = gdk_pixbuf_get_n_channels(frame);
    guchar *pixels = gdk_pixbuf_get_pixels(frame);

    // Render image data with the shared renderer
    GString *result = renderer_render_image_data(player->renderer, pixels, width, height, rowstride, n_channels);

    if (result) {
        gint rendered_w = 0;
        gint rendered_h = 0;
        renderer_get_rendered_dimensions(player->renderer, &rendered_w, &rendered_h);
        gif_player_present_rendered_frame(player,
                                          result,
                                          rendered_w,
                                          rendered_h,
                                          renderer_is_graphics_mode(player->renderer));
        g_string_free(result, TRUE);
    }
}

void gif_player_present_rendered_frame_for_test(GifPlayer *player,
                                                const GString *rendered,
                                                gint rendered_width,
                                                gint rendered_height,
                                                gboolean graphics_mode) {
    gif_player_present_rendered_frame(player,
                                      rendered,
                                      rendered_width,
                                      rendered_height,
                                      graphics_mode);
}

// Timer callback
static gboolean render_next_frame(gpointer user_data) {
    GifPlayer *player = (GifPlayer *)user_data;
    
    if (!player) {
        return G_SOURCE_REMOVE;
    }

    if (!player->is_playing || !player->iter) {
        player->timer_id = 0;
        return G_SOURCE_REMOVE;
    }
    
    // Advance iterator to current time
    gdk_pixbuf_animation_iter_advance(player->iter, NULL);
    
    // Render the new frame
    render_current_frame_internal(player);
    
    // Get delay for next frame
    int delay = gdk_pixbuf_animation_iter_get_delay_time(player->iter);
    if (delay < 10) delay = 10; // Minimum delay guard
    
    // Reschedule timer
    player->timer_id = g_timeout_add(delay, render_next_frame, player);
    
    // Return G_SOURCE_REMOVE because we rescheduled the timer
    return G_SOURCE_REMOVE;
}

// Play GIF
ErrorCode gif_player_play(GifPlayer *player) {
    if (!player || !player->is_animated || !player->animation) {
        return ERROR_INVALID_IMAGE;
    }
    
    if (player->is_playing) {
        return ERROR_NONE;
    }
    
    // Ensure iterator exists
    if (!player->iter) {
        player->iter = gdk_pixbuf_animation_get_iter(player->animation, NULL);
    }
    
    player->is_playing = TRUE;
    player->fixed_frame_valid = FALSE;
    player->last_frame_top_row = 0;
    player->last_frame_height = 0;
    gif_player_clear_line_cache(player);
    
    // Render the first frame immediately
    render_current_frame_internal(player);
    
    // Schedule the next frame
    int delay = gdk_pixbuf_animation_iter_get_delay_time(player->iter);
    if (delay < 10) delay = 10;
    
    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
    }
    player->timer_id = g_timeout_add(delay, render_next_frame, player);
    
    return ERROR_NONE;
}

// Pause playback
ErrorCode gif_player_pause(GifPlayer *player) {
    if (!player) {
        return ERROR_INVALID_IMAGE;
    }
    
    player->is_playing = FALSE;
    
    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
        player->timer_id = 0;
    }
    
    return ERROR_NONE;
}

// Stop playback
ErrorCode gif_player_stop(GifPlayer *player) {
    if (!player) {
        return ERROR_INVALID_IMAGE;
    }
    
    player->is_playing = FALSE;
    
    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
        player->timer_id = 0;
    }

    gif_player_clear_line_cache(player);
    
    // Reset iterator if needed to return to start
    // Next play or load will handle iterator reset
    
    return ERROR_NONE;
}

// Check if playing
gboolean gif_player_is_playing(const GifPlayer *player) {
    return player && player->is_playing;
}

// Check if animated GIF
gboolean gif_player_is_animated(const GifPlayer *player) {
    return player && player->is_animated;
}

// Update terminal size for the internal renderer
ErrorCode gif_player_update_terminal_size(GifPlayer *player) {
    if (!player || !player->renderer) {
        return ERROR_INVALID_IMAGE;
    }
    return renderer_update_terminal_size(player->renderer);
}
