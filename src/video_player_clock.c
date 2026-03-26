#include "video_player_clock_internal.h"

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
    updated_fallback_pts_ms = resolved_pts_ms + frame_delay;
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
            position_ms += elapsed_us / 1000;
        }
    }

    return position_ms < 0 ? 0 : position_ms;
}
