#include "video_player_layout_internal.h"

/* ───── Pixel mode label ───── */

const char *video_player_pixel_mode_label(ChafaPixelMode mode) {
    switch (mode) {
        case CHAFA_PIXEL_MODE_KITTY:
            return "kitty";
        case CHAFA_PIXEL_MODE_ITERM2:
            return "iterm2";
        case CHAFA_PIXEL_MODE_SIXELS:
            return "sixel";
        case CHAFA_PIXEL_MODE_SYMBOLS:
        default:
            return "text";
    }
}

/* ───── Line cache ───── */

void video_player_clear_line_cache(VideoPlayer *player) {
    if (!player || !player->last_frame_lines) {
        return;
    }
    g_ptr_array_free(player->last_frame_lines, TRUE);
    player->last_frame_lines = NULL;
}

/* ───── I/O timing averages ───── */

void video_player_update_io_avg(VideoPlayer *player, gint64 io_ms) {
    if (!player || io_ms < 0) {
        return;
    }
    const gdouble alpha = 0.2;
    g_mutex_lock(&player->state_mutex);
    if (!player->io_avg_valid) {
        player->io_avg_ms = (gdouble)io_ms;
        player->io_avg_valid = TRUE;
    } else {
        player->io_avg_ms = player->io_avg_ms * (1.0 - alpha) + (gdouble)io_ms * alpha;
    }
    g_mutex_unlock(&player->state_mutex);
}

void video_player_update_present_fps(VideoPlayer *player, gint64 now_us) {
    if (!player || now_us <= 0) {
        return;
    }
    g_mutex_lock(&player->state_mutex);
    if (player->last_present_us > 0) {
        gint64 delta_us = now_us - player->last_present_us;
        if (delta_us > 0) {
            gdouble fps = 1000000.0 / (gdouble)delta_us;
            const gdouble alpha = 0.2;
            if (!player->present_fps_valid) {
                player->present_fps = fps;
                player->present_fps_valid = TRUE;
            } else {
                player->present_fps = player->present_fps * (1.0 - alpha) + fps * alpha;
            }
        }
    }
    player->last_present_us = now_us;
    g_mutex_unlock(&player->state_mutex);
}

/* ───── Queue depth ───── */

void video_player_update_queue_depth(VideoPlayer *player, gint rendered_w, gint rendered_h) {
    if (!player) {
        return;
    }

    gint area = rendered_w * rendered_h;
    g_mutex_lock(&player->queue_mutex);
    if (area >= VIDEO_PLAYER_QUEUE_DEPTH_LARGE_AREA) {
        player->max_queue_size = VIDEO_PLAYER_QUEUE_DEPTH_LARGE_SIZE;
    } else if (area >= VIDEO_PLAYER_QUEUE_DEPTH_MEDIUM_AREA) {
        player->max_queue_size = VIDEO_PLAYER_QUEUE_DEPTH_MEDIUM_SIZE;
    } else {
        player->max_queue_size = VIDEO_PLAYER_QUEUE_DEPTH_SMALL_SIZE;
    }
    g_cond_broadcast(&player->frame_queue_has_space);
    g_mutex_unlock(&player->queue_mutex);
}
