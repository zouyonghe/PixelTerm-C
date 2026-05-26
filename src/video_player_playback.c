#include "video_player_playback_internal.h"

enum {
    VIDEO_PLAYER_LATE_DROP_BACKLOG_THRESHOLD = 5,
    VIDEO_PLAYER_MAX_SILENCE_US = 1000000
};

/* ───── Playback generation ───── */

guint video_player_generation_get(VideoPlayer *player) {
    if (!player) {
        return 0;
    }
    return (guint)g_atomic_int_get(&player->playback_generation);
}

guint video_player_generation_bump(VideoPlayer *player) {
    if (!player) {
        return 0;
    }
    return (guint)(g_atomic_int_add(&player->playback_generation, 1) + 1);
}

/* ───── Frame delay ───── */

gint video_player_get_frame_delay_ms(VideoPlayer *player) {
    if (!player) {
        return 0;
    }
    g_mutex_lock(&player->state_mutex);
    gint frame_delay = player->frame_delay_ms;
    g_mutex_unlock(&player->state_mutex);
    return frame_delay;
}

/* ───── EOF flags ───── */

gboolean video_player_is_eof_pending(VideoPlayer *player) {
    if (!player) {
        return FALSE;
    }
    g_mutex_lock(&player->state_mutex);
    gboolean eof_pending = player->eof_pending;
    g_mutex_unlock(&player->state_mutex);
    return eof_pending;
}

gboolean video_player_is_eof_ended(VideoPlayer *player) {
    if (!player) {
        return FALSE;
    }
    g_mutex_lock(&player->state_mutex);
    gboolean eof_ended = player->eof_ended;
    g_mutex_unlock(&player->state_mutex);
    return eof_ended;
}

/* ───── Clock / target PTS ───── */

gboolean video_player_get_target_pts_ms(VideoPlayer *player, gint64 *target_pts_ms) {
    if (!player || !target_pts_ms) {
        return FALSE;
    }
    gint64 start_us = 0, start_pts_ms = 0;
    gboolean started = FALSE;
    g_mutex_lock(&player->state_mutex);
    started = player->clock_started;
    if (started) {
        start_us = player->clock_start_us;
        start_pts_ms = player->clock_start_pts_ms;
    }
    g_mutex_unlock(&player->state_mutex);
    if (!started) {
        return FALSE;
    }
    gint64 now_us = g_get_monotonic_time();
    gint64 elapsed_ms = (now_us - start_us) / 1000;
    *target_pts_ms = start_pts_ms + elapsed_ms;
    return TRUE;
}

/* ───── Frame queue helpers (read-only access for timing) ───── */

guint video_player_queue_length(VideoPlayer *player) {
    if (!player || !player->frame_queue) {
        return 0;
    }
    g_mutex_lock(&player->queue_mutex);
    guint length = g_queue_get_length(player->frame_queue);
    g_mutex_unlock(&player->queue_mutex);
    return length;
}

gboolean video_player_queue_peek_pts_ms(VideoPlayer *player, gint64 *pts_ms) {
    if (!player || !player->frame_queue || !pts_ms) {
        return FALSE;
    }
    g_mutex_lock(&player->queue_mutex);
    if (g_queue_is_empty(player->frame_queue)) {
        g_mutex_unlock(&player->queue_mutex);
        return FALSE;
    }
    VideoFrame *frame = (VideoFrame *)g_queue_peek_head(player->frame_queue);
    if (!frame) {
        g_mutex_unlock(&player->queue_mutex);
        return FALSE;
    }
    *pts_ms = frame->pts_ms;
    g_mutex_unlock(&player->queue_mutex);
    return TRUE;
}

/* ───── Calc delay ───── */

gint video_player_calc_delay_ms(VideoPlayer *player) {
    if (!player) {
        return 10;
    }
    gint64 target_pts_ms = 0;
    if (!video_player_get_target_pts_ms(player, &target_pts_ms)) {
        return 5;
    }
    gint64 next_pts_ms = 0;
    if (!video_player_queue_peek_pts_ms(player, &next_pts_ms)) {
        gint64 last_present_us = 0;
        g_mutex_lock(&player->state_mutex);
        last_present_us = player->last_present_us;
        g_mutex_unlock(&player->state_mutex);
        if (last_present_us > 0) {
            return 5;
        }
        gint delay = video_player_get_frame_delay_ms(player);
        if (delay <= 0) delay = 10;
        if (delay < 5) delay = 5;
        return delay;
    }
    gint64 wait_ms = next_pts_ms - target_pts_ms;
    if (wait_ms < 1) wait_ms = 1;
    return (gint)wait_ms;
}

/* ───── Slow level / late window ─────
 *
 * These helpers derive timing thresholds from frame_delay_ms (playback) and
 * io_avg_ms (layout-owned but accessed read-only here under state_mutex).
 * They live in the playback module because the values are timing-derived and
 * consumed by playback gating decisions (late drop, frame waiting). */

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

/* ───── Late frame drop logic ───── */

gboolean video_player_should_drop_late_frame(VideoPlayer *player, gint64 pts_ms) {
    if (!player) {
        return FALSE;
    }
    g_mutex_lock(&player->state_mutex);
    gboolean rewind_needs_resync = player->rewind_needs_resync;
    g_mutex_unlock(&player->state_mutex);
    if (rewind_needs_resync) {
        return FALSE;
    }

    gint64 target_pts_ms = 0;
    if (!video_player_get_target_pts_ms(player, &target_pts_ms)) {
        return FALSE;
    }

    gint64 late_ms = target_pts_ms - pts_ms;
    gint64 late_threshold = video_player_calc_late_window_ms(player, 1, 10);
    if (late_ms <= late_threshold) {
        return FALSE;
    }

    guint backlog = video_player_queue_length(player);
    if (backlog < VIDEO_PLAYER_LATE_DROP_BACKLOG_THRESHOLD) {
        return FALSE;
    }

    gint64 last_presented_pts_ms = G_MININT64;
    g_mutex_lock(&player->state_mutex);
    last_presented_pts_ms = player->last_presented_pts_ms;
    g_mutex_unlock(&player->state_mutex);
    if (last_presented_pts_ms != G_MININT64 && pts_ms <= last_presented_pts_ms) {
        return TRUE;
    }

    gint64 now_us = g_get_monotonic_time();
    gint64 last_present_us = 0;
    g_mutex_lock(&player->state_mutex);
    last_present_us = player->last_present_us;
    g_mutex_unlock(&player->state_mutex);

    gint64 max_silence_us = VIDEO_PLAYER_MAX_SILENCE_US;
    return last_present_us > 0 && (now_us - last_present_us) < max_silence_us;
}
