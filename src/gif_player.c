#define _GNU_SOURCE

#include "gif_player.h"
#include "common.h"
#include "kitty_graphics.h"
#include "terminal_output.h"
#include "process_env.h"
#include "gif_player_test_internal.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <chafa.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <gio/gio.h>

// Suppress deprecation warnings for GdkPixbufAnimation and GTimeVal
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static gboolean render_next_frame(gpointer user_data);
static ErrorCode render_current_frame_internal(GifPlayer *player);
static GifPlayerAdvanceHook gif_player_advance_hook = NULL;
#define KITTY_ANIMATION_MAX_UNCHANGED_TICKS 8

void gif_player_set_advance_hook_for_test(GifPlayerAdvanceHook hook) {
    gif_player_advance_hook = hook;
}

static gboolean gif_player_advance_iter(GdkPixbufAnimationIter *iter,
                                        const GTimeVal *current_time) {
    if (gif_player_advance_hook) {
        return gif_player_advance_hook(iter, current_time);
    }
    return gdk_pixbuf_animation_iter_advance(iter, current_time);
}

static void gif_player_clear_line_cache(GifPlayer *player) {
    if (!player || !player->last_frame_lines) {
        return;
    }

    g_ptr_array_free(player->last_frame_lines, TRUE);
    player->last_frame_lines = NULL;
}

static gboolean gif_player_write_kitty_command(GString *command) {
    if (!command) {
        return FALSE;
    }
    if (!terminal_output_ready(g_get_monotonic_time())) {
        g_string_free(command, TRUE);
        return FALSE;
    }
    gboolean success = terminal_output_write(command->str, command->len);
    g_string_free(command, TRUE);
    return success;
}

static void gif_player_release_kitty_shm_name(gpointer data) {
    gchar *shm_name = data;
    kitty_graphics_shm_unlink(shm_name);
    g_free(shm_name);
}

static gboolean gif_player_reset_kitty_animation(GifPlayer *player, gboolean delete_image) {
    if (!player) {
        return TRUE;
    }
    gboolean success = TRUE;
    terminal_output_take_chunk_error();
    if (player->kitty_animation_upload) {
        g_string_free(player->kitty_animation_upload, TRUE);
        player->kitty_animation_upload = NULL;
        player->kitty_animation_upload_offset = 0;
    }
    if (delete_image && player->kitty_animation_prepared && player->kitty_animation_id != 0) {
        /* Not yet sent: transfer ownership behind framing recovery, do not drop it. */
        kitty_graphics_defer_image_delete(player->kitty_animation_id);
        success = terminal_output_ready(g_get_monotonic_time());
    }
    player->kitty_animation_id = 0;
    player->kitty_animation_prepared = FALSE;
    player->kitty_animation_complete = FALSE;
    player->kitty_animation_frame_count = 0;
    player->kitty_animation_elapsed_ms = 0;
    player->kitty_animation_current_delay_ms = 0;
    player->kitty_animation_source_width = 0;
    player->kitty_animation_source_height = 0;
    player->kitty_animation_display_width = 0;
    player->kitty_animation_display_height = 0;
    player->kitty_animation_unchanged_ticks = 0;
    if (player->kitty_animation_shm_names) {
        g_ptr_array_set_size(player->kitty_animation_shm_names, 0);
    }
    return success;
}

static ErrorCode gif_player_output_failure(GifPlayer *player) {
    player->is_playing = FALSE;
    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
        player->timer_id = 0;
    }
    gif_player_reset_kitty_animation(player, TRUE);
    fprintf(stderr, "GIF playback failed: %s\n", error_code_to_string(ERROR_OUTPUT_IO));
    return ERROR_OUTPUT_IO;
}

static gboolean gif_player_skip_gif_sub_blocks(const guint8 *data,
                                               gsize length,
                                               gsize *offset) {
    while (*offset < length) {
        guint8 block_size = data[(*offset)++];
        if (block_size == 0) {
            return TRUE;
        }
        if ((gsize)block_size > length - *offset) {
            return FALSE;
        }
        *offset += block_size;
    }
    return FALSE;
}

static guint gif_player_count_gif_frames(const guint8 *data, gsize length) {
    if (!data || length < 13 ||
        (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0)) {
        return 0;
    }

    gsize offset = 13;
    guint8 screen_flags = data[10];
    if ((screen_flags & 0x80) != 0) {
        gsize color_table_size = (gsize)3 << ((screen_flags & 0x07) + 1);
        if (color_table_size > length - offset) {
            return 0;
        }
        offset += color_table_size;
    }

    guint frame_count = 0;
    while (offset < length) {
        guint8 marker = data[offset++];
        if (marker == 0x3B) {
            return frame_count;
        }
        if (marker == 0x21) {
            if (offset >= length) {
                return 0;
            }
            offset++;
            if (!gif_player_skip_gif_sub_blocks(data, length, &offset)) {
                return 0;
            }
            continue;
        }
        if (marker != 0x2C || length - offset < 9) {
            return 0;
        }

        guint8 image_flags = data[offset + 8];
        offset += 9;
        if ((image_flags & 0x80) != 0) {
            gsize color_table_size = (gsize)3 << ((image_flags & 0x07) + 1);
            if (color_table_size > length - offset) {
                return 0;
            }
            offset += color_table_size;
        }
        if (offset >= length) {
            return 0;
        }
        offset++;
        if (!gif_player_skip_gif_sub_blocks(data, length, &offset)) {
            return 0;
        }
        frame_count++;
    }
    return 0;
}

static guint32 gif_player_read_le32(const guint8 *data) {
    return (guint32)data[0] |
           ((guint32)data[1] << 8) |
           ((guint32)data[2] << 16) |
           ((guint32)data[3] << 24);
}

static guint gif_player_count_webp_frames(const guint8 *data, gsize length) {
    if (!data || length < 20 || memcmp(data, "RIFF", 4) != 0 ||
        memcmp(data + 8, "WEBP", 4) != 0) {
        return 0;
    }

    guint64 riff_end = 8U + (guint64)gif_player_read_le32(data + 4);
    if (riff_end > length || riff_end < 12U) {
        return 0;
    }

    gsize offset = 12;
    gboolean has_animation = FALSE;
    guint frame_count = 0;
    while ((guint64)offset + 8U <= riff_end) {
        const guint8 *chunk = data + offset;
        guint32 chunk_size = gif_player_read_le32(chunk + 4);
        guint64 padded_size = (guint64)chunk_size + (guint64)(chunk_size & 1U);
        if (padded_size > riff_end - offset - 8U) {
            return 0;
        }

        if (memcmp(chunk, "ANIM", 4) == 0) {
            if (chunk_size < 6) {
                return 0;
            }
            has_animation = TRUE;
        } else if (memcmp(chunk, "ANMF", 4) == 0) {
            if (chunk_size < 16) {
                return 0;
            }
            frame_count++;
        }
        offset += 8U + (gsize)padded_size;
    }

    if ((guint64)offset != riff_end) {
        return 0;
    }
    return has_animation ? frame_count : 0;
}

guint gif_player_count_gif_frames_for_test(const guint8 *data, gsize length) {
    return gif_player_count_gif_frames(data, length);
}

guint gif_player_count_webp_frames_for_test(const guint8 *data, gsize length) {
    return gif_player_count_webp_frames(data, length);
}

static gboolean gif_player_present_rendered_frame(GifPlayer *player,
                                              const GString *result,
                                              gint rendered_w,
                                              gint rendered_h,
                                              gboolean graphics_mode) {
    if (!player || !result) {
        return FALSE;
    }
    if (!terminal_output_ready(g_get_monotonic_time())) return FALSE;

    GString *output = g_string_new(NULL);

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
            g_string_append_printf(output, "\033[%d;%dH", row, col);
            if (result->len > 0) {
                g_string_append_len(output, result->str, result->len);
            }
            lines_printed = rendered_h > 0 ? rendered_h : 1;
        } else if (!has_newline) {
            gif_player_clear_line_cache(player);
            g_string_append_printf(output, "\033[%d;1H", row);
            if (left_pad > 0) {
                gchar *pad_buffer = g_malloc(left_pad);
                memset(pad_buffer, ' ', left_pad);
                g_string_append_len(output, pad_buffer, left_pad);
                g_free(pad_buffer);
            }
            if (result->len > 0) {
                g_string_append_len(output, result->str, result->len);
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
                    g_string_append_printf(output, "\033[%d;1H\033[2K", row);
                    if (full_len > 0) {
                        g_string_append_len(output, full_line, full_len);
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
                    g_string_append_printf(output, "\033[%d;1H\033[2K", r);
                }
            }
        }

        player->last_frame_top_row = image_top_row;
        player->last_frame_height = lines_printed > 0 ? lines_printed : 0;
    } else {
        gif_player_clear_line_cache(player);
        g_string_append_printf(output, "\033[H");
        if (result->len > 0) {
            g_string_append_len(output, result->str, result->len);
        }
        g_string_append_printf(output, "\033[J");
        player->last_frame_top_row = 0;
        player->last_frame_height = 0;
    }

    if (graphics_mode && result->len > 4096 &&
        (pixelterm_getenv("SSH_CONNECTION") || pixelterm_getenv("SSH_CLIENT")) &&
        player->kitty_animation_prepared) {
        player->kitty_animation_upload = output;
        player->kitty_animation_upload_offset = 0;
        return TRUE;
    }
    gboolean success = terminal_output_write(output->str, output->len);
    g_string_free(output, TRUE);
    if (!success) gif_player_clear_line_cache(player);
    return success;
}

static gboolean gif_player_kitty_uses_direct_transfer(void) {
    return pixelterm_getenv("SSH_CONNECTION") || pixelterm_getenv("SSH_CLIENT");
}

static gboolean gif_player_is_direct_kitty(const GifPlayer *player) {
    if (!player || !player->renderer || !player->renderer->config.force_kitty ||
        player->kitty_animation_disabled ||
        player->renderer->config.gamma != 1.0 ||
        player->renderer->config.color_enhance != COLOR_ENHANCE_OFF ||
        player->total_frames < 2 || player->total_frames > 512 ||
        (!kitty_graphics_shm_auto_enabled() && !gif_player_kitty_uses_direct_transfer()) ||
        pixelterm_getenv("TMUX") || pixelterm_getenv("STY")) {
        return FALSE;
    }

    const gchar *term = pixelterm_getenv("TERM");
    const gchar *term_program = pixelterm_getenv("TERM_PROGRAM");
    return pixelterm_getenv("KITTY_WINDOW_ID") != NULL ||
           (term && g_strcmp0(term, "xterm-kitty") == 0) ||
           (term_program && g_ascii_strcasecmp(term_program, "kitty") == 0);
}

static GdkPixbuf *gif_player_ensure_rgba(GdkPixbuf *pixbuf) {
    if (!pixbuf) {
        return NULL;
    }
    if (gdk_pixbuf_get_has_alpha(pixbuf) && gdk_pixbuf_get_n_channels(pixbuf) == 4) {
        return g_object_ref(pixbuf);
    }
    return gdk_pixbuf_add_alpha(pixbuf, FALSE, 0, 0, 0);
}

static gboolean gif_player_drain_upload(GifPlayer *player) {
    GString *upload = player->kitty_animation_upload;
    if (!upload) return TRUE;
    gsize offset = player->kitty_animation_upload_offset;
    gsize batch_start = offset;
    gint64 deadline = g_get_monotonic_time() + 2000;
    while (offset < upload->len && offset - batch_start < 64 * 1024 &&
           g_get_monotonic_time() < deadline) {
        const gchar *end = g_strstr_len(upload->str + offset, upload->len - offset, "\033\\");
        if (!end) return FALSE;
        gsize length = (gsize)(end - (upload->str + offset)) + 2;
        if (!terminal_output_write_chunk(upload->str + offset, length)) return TRUE;
        offset += length;
        player->kitty_animation_upload_offset = offset;
    }
    if (offset < upload->len || !terminal_output_ready(g_get_monotonic_time())) return TRUE;
    g_string_free(upload, TRUE);
    player->kitty_animation_upload = NULL;
    player->kitty_animation_upload_offset = 0;
    if (player->kitty_animation_frame_count >= (guint)player->total_frames) {
        if (!gif_player_write_kitty_command(
                kitty_graphics_build_animation_play_command(player->kitty_animation_id))) return FALSE;
        player->kitty_animation_complete = TRUE;
    }
    return TRUE;
}

static gboolean gif_player_submit_kitty_frame(GifPlayer *player,
                                              KittyGraphicsFrame *frame,
                                              gboolean display_root) {
    if (!player || !frame || !frame->command) {
        kitty_graphics_frame_free(frame);
        return FALSE;
    }
    if (!terminal_output_ready(g_get_monotonic_time())) {
        kitty_graphics_frame_free(frame);
        return FALSE;
    }

    gboolean success = FALSE;
    if (gif_player_kitty_uses_direct_transfer() && frame->command->len > 4096) {
        if (display_root) {
            success = gif_player_present_rendered_frame(player, frame->command,
                frame->display_width_cells, frame->display_height_cells, TRUE);
        } else {
            success = TRUE;
            player->kitty_animation_upload = frame->command;
            player->kitty_animation_upload_offset = 0;
            frame->command = NULL;
        }
        kitty_graphics_frame_free(frame);
        return success;
    }
    if (display_root) {
        success = gif_player_present_rendered_frame(player,
                                          frame->command,
                                          frame->display_width_cells,
                                          frame->display_height_cells,
                                          TRUE);
    } else {
        success = terminal_output_write(frame->command->str, frame->command->len);
    }

    if (success && frame->shm_name) {
        g_ptr_array_add(player->kitty_animation_shm_names, frame->shm_name);
        frame->shm_name = NULL;
    }
    kitty_graphics_frame_free(frame);
    return success;
}

gboolean gif_player_submit_kitty_frame_for_test(GifPlayer *player, KittyGraphicsFrame *frame, gboolean root) {
    return gif_player_submit_kitty_frame(player, frame, root);
}

static gboolean gif_player_native_frame_fits_budget(const GifPlayer *player,
                                                    GdkPixbuf *pixbuf) {
    gsize frame_bytes = 0;
    gsize animation_bytes = 0;
    return player && pixbuf &&
           g_size_checked_mul(&frame_bytes,
                              (gsize)(gif_player_kitty_uses_direct_transfer() ? player->kitty_animation_transfer_width : gdk_pixbuf_get_width(pixbuf)),
                              (gsize)(gif_player_kitty_uses_direct_transfer() ? player->kitty_animation_transfer_height : gdk_pixbuf_get_height(pixbuf))) &&
           g_size_checked_mul(&frame_bytes, frame_bytes, (gsize)4) &&
           (!gif_player_kitty_uses_direct_transfer() || frame_bytes <= KITTY_GRAPHICS_DIRECT_MAX_BYTES) &&
           g_size_checked_mul(&animation_bytes, frame_bytes, (gsize)player->total_frames) &&
           animation_bytes <= (gsize)256 * 1024 * 1024;
}

static gint gif_player_iter_delay_ms(GdkPixbufAnimationIter *iter) {
    gint delay = gdk_pixbuf_animation_iter_get_delay_time(iter);
    return delay < 10 ? 10 : delay;
}

static gint gif_player_current_delay_ms(GifPlayer *player) {
    return gif_player_iter_delay_ms(player->iter);
}

static gboolean gif_player_native_geometry_matches(const GifPlayer *player,
                                                   gint width,
                                                   gint height) {
    return player && width > 0 && height > 0 &&
           width == player->kitty_animation_source_width &&
           height == player->kitty_animation_source_height;
}

gboolean gif_player_native_geometry_matches_for_test(const GifPlayer *player,
                                                     gint width,
                                                     gint height) {
    return gif_player_native_geometry_matches(player, width, height);
}

static guint gif_player_next_tick_ms(GifPlayer *player) {
    /* Upload bounded batches, not one frame per animation interval. */
    if (player->kitty_animation_prepared && !player->kitty_animation_complete &&
        gif_player_kitty_uses_direct_transfer()) return 1;
    return gif_player_current_delay_ms(player);
}

static GdkPixbuf *gif_player_transfer_rgba(GifPlayer *player, GdkPixbuf *pixbuf) {
    GdkPixbuf *rgba = gif_player_ensure_rgba(pixbuf);
    if (rgba && gif_player_kitty_uses_direct_transfer() &&
        player->kitty_animation_transfer_width > 0 && player->kitty_animation_transfer_height > 0 &&
        (gdk_pixbuf_get_width(rgba) != player->kitty_animation_transfer_width ||
         gdk_pixbuf_get_height(rgba) != player->kitty_animation_transfer_height)) {
        GdkPixbuf *scaled = gdk_pixbuf_scale_simple(rgba,
            player->kitty_animation_transfer_width, player->kitty_animation_transfer_height,
            GDK_INTERP_BILINEAR);
        g_object_unref(rgba);
        rgba = scaled;
    }
    return rgba;
}

static gboolean gif_player_prepare_kitty_animation(GifPlayer *player, ErrorCode *output_error) {
    if (!terminal_output_ready(g_get_monotonic_time())) {
        *output_error = ERROR_OUTPUT_IO;
        return FALSE;
    }
    if (!gif_player_is_direct_kitty(player) || !player->iter) {
        return FALSE;
    }

    GdkPixbufAnimationIter *native_iter = NULL;
    if (player->resume_from_current_frame && player->iter) {
        native_iter = g_object_ref(player->iter);
    } else {
        GTimeVal start_time = {0, 0};
        native_iter = gdk_pixbuf_animation_get_iter(player->animation, &start_time);
    }
    if (!native_iter) {
        player->kitty_animation_disabled = TRUE;
        return FALSE;
    }

    GdkPixbuf *pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(native_iter);
    if (renderer_update_terminal_size(player->renderer) != ERROR_NONE) {
        player->kitty_animation_disabled = TRUE;
        g_object_unref(native_iter);
        return FALSE;
    }
    if (player->render_layout_valid) {
        player->renderer->config.max_width = player->render_max_width;
        player->renderer->config.max_height = player->render_max_height;
    }
    if (renderer_setup_canvas(player->renderer,
                              gdk_pixbuf_get_width(pixbuf),
                              gdk_pixbuf_get_height(pixbuf)) != ERROR_NONE) {
        player->kitty_animation_disabled = TRUE;
        g_object_unref(native_iter);
        return FALSE;
    }

    gint rendered_w = 0;
    gint rendered_h = 0;
    renderer_get_rendered_dimensions(player->renderer, &rendered_w, &rendered_h);
    gint cell_width = 0, cell_height = 0;
    get_terminal_cell_geometry(&cell_width, &cell_height);
    gint width = gdk_pixbuf_get_width(pixbuf), height = gdk_pixbuf_get_height(pixbuf);
    gdouble scale = MIN(1.0, MIN((gdouble)rendered_w * cell_width / width,
                               (gdouble)rendered_h * cell_height / height));
    player->kitty_animation_transfer_width = MAX(1, (gint)(width * scale));
    player->kitty_animation_transfer_height = MAX(1, (gint)(height * scale));
    if (!gif_player_native_frame_fits_budget(player, pixbuf)) {
        player->kitty_animation_disabled = TRUE;
        g_object_unref(native_iter);
        return FALSE;
    }
    GdkPixbuf *rgba = gif_player_transfer_rgba(player, pixbuf);
    if (!rgba) {
        player->kitty_animation_disabled = TRUE;
        g_object_unref(native_iter);
        return FALSE;
    }

    guint32 image_id = g_random_int();
    if (image_id == 0) {
        image_id = 1;
    }
    KittyGraphicsFrame *root = gif_player_kitty_uses_direct_transfer() ?
        kitty_graphics_animation_new_direct_rgba(
            gdk_pixbuf_get_pixels(rgba),
            gdk_pixbuf_get_width(rgba),
            gdk_pixbuf_get_height(rgba),
            gdk_pixbuf_get_rowstride(rgba),
            rendered_w, rendered_h, image_id, 0) :
        kitty_graphics_animation_root_new_shm_rgba(
        gdk_pixbuf_get_pixels(rgba),
        gdk_pixbuf_get_width(rgba),
        gdk_pixbuf_get_height(rgba),
        gdk_pixbuf_get_rowstride(rgba),
        rendered_w,
        rendered_h,
        image_id);
    g_object_unref(rgba);
    if (!root) {
        player->kitty_animation_disabled = TRUE;
        g_object_unref(native_iter);
        return FALSE;
    }
    /* Own the image before submission: a failed write may still deliver it. */
    player->kitty_animation_id = image_id;
    player->kitty_animation_prepared = TRUE;
    if (!gif_player_submit_kitty_frame(player, root, TRUE)) {
        *output_error = ERROR_OUTPUT_IO;
        player->kitty_animation_disabled = TRUE;
        g_object_unref(native_iter);
        return FALSE;
    }

    player->kitty_animation_complete = FALSE;
    player->kitty_animation_frame_count = 1;
    player->kitty_animation_source_width = gdk_pixbuf_get_width(pixbuf);
    player->kitty_animation_source_height = gdk_pixbuf_get_height(pixbuf);
    player->kitty_animation_display_width = rendered_w;
    player->kitty_animation_display_height = rendered_h;
    player->kitty_animation_unchanged_ticks = 0;

    gint delay = gif_player_iter_delay_ms(native_iter);
    if (player->kitty_animation_upload) {
        GString *delay_command = kitty_graphics_build_animation_frame_delay_command(image_id, 1, delay);
        GString *stop_command = kitty_graphics_build_animation_stop_command(image_id);
        g_string_append_len(player->kitty_animation_upload, delay_command->str, delay_command->len);
        g_string_append_len(player->kitty_animation_upload, stop_command->str, stop_command->len);
        g_string_free(delay_command, TRUE);
        g_string_free(stop_command, TRUE);
    } else if (!gif_player_write_kitty_command(
            kitty_graphics_build_animation_frame_delay_command(image_id, 1, delay)) ||
        !gif_player_write_kitty_command(
            (gif_player_kitty_uses_direct_transfer() ?
                kitty_graphics_build_animation_stop_command(image_id) :
                kitty_graphics_build_animation_loading_command(image_id)))) {
        *output_error = ERROR_OUTPUT_IO;
        gif_player_reset_kitty_animation(player, TRUE);
        player->kitty_animation_disabled = TRUE;
        g_object_unref(native_iter);
        return FALSE;
    }

    g_object_unref(player->iter);
    player->iter = native_iter;
    if (!player->resume_from_current_frame) {
        player->kitty_animation_elapsed_ms = 0;
    }
    player->kitty_animation_current_delay_ms = delay;
    player->resume_from_current_frame = FALSE;
    return TRUE;
}

static gboolean gif_player_append_kitty_animation_frame(GifPlayer *player, ErrorCode *output_error) {
    if (!player || !player->kitty_animation_prepared || !player->iter) {
        return FALSE;
    }

    GdkPixbuf *pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(player->iter);
    if (!pixbuf) {
        return FALSE;
    }
    if (!gif_player_native_geometry_matches(player,
                                            gdk_pixbuf_get_width(pixbuf),
                                            gdk_pixbuf_get_height(pixbuf))) {
        return FALSE;
    }
    GdkPixbuf *rgba = gif_player_transfer_rgba(player, pixbuf);
    if (!rgba) {
        return FALSE;
    }
    KittyGraphicsFrame *frame = gif_player_kitty_uses_direct_transfer() ?
        kitty_graphics_animation_new_direct_rgba(
            gdk_pixbuf_get_pixels(rgba),
            gdk_pixbuf_get_width(rgba),
            gdk_pixbuf_get_height(rgba),
            gdk_pixbuf_get_rowstride(rgba),
            player->kitty_animation_display_width, player->kitty_animation_display_height,
            player->kitty_animation_id, gif_player_current_delay_ms(player)) :
        kitty_graphics_animation_frame_new_shm_rgba(
        gdk_pixbuf_get_pixels(rgba),
        gdk_pixbuf_get_width(rgba),
        gdk_pixbuf_get_height(rgba),
        gdk_pixbuf_get_rowstride(rgba),
        player->kitty_animation_display_width,
        player->kitty_animation_display_height,
        player->kitty_animation_id,
        gif_player_current_delay_ms(player));
    g_object_unref(rgba);
    if (!frame) return FALSE;
    if (!gif_player_submit_kitty_frame(player, frame, FALSE)) {
        *output_error = ERROR_OUTPUT_IO;
        return FALSE;
    }

    player->kitty_animation_frame_count++;
    player->kitty_animation_current_delay_ms = gif_player_current_delay_ms(player);
    if (!player->kitty_animation_upload &&
        player->kitty_animation_frame_count >= (guint)player->total_frames) {
        if (!gif_player_write_kitty_command(
                kitty_graphics_build_animation_play_command(player->kitty_animation_id))) {
            *output_error = ERROR_OUTPUT_IO;
            return FALSE;
        }
        player->kitty_animation_complete = TRUE;
    }
    return TRUE;
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
    player->resume_from_current_frame = FALSE;
    player->kitty_animation_id = 0;
    player->kitty_animation_prepared = FALSE;
    player->kitty_animation_complete = FALSE;
    player->kitty_animation_disabled = FALSE;
    player->kitty_animation_frame_count = 0;
    player->kitty_animation_elapsed_ms = 0;
    player->kitty_animation_current_delay_ms = 0;
    player->kitty_animation_source_width = 0;
    player->kitty_animation_source_height = 0;
    player->kitty_animation_display_width = 0;
    player->kitty_animation_display_height = 0;
    player->kitty_animation_unchanged_ticks = 0;
    player->kitty_animation_shm_names = g_ptr_array_new_with_free_func(
        gif_player_release_kitty_shm_name);
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
ErrorCode gif_player_set_render_area(GifPlayer *player,
                                gint term_width,
                                gint term_height,
                                gint area_top_row,
                                gint area_height,
                                gint max_width,
                                gint max_height) {
    if (!player) {
        return ERROR_NONE;
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
        gboolean restart_native = player->kitty_animation_prepared && player->is_playing;
        gboolean preserve_native_timeline = player->resume_from_current_frame;
        gint64 animation_elapsed_ms = player->kitty_animation_elapsed_ms;
        gint animation_delay_ms = player->kitty_animation_current_delay_ms;
        gboolean cleanup_ok = gif_player_reset_kitty_animation(player, TRUE);
        if (preserve_native_timeline) {
            player->kitty_animation_elapsed_ms = animation_elapsed_ms;
            player->kitty_animation_current_delay_ms = animation_delay_ms;
        }
        player->fixed_frame_valid = FALSE;
        player->last_frame_top_row = 0;
        player->last_frame_height = 0;
        gif_player_clear_line_cache(player);
        if (!cleanup_ok) {
            return gif_player_output_failure(player);
        }
        if (restart_native) {
            ErrorCode output_error = ERROR_NONE;
            if (!gif_player_prepare_kitty_animation(player, &output_error) &&
                output_error == ERROR_NONE) {
                output_error = render_current_frame_internal(player);
            }
            if (output_error != ERROR_NONE) {
                gif_player_output_failure(player);
                return output_error;
            }
            if (player->timer_id != 0) {
                g_source_remove(player->timer_id);
            }
            player->timer_id = g_timeout_add(gif_player_next_tick_ms(player),
                                             render_next_frame,
                                             player);
        }
    }
    return ERROR_NONE;
}

// Destroy GIF player
void gif_player_destroy(GifPlayer *player) {
    if (!player) {
        return;
    }

    if (gif_player_stop(player) != ERROR_NONE) {
        gif_player_output_failure(player);
    }
    
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

    g_ptr_array_free(player->kitty_animation_shm_names, TRUE);

    g_free(player);
}

// Load a GIF file and analyze animation info
ErrorCode gif_player_load(GifPlayer *player, const gchar *filepath) {
    if (!player || !filepath) {
        return ERROR_INVALID_IMAGE;
    }
    
    // Stop any previous playback
    ErrorCode stop_result = gif_player_stop(player);
    if (stop_result != ERROR_NONE) return stop_result;
    
    // Clean up previous resources
    if (player->iter) {
        g_object_unref(player->iter);
        player->iter = NULL;
    }
    player->resume_from_current_frame = FALSE;
    if (player->animation) {
        g_object_unref(player->animation);
        player->animation = NULL;
    }
    g_free(player->filepath);
    player->filepath = NULL;
    player->total_frames = 0;

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
    gchar *contents = NULL;
    gsize contents_length = 0;
    GStatBuf file_stat;
    if (g_stat(filepath, &file_stat) == 0 && file_stat.st_size >= 0 &&
        (guint64)file_stat.st_size <= (guint64)64 * 1024 * 1024 &&
        g_file_get_contents(filepath, &contents, &contents_length, NULL)) {
        guint frame_count = gif_player_count_gif_frames((const guint8 *)contents, contents_length);
        if (frame_count == 0) {
            frame_count = gif_player_count_webp_frames((const guint8 *)contents, contents_length);
        }
        if (frame_count <= G_MAXINT) {
            player->total_frames = (gint)frame_count;
        }
        g_free(contents);
    }
    player->fixed_frame_valid = FALSE;
    player->last_frame_top_row = 0;
    player->last_frame_height = 0;
    gif_player_clear_line_cache(player);
    
    // Initialize iterator
    player->iter = gdk_pixbuf_animation_get_iter(player->animation, NULL);
    
    return ERROR_NONE;
}

// Helper to render the current frame
static ErrorCode render_current_frame_internal(GifPlayer *player) {
    if (!player || !player->iter || !player->renderer) {
        return ERROR_NONE;
    }
    if (!terminal_output_ready(g_get_monotonic_time())) return ERROR_OUTPUT_IO;

    // Update terminal size for renderer to ensure correct scaling
    renderer_update_terminal_size(player->renderer);
    if (player->render_layout_valid) {
        player->renderer->config.max_width = player->render_max_width;
        player->renderer->config.max_height = player->render_max_height;
    }

    GdkPixbuf *frame = gdk_pixbuf_animation_iter_get_pixbuf(player->iter);
    if (!frame) return ERROR_NONE;

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
        gboolean submitted = gif_player_present_rendered_frame(player,
                                          result,
                                          rendered_w,
                                          rendered_h,
                                          renderer_is_graphics_mode(player->renderer));
        g_string_free(result, TRUE);
        if (!submitted) return ERROR_OUTPUT_IO;
    }
    return ERROR_NONE;
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
    if (!terminal_output_ready(g_get_monotonic_time())) return G_SOURCE_CONTINUE;
    ErrorCode output_error = ERROR_NONE;
    if (terminal_output_take_chunk_error()) goto output_failed;
    if (player->kitty_animation_upload) {
        if (!gif_player_drain_upload(player)) goto output_failed;
        if (player->kitty_animation_complete) { player->timer_id = 0; return G_SOURCE_REMOVE; }
        player->timer_id = g_timeout_add(1, render_next_frame, player);
        return G_SOURCE_REMOVE;
    }
    
    if (player->kitty_animation_prepared && !player->kitty_animation_complete) {
        player->kitty_animation_elapsed_ms += player->kitty_animation_current_delay_ms;
        GTimeVal frame_time = {
            .tv_sec = (glong)(player->kitty_animation_elapsed_ms / 1000),
            .tv_usec = (glong)((player->kitty_animation_elapsed_ms % 1000) * 1000 + 1)
        };
        if (frame_time.tv_usec >= 1000000) {
            frame_time.tv_sec++;
            frame_time.tv_usec -= 1000000;
        }
        gboolean advanced = gif_player_advance_iter(player->iter, &frame_time);
        if (!advanced) {
            player->kitty_animation_unchanged_ticks++;
        } else {
            player->kitty_animation_unchanged_ticks = 0;
        }
        if ((advanced && !gif_player_append_kitty_animation_frame(player, &output_error)) ||
            player->kitty_animation_unchanged_ticks > KITTY_ANIMATION_MAX_UNCHANGED_TICKS) {
            if (!gif_player_reset_kitty_animation(player, TRUE))
                output_error = ERROR_OUTPUT_IO;
            if (output_error != ERROR_NONE) goto output_failed;
            player->kitty_animation_disabled = TRUE;
            g_object_unref(player->iter);
            player->iter = gdk_pixbuf_animation_get_iter(player->animation, NULL);
            output_error = render_current_frame_internal(player);
        } else if (player->kitty_animation_complete) {
            player->timer_id = 0;
            return G_SOURCE_REMOVE;
        }
    } else if (!player->kitty_animation_prepared) {
        gdk_pixbuf_animation_iter_advance(player->iter, NULL);
        if (!gif_player_prepare_kitty_animation(player, &output_error) &&
            output_error == ERROR_NONE) {
            output_error = render_current_frame_internal(player);
        }
    }
    if (output_error != ERROR_NONE) goto output_failed;
    
    // Get delay for next frame
    int delay = gif_player_next_tick_ms(player);
    
    // Reschedule timer
    player->timer_id = g_timeout_add(delay, render_next_frame, player);
    
    // Return G_SOURCE_REMOVE because we rescheduled the timer
    return G_SOURCE_REMOVE;

output_failed:
    /* This source is removed by the callback return, not g_source_remove(). */
    player->timer_id = 0;
    gif_player_output_failure(player);
    return G_SOURCE_REMOVE;
}

gboolean gif_player_render_next_frame_for_test(GifPlayer *player) {
    return render_next_frame(player);
}

// Play GIF
ErrorCode gif_player_play(GifPlayer *player) {
    if (!player || !player->is_animated || !player->animation) {
        return ERROR_INVALID_IMAGE;
    }
    
    if (player->is_playing) {
        return ERROR_NONE;
    }
    if (!terminal_output_ready(g_get_monotonic_time()))
        return gif_player_output_failure(player);

    if (!player->kitty_animation_prepared) {
        player->kitty_animation_disabled = FALSE;
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

    if (player->kitty_animation_prepared && player->kitty_animation_complete) {
        if (gif_player_write_kitty_command(
                kitty_graphics_build_animation_play_command(player->kitty_animation_id))) {
            return ERROR_NONE;
        }
        return gif_player_output_failure(player);
    }

    if (player->kitty_animation_prepared && !player->kitty_animation_upload) {
        if (!gif_player_write_kitty_command(
                (gif_player_kitty_uses_direct_transfer() ?
                kitty_graphics_build_animation_stop_command(player->kitty_animation_id) :
                kitty_graphics_build_animation_loading_command(player->kitty_animation_id)))) {
            return gif_player_output_failure(player);
        }
    }

    ErrorCode output_error = ERROR_NONE;
    if (!player->kitty_animation_prepared &&
        !gif_player_prepare_kitty_animation(player, &output_error) &&
        output_error == ERROR_NONE) {
        output_error = render_current_frame_internal(player);
    }
    if (output_error != ERROR_NONE) return gif_player_output_failure(player);
    
    // Schedule the next frame
    int delay = gif_player_next_tick_ms(player);
    
    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
    }
    player->timer_id = g_timeout_add(delay, render_next_frame, player);
    
    return ERROR_NONE;
}

ErrorCode gif_player_refresh_paused_frame(GifPlayer *player) {
    if (!player || !player->iter || player->is_playing) return ERROR_INVALID_ARGS;
    return render_current_frame_internal(player);
}

ErrorCode gif_player_prepare_for_redraw(GifPlayer *player) {
    if (!player || !player->is_playing) {
        return ERROR_NONE;
    }

    gint64 animation_elapsed_ms = player->kitty_animation_elapsed_ms;
    gint animation_delay_ms = player->kitty_animation_current_delay_ms;
    gboolean had_native_animation = player->kitty_animation_prepared;

    player->is_playing = FALSE;
    if (player->timer_id != 0) {
        g_source_remove(player->timer_id);
        player->timer_id = 0;
    }
    gboolean cleanup_ok = gif_player_reset_kitty_animation(player, TRUE);
    if (had_native_animation) {
        player->kitty_animation_elapsed_ms = animation_elapsed_ms;
        player->kitty_animation_current_delay_ms = animation_delay_ms;
    }
    player->resume_from_current_frame = player->iter != NULL;
    gif_player_clear_line_cache(player);
    if (!cleanup_ok) return gif_player_output_failure(player);
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

    if (player->kitty_animation_prepared && !player->kitty_animation_upload) {
        GString *command = kitty_graphics_build_animation_stop_command(player->kitty_animation_id);
        if (!command) return ERROR_OUTPUT_IO;
        /* Locally paused even on failure; the idempotent native STOP must survive recovery. */
        terminal_output_defer(command->str, TERMINAL_OUTPUT_RESTORE);
        g_string_free(command, TRUE);
        if (!terminal_output_ready(g_get_monotonic_time()))
            return ERROR_OUTPUT_IO;
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

    if (player->iter) {
        g_object_unref(player->iter);
        player->iter = NULL;
    }

    gboolean cleanup_ok = gif_player_reset_kitty_animation(player, TRUE);
    player->kitty_animation_disabled = FALSE;

    gif_player_clear_line_cache(player);
    
    // Reset iterator if needed to return to start
    // Next play or load will handle iterator reset
    
    return cleanup_ok ? ERROR_NONE : ERROR_OUTPUT_IO;
}

// Check if playing
gboolean gif_player_is_playing(const GifPlayer *player) {
    return player && player->is_playing;
}

// Check if animated GIF
gboolean gif_player_is_animated(const GifPlayer *player) {
    return player && player->is_animated;
}

gboolean gif_player_is_loaded_file(const GifPlayer *player, const gchar *filepath) {
    return player && filepath && player->filepath &&
           g_strcmp0(player->filepath, filepath) == 0;
}

void gif_player_set_color_enhance(GifPlayer *player, ColorEnhanceMode color_enhance) {
    if (!player || !player->renderer) {
        return;
    }
    player->renderer->config.color_enhance = color_enhance;
}

// Update terminal size for the internal renderer
ErrorCode gif_player_update_terminal_size(GifPlayer *player) {
    if (!player || !player->renderer) {
        return ERROR_INVALID_IMAGE;
    }
    return renderer_update_terminal_size(player->renderer);
}
