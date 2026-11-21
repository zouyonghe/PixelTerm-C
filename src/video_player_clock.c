#include "video_player_clock_internal.h"

static gint64 add_pts_ms(gint64 pts_ms, gint64 delta_ms) {
    if (delta_ms > 0 && pts_ms > G_MAXINT64 - delta_ms) return G_MAXINT64;
    if (delta_ms < 0 && pts_ms < G_MININT64 - delta_ms) return G_MININT64;
    return pts_ms + delta_ms;
}

void video_player_set_fallback_pts_ms(VideoPlayer *player, gint64 pts_ms) {
    if (!player) {
        return;
    }

    g_mutex_lock(&player->state_mutex);
    player->fallback_pts_ms = pts_ms;
    g_mutex_unlock(&player->state_mutex);
}

gint64 video_player_resolve_and_advance_fallback_pts_ms(VideoPlayer *player,
                                                        gint64 raw_pts_ms,
                                                        gint frame_delay,
                                                        gint64 *next_fallback_pts_ms) {
    gint64 resolved_pts_ms = raw_pts_ms;
    gint64 updated_fallback_pts_ms = 0;

    if (!player) {
        if (next_fallback_pts_ms) {
            *next_fallback_pts_ms = 0;
        }
        return resolved_pts_ms;
    }

    g_mutex_lock(&player->state_mutex);
    if (resolved_pts_ms == G_MININT64) {
        resolved_pts_ms = player->fallback_pts_ms;
    }
    updated_fallback_pts_ms = add_pts_ms(resolved_pts_ms, frame_delay);
    player->fallback_pts_ms = updated_fallback_pts_ms;
    g_mutex_unlock(&player->state_mutex);

    if (next_fallback_pts_ms) {
        *next_fallback_pts_ms = updated_fallback_pts_ms;
    }
    return resolved_pts_ms;
}

gint64 video_player_current_position_ms(VideoPlayer *player) {
    if (!player) {
        return 0;
    }

    gint64 last_presented_pts_ms = G_MININT64;
    gint64 fallback_pts_ms = 0;
    gboolean clock_started = FALSE;
    gint64 clock_start_us = 0;
    gint64 clock_start_pts_ms = 0;
    gboolean is_playing = FALSE;

    g_mutex_lock(&player->state_mutex);
    last_presented_pts_ms = player->last_presented_pts_ms;
    fallback_pts_ms = player->fallback_pts_ms;
    clock_started = player->clock_started;
    clock_start_us = player->clock_start_us;
    clock_start_pts_ms = player->clock_start_pts_ms;
    is_playing = player->is_playing;
    g_mutex_unlock(&player->state_mutex);

    if (last_presented_pts_ms != G_MININT64) {
        return last_presented_pts_ms < 0 ? 0 : last_presented_pts_ms;
    }

    if (!clock_started) {
        return fallback_pts_ms < 0 ? 0 : fallback_pts_ms;
    }

    gint64 position_ms = clock_start_pts_ms;
    if (is_playing && clock_start_us > 0) {
        gint64 elapsed_us = g_get_monotonic_time() - clock_start_us;
        if (elapsed_us > 0) {
            position_ms = add_pts_ms(position_ms, elapsed_us / 1000);
        }
    }

    return position_ms < 0 ? 0 : position_ms;
}

gboolean video_player_get_target_pts_ms(VideoPlayer *player, gint64 *target_pts_ms) {
    if (!player || !target_pts_ms) return FALSE;

    g_mutex_lock(&player->state_mutex);
    gboolean started = player->clock_started;
    gint64 start_us = player->clock_start_us;
    gint64 start_pts_ms = player->clock_start_pts_ms;
    g_mutex_unlock(&player->state_mutex);
    if (!started) return FALSE;

    gint64 elapsed_ms = (g_get_monotonic_time() - start_us) / 1000;
    *target_pts_ms = add_pts_ms(start_pts_ms, elapsed_ms);
    return TRUE;
}

void video_player_reset_timing_state(VideoPlayer *player) {
    if (!player) {
        return;
    }

    g_mutex_lock(&player->state_mutex);
    player->clock_started = FALSE;
    player->rewind_needs_resync = FALSE;
    player->eof_pending = FALSE;
    player->eof_ended = FALSE;
    player->clock_start_us = 0;
    player->clock_start_pts_ms = 0;
    player->smooth_last_pts_ms = 0;
    player->smooth_pts_ms = 0;
    player->smooth_valid = FALSE;
    player->io_avg_ms = 0.0;
    player->io_avg_valid = FALSE;
    player->last_present_us = 0;
    player->last_presented_pts_ms = G_MININT64;
    player->present_fps = 0.0;
    player->present_fps_valid = FALSE;
    player->fallback_pts_ms = 0;
    player->draining = FALSE;
    g_mutex_unlock(&player->state_mutex);
}

void video_player_restore_paused_seek_target(VideoPlayer *player, gint64 target_ms) {
    if (!player) {
        return;
    }

    g_mutex_lock(&player->state_mutex);
    player->clock_started = FALSE;
    player->rewind_needs_resync = TRUE;
    player->clock_start_us = 0;
    player->clock_start_pts_ms = 0;
    player->last_presented_pts_ms = G_MININT64;
    player->fallback_pts_ms = target_ms;
    g_mutex_unlock(&player->state_mutex);
}

gint video_player_frame_delay_from_rate(gint numerator, gint denominator) {
    if (numerator <= 0 || denominator <= 0) return 33;
    gint64 delay = 1000LL * denominator / numerator;
    return (gint)CLAMP(delay, 1, G_MAXINT);
}

gint64 video_player_smooth_pts_ms(VideoPlayer *player, gint64 raw_pts_ms, gint frame_delay) {
    gint64 pts_ms = raw_pts_ms;
    g_mutex_lock(&player->state_mutex);
    if (player->smooth_valid) {
        guint64 min_step = MAX(frame_delay / 2, 1);
        guint64 max_step = 2 * (guint64)MAX(frame_delay, 1);
        guint64 delta = raw_pts_ms > player->smooth_last_pts_ms
            ? (guint64)raw_pts_ms - (guint64)player->smooth_last_pts_ms : 0;
        pts_ms = add_pts_ms(player->smooth_pts_ms, (gint64)CLAMP(delta, min_step, max_step));
    } else {
        player->smooth_valid = TRUE;
    }
    player->smooth_last_pts_ms = raw_pts_ms;
    player->smooth_pts_ms = pts_ms;
    g_mutex_unlock(&player->state_mutex);
    return pts_ms;
}
