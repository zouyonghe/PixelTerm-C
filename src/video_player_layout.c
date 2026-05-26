#include "video_player_layout_internal.h"
#include "video_player_playback_internal.h"

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

enum {
    VIDEO_PLAYER_QUEUE_DEPTH_MEDIUM_AREA = 1500,
    VIDEO_PLAYER_QUEUE_DEPTH_LARGE_AREA = 3000,
    VIDEO_PLAYER_QUEUE_DEPTH_LARGE_SIZE = 4,
    VIDEO_PLAYER_QUEUE_DEPTH_MEDIUM_SIZE = 6,
    VIDEO_PLAYER_QUEUE_DEPTH_SMALL_SIZE = 8
};

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

/* ───── Slow level / late window ───── */

gint video_player_get_slow_level(VideoPlayer *player) {
    if (!player) {
        return 0;
    }

    gint frame_delay = video_player_get_frame_delay_ms(player);
    if (frame_delay <= 0) {
        return 0;
    }

    g_mutex_lock(&player->state_mutex);
    gboolean io_valid = player->io_avg_valid;
    gdouble io_avg = player->io_avg_ms;
    g_mutex_unlock(&player->state_mutex);
    if (!io_valid || io_avg <= 0.0) {
        return 0;
    }
    gdouble ratio = io_avg / (gdouble)frame_delay;
    if (ratio > 1.6) {
        return 2;
    }
    if (ratio > 1.2) {
        return 1;
    }
    return 0;
}

gint64 video_player_calc_late_window_ms(VideoPlayer *player, gint multiplier, gint min_ms) {
    if (!player || multiplier < 1) {
        return min_ms > 0 ? min_ms : 0;
    }
    gint frame_delay = video_player_get_frame_delay_ms(player);
    if (frame_delay <= 0) {
        frame_delay = 10;
    }
    gint64 base = (gint64)frame_delay * multiplier;
    if (min_ms > 0 && base < min_ms) {
        base = min_ms;
    }
    gint slow_level = video_player_get_slow_level(player);
    if (slow_level >= 2) {
        gint64 tight = frame_delay / 2;
        if (tight < 10) {
            tight = 10;
        }
        return tight;
    }
    if (slow_level == 1) {
        gint64 tight = frame_delay;
        if (min_ms > 0 && tight < min_ms) {
            tight = min_ms;
        }
        return tight;
    }
    return base;
}
