#include <glib.h>

#include "video_player_test_internal.h"

typedef RenderedFrame VideoFrame;

static gsize test_video_player_sync_once = 0;

static GMutex decode_queue_push_mutex;
static GCond decode_queue_push_cond;
static gboolean decode_queue_push_started = FALSE;

static GMutex queue_push_mutex;
static GCond queue_push_cond;
static gboolean queue_push_started = FALSE;

static void wait_for_flag_or_fail(GMutex *mutex, GCond *cond, gboolean *flag, const gchar *message) {
    gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_MILLISECOND * 500;

    g_mutex_lock(mutex);
    while (!*flag) {
        if (!g_cond_wait_until(cond, mutex, deadline)) {
            g_mutex_unlock(mutex);
            g_error("%s", message);
        }
    }
    g_mutex_unlock(mutex);
}

static void init_test_sync_primitives(void) {
    if (g_once_init_enter(&test_video_player_sync_once)) {
        g_mutex_init(&decode_queue_push_mutex);
        g_cond_init(&decode_queue_push_cond);
        g_mutex_init(&queue_push_mutex);
        g_cond_init(&queue_push_cond);
        g_once_init_leave(&test_video_player_sync_once, 1);
    }
}

static VideoFrame *make_test_frame_with_generation(gint64 pts_ms, guint generation) {
    VideoFrame *frame = g_new0(VideoFrame, 1);
    g_assert_nonnull(frame);
    frame->rendered = g_string_new("frame");
    frame->rendered_width = 5;
    frame->rendered_height = 1;
    frame->pts_ms = pts_ms;
    frame->pixel_mode = CHAFA_PIXEL_MODE_SYMBOLS;
    frame->generation = generation;
    return frame;
}

static VideoFrame *make_test_frame(gint64 pts_ms) {
    return make_test_frame_with_generation(pts_ms, 1);
}

static guint video_player_queue_length_for_test(VideoPlayer *player) {
    guint length = 0;
    g_mutex_lock(&player->queue_mutex);
    length = g_queue_get_length(player->frame_queue);
    g_mutex_unlock(&player->queue_mutex);
    return length;
}

static gint64 video_player_queue_head_pts_for_test(VideoPlayer *player) {
    gint64 pts = -1;
    g_mutex_lock(&player->queue_mutex);
    VideoFrame *frame = player && player->frame_queue ? (VideoFrame *)g_queue_peek_head(player->frame_queue) : NULL;
    if (frame) {
        pts = frame->pts_ms;
    }
    g_mutex_unlock(&player->queue_mutex);
    return pts;
}

static void video_player_queue_insert_sorted_for_test(VideoPlayer *player, VideoFrame *frame) {
    video_player_queue_insert_sorted(player, frame);
}

static gint64 video_player_queue_pop_pts_for_test(VideoPlayer *player) {
    VideoFrame *frame = video_player_queue_take_first(player);
    gint64 pts = -1;
    if (frame) {
        pts = frame->pts_ms;
        video_frame_destroy(frame);
    }
    return pts;
}

static void decoded_frame_fill(DecodedFrame *frame, gint64 pts_ms, guint generation) {
    frame->pixels = g_malloc(4);
    g_assert_nonnull(frame->pixels);
    frame->pixels[0] = 0;
    frame->pixels[1] = 0;
    frame->pixels[2] = 0;
    frame->pixels[3] = 255;
    frame->width = 1;
    frame->height = 1;
    frame->rowstride = 4;
    frame->pts_ms = pts_ms;
    frame->generation = generation;
}

static void test_decode_queue_clear_removes_all_decoded_frames(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    DecodedFrame *a = g_new0(DecodedFrame, 1);
    DecodedFrame *b = g_new0(DecodedFrame, 1);
    decoded_frame_fill(a, 100, 1);
    decoded_frame_fill(b, 133, 1);
    g_queue_push_tail(player->decode_queue, a);
    g_queue_push_tail(player->decode_queue, b);
    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 2);

    video_player_decode_queue_clear(player);

    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 0);

    video_player_destroy(player);
}

static gint64 decode_queue_head_pts_for_test(VideoPlayer *player) {
    gint64 pts = -1;
    g_mutex_lock(&player->queue_mutex);
    DecodedFrame *frame = player && player->decode_queue ? (DecodedFrame *)g_queue_peek_head(player->decode_queue) : NULL;
    if (frame) {
        pts = frame->pts_ms;
    }
    g_mutex_unlock(&player->queue_mutex);
    return pts;
}

static gpointer decode_queue_push_thread_main(gpointer user_data) {
    VideoPlayer *player = (VideoPlayer *)user_data;
    DecodedFrame *frame = g_new0(DecodedFrame, 1);
    decoded_frame_fill(frame, 233, 1);

    g_mutex_lock(&decode_queue_push_mutex);
    decode_queue_push_started = TRUE;
    g_cond_broadcast(&decode_queue_push_cond);
    g_mutex_unlock(&decode_queue_push_mutex);

    video_player_decode_queue_push(player, frame);
    return NULL;
}

static void test_decode_queue_sixel_mode_waits_instead_of_replacing_oldest(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    player->renderer->config.force_text = FALSE;
    player->renderer->config.force_sixel = TRUE;
    init_test_sync_primitives();

    DecodedFrame *a = g_new0(DecodedFrame, 1);
    DecodedFrame *b = g_new0(DecodedFrame, 1);
    DecodedFrame *c = g_new0(DecodedFrame, 1);
    DecodedFrame *d = g_new0(DecodedFrame, 1);
    decoded_frame_fill(a, 100, 1);
    decoded_frame_fill(b, 133, 1);
    decoded_frame_fill(c, 167, 1);
    decoded_frame_fill(d, 200, 1);

    video_player_decode_queue_push(player, a);
    video_player_decode_queue_push(player, b);
    video_player_decode_queue_push(player, c);
    video_player_decode_queue_push(player, d);
    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 4);
    g_assert_cmpint(decode_queue_head_pts_for_test(player), ==, 100);

    g_mutex_lock(&decode_queue_push_mutex);
    decode_queue_push_started = FALSE;
    g_mutex_unlock(&decode_queue_push_mutex);

    GThread *thread = g_thread_new("decode-queue-push-test", decode_queue_push_thread_main, player);
    wait_for_flag_or_fail(&decode_queue_push_mutex,
                          &decode_queue_push_cond,
                          &decode_queue_push_started,
                          "Timed out waiting for decode queue push thread to start");

    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 4);
    g_assert_cmpint(decode_queue_head_pts_for_test(player), ==, 100);

    DecodedFrame *oldest = video_player_decode_queue_take(player);
    g_assert_nonnull(oldest);
    g_assert_cmpint(oldest->pts_ms, ==, 100);
    decoded_frame_destroy(oldest);

    g_thread_join(thread);

    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 4);
    g_assert_cmpint(decode_queue_head_pts_for_test(player), ==, 133);

    video_player_destroy(player);
}

static void test_decode_queue_text_mode_waits_instead_of_replacing_oldest(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    DecodedFrame *a = g_new0(DecodedFrame, 1);
    DecodedFrame *b = g_new0(DecodedFrame, 1);
    DecodedFrame *c = g_new0(DecodedFrame, 1);
    DecodedFrame *d = g_new0(DecodedFrame, 1);
    decoded_frame_fill(a, 100, 1);
    decoded_frame_fill(b, 133, 1);
    decoded_frame_fill(c, 167, 1);
    decoded_frame_fill(d, 200, 1);

    video_player_decode_queue_push(player, a);
    video_player_decode_queue_push(player, b);
    video_player_decode_queue_push(player, c);
    video_player_decode_queue_push(player, d);
    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 4);
    g_assert_cmpint(decode_queue_head_pts_for_test(player), ==, 100);

    g_mutex_lock(&decode_queue_push_mutex);
    decode_queue_push_started = FALSE;
    g_mutex_unlock(&decode_queue_push_mutex);

    GThread *thread = g_thread_new("decode-queue-push-test", decode_queue_push_thread_main, player);
    wait_for_flag_or_fail(&decode_queue_push_mutex,
                          &decode_queue_push_cond,
                          &decode_queue_push_started,
                          "Timed out waiting for decode queue push thread to start");

    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 4);
    g_assert_cmpint(decode_queue_head_pts_for_test(player), ==, 100);

    DecodedFrame *oldest = video_player_decode_queue_take(player);
    g_assert_nonnull(oldest);
    g_assert_cmpint(oldest->pts_ms, ==, 100);
    decoded_frame_destroy(oldest);

    g_thread_join(thread);

    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 4);
    g_assert_cmpint(decode_queue_head_pts_for_test(player), ==, 133);

    video_player_destroy(player);
}

static void test_generation_counter_starts_initialized(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    g_assert_cmpuint(player->playback_generation, ==, 1);

    video_player_destroy(player);
}

static void test_render_layout_generation_starts_initialized(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    g_assert_cmpuint(player->render_layout_generation, ==, 1);

    video_player_destroy(player);
}

static void test_render_layout_generation_increments_only_on_layout_change(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    guint initial_generation = player->render_layout_generation;
    video_player_set_render_area(player, 120, 40, 4, 30, 80, 24);
    g_assert_cmpuint(player->render_layout_generation, ==, initial_generation + 1);

    guint changed_generation = player->render_layout_generation;
    video_player_set_render_area(player, 120, 40, 4, 30, 80, 24);
    g_assert_cmpuint(player->render_layout_generation, ==, changed_generation);

    video_player_set_render_area(player, 120, 40, 4, 30, 78, 24);
    g_assert_cmpuint(player->render_layout_generation, ==, changed_generation + 1);

    video_player_destroy(player);
}

static void test_reset_timing_state_clears_loop_sensitive_fields(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    player->draining = TRUE;
    player->clock_started = TRUE;
    player->clock_start_us = 1234;
    player->clock_start_pts_ms = 5678;
    player->fallback_pts_ms = 999;
    player->smooth_last_pts_ms = 222;
    player->smooth_pts_ms = 333;
    player->smooth_valid = TRUE;
    player->io_avg_ms = 12.5;
    player->io_avg_valid = TRUE;
    player->last_present_us = 4444;
    player->present_fps = 27.0;
    player->present_fps_valid = TRUE;

    video_player_reset_timing_state(player);

    g_assert_false(player->draining);
    g_assert_false(player->clock_started);
    g_assert_cmpint(player->clock_start_us, ==, 0);
    g_assert_cmpint(player->clock_start_pts_ms, ==, 0);
    g_assert_cmpint(player->fallback_pts_ms, ==, 0);
    g_assert_cmpint(player->smooth_last_pts_ms, ==, 0);
    g_assert_cmpint(player->smooth_pts_ms, ==, 0);
    g_assert_false(player->smooth_valid);
    g_assert_cmpfloat(player->io_avg_ms, ==, 0.0);
    g_assert_false(player->io_avg_valid);
    g_assert_cmpint(player->last_present_us, ==, 0);
    g_assert_cmpfloat(player->present_fps, ==, 0.0);
    g_assert_false(player->present_fps_valid);

    video_player_destroy(player);
}

static void test_render_queue_insert_sorted_orders_by_pts(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    video_player_queue_insert_sorted_for_test(player, make_test_frame(200));
    video_player_queue_insert_sorted_for_test(player, make_test_frame(100));
    video_player_queue_insert_sorted_for_test(player, make_test_frame(150));

    g_assert_cmpint(video_player_queue_pop_pts_for_test(player), ==, 100);
    g_assert_cmpint(video_player_queue_pop_pts_for_test(player), ==, 150);
    g_assert_cmpint(video_player_queue_pop_pts_for_test(player), ==, 200);

    video_player_destroy(player);
}

static void test_queue_clear_removes_all_rendered_frames(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    video_player_queue_push(player, make_test_frame(100));
    video_player_queue_push(player, make_test_frame(133));
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 2);

    video_player_queue_clear(player);

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);
    g_assert_cmpint(video_player_queue_head_pts_for_test(player), ==, -1);

    video_player_destroy(player);
}

static void test_should_not_drop_late_frame_when_backlog_is_shallow(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    gint64 now_us = g_get_monotonic_time();
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_us = now_us - 200 * 1000;
    player->clock_start_pts_ms = 0;
    player->last_present_us = now_us - 10 * 1000;
    player->io_avg_valid = TRUE;
    player->io_avg_ms = (gdouble)player->frame_delay_ms;
    g_mutex_unlock(&player->state_mutex);

    g_assert_false(video_player_should_drop_late_frame(player, 10));

    VideoFrame *queued = g_new0(VideoFrame, 1);
    g_assert_nonnull(queued);
    queued->rendered = g_string_new("queued");
    queued->pts_ms = 250;
    video_player_queue_push(player, queued);

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 1);
    g_assert_false(video_player_should_drop_late_frame(player, 10));

    video_player_destroy(player);
}

static void test_should_drop_late_frame_when_backlog_is_deep(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    gint64 now_us = g_get_monotonic_time();
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_us = now_us - 200 * 1000;
    player->clock_start_pts_ms = 0;
    player->last_present_us = now_us - 10 * 1000;
    player->io_avg_valid = TRUE;
    player->io_avg_ms = (gdouble)player->frame_delay_ms;
    g_mutex_unlock(&player->state_mutex);

    video_player_queue_push(player, make_test_frame(250));
    video_player_queue_push(player, make_test_frame(260));
    video_player_queue_push(player, make_test_frame(270));
    video_player_queue_push(player, make_test_frame(280));
    video_player_queue_push(player, make_test_frame(290));

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 5);
    g_assert_true(video_player_should_drop_late_frame(player, 10));

    video_player_destroy(player);
}

static void test_queue_take_for_playback_waits_even_when_future_queue_is_full(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    video_player_queue_push(player, make_test_frame(100));
    video_player_queue_push(player, make_test_frame(110));
    video_player_queue_push(player, make_test_frame(120));
    video_player_queue_push(player, make_test_frame(130));

    VideoFrame *frame = video_player_queue_take_for_playback(player, 50, 20);
    g_assert_null(frame);

    video_player_destroy(player);
}

static void test_queue_take_for_playback_waits_when_future_queue_is_not_full(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    video_player_queue_push(player, make_test_frame(100));
    video_player_queue_push(player, make_test_frame(110));

    VideoFrame *frame = video_player_queue_take_for_playback(player, 50, 20);
    g_assert_null(frame);

    video_player_destroy(player);
}

static void test_calc_delay_uses_queue_head_even_with_high_io_avg(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    gint64 now_us = g_get_monotonic_time();
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_us = now_us - 100 * 1000;
    player->clock_start_pts_ms = 0;
    player->io_avg_valid = TRUE;
    player->io_avg_ms = 80.0;
    g_mutex_unlock(&player->state_mutex);

    video_player_queue_push(player, make_test_frame(120));
    video_player_queue_push(player, make_test_frame(153));

    gint delay = video_player_calc_delay_ms(player);
    g_assert_cmpint(delay, >=, 15);
    g_assert_cmpint(delay, <=, 25);

    video_player_destroy(player);
}

static void test_calc_delay_retries_quickly_when_playing_and_queue_is_empty(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    gint64 now_us = g_get_monotonic_time();
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_us = now_us - 100 * 1000;
    player->clock_start_pts_ms = 0;
    player->last_present_us = now_us - 10 * 1000;
    g_mutex_unlock(&player->state_mutex);

    gint delay = video_player_calc_delay_ms(player);
    g_assert_cmpint(delay, ==, 5);

    video_player_destroy(player);
}

static void test_should_not_drop_late_frame_when_backlog_is_medium(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    gint64 now_us = g_get_monotonic_time();
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_us = now_us - 200 * 1000;
    player->clock_start_pts_ms = 0;
    player->last_present_us = now_us - 10 * 1000;
    player->io_avg_valid = TRUE;
    player->io_avg_ms = (gdouble)player->frame_delay_ms;
    g_mutex_unlock(&player->state_mutex);

    video_player_queue_push(player, make_test_frame(250));
    video_player_queue_push(player, make_test_frame(260));
    video_player_queue_push(player, make_test_frame(270));
    video_player_queue_push(player, make_test_frame(280));

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 4);
    g_assert_false(video_player_should_drop_late_frame(player, 10));

    video_player_destroy(player);
}

static void test_should_not_drop_late_frame_when_backlog_is_deep_and_silence_exceeded(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    gint64 now_us = g_get_monotonic_time();
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_us = now_us - 200 * 1000;
    player->clock_start_pts_ms = 0;
    player->last_present_us = now_us - 2 * 1000 * 1000;
    player->io_avg_valid = TRUE;
    player->io_avg_ms = (gdouble)player->frame_delay_ms;
    g_mutex_unlock(&player->state_mutex);

    video_player_queue_push(player, make_test_frame(250));
    video_player_queue_push(player, make_test_frame(260));
    video_player_queue_push(player, make_test_frame(270));
    video_player_queue_push(player, make_test_frame(280));
    video_player_queue_push(player, make_test_frame(290));

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 5);
    g_assert_false(video_player_should_drop_late_frame(player, 10));

    video_player_destroy(player);
}

static gpointer queue_push_thread_main(gpointer user_data) {
    VideoPlayer *player = (VideoPlayer *)user_data;

    g_mutex_lock(&queue_push_mutex);
    queue_push_started = TRUE;
    g_cond_broadcast(&queue_push_cond);
    g_mutex_unlock(&queue_push_mutex);

    video_player_queue_push(player, make_test_frame(167));
    return NULL;
}

static void test_update_queue_depth_uses_smaller_queue_for_large_render_geometry(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    player->max_queue_size = 8;
    video_player_update_queue_depth(player, 69, 61);
    g_assert_cmpint(player->max_queue_size, ==, 4);

    video_player_destroy(player);
}

static void test_update_queue_depth_keeps_larger_queue_for_small_render_geometry(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    player->max_queue_size = 8;
    video_player_update_queue_depth(player, 21, 18);
    g_assert_cmpint(player->max_queue_size, ==, 8);

    video_player_destroy(player);
}

static void test_update_queue_depth_uses_medium_queue_for_mid_render_geometry(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    player->max_queue_size = 8;
    video_player_update_queue_depth(player, 50, 40);
    g_assert_cmpint(player->max_queue_size, ==, 6);

    video_player_destroy(player);
}

static void test_update_queue_depth_uses_low_latency_queue_for_sixel_large_geometry(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    player->renderer->config.force_text = FALSE;
    player->renderer->config.force_sixel = TRUE;
    player->max_queue_size = 8;
    video_player_update_queue_depth(player, 69, 61);
    g_assert_cmpint(player->max_queue_size, ==, 4);

    video_player_destroy(player);
}

static void test_queue_push_waits_for_capacity_instead_of_dropping_new_frame(void) {
    VideoPlayer *player = video_player_new(2, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    player->max_queue_size = 2;
    init_test_sync_primitives();
    video_player_queue_push(player, make_test_frame(100));
    video_player_queue_push(player, make_test_frame(133));
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 2);

    g_mutex_lock(&queue_push_mutex);
    queue_push_started = FALSE;
    g_mutex_unlock(&queue_push_mutex);

    GThread *thread = g_thread_new("queue-push-test", queue_push_thread_main, player);
    wait_for_flag_or_fail(&queue_push_mutex,
                          &queue_push_cond,
                          &queue_push_started,
                          "Timed out waiting for render queue push thread to start");
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 2);
    g_assert_cmpint(video_player_queue_head_pts_for_test(player), ==, 100);

    VideoFrame *taken = video_player_queue_take_first(player);
    g_assert_nonnull(taken);
    g_assert_cmpint(taken->pts_ms, ==, 100);
    video_frame_destroy(taken);

    g_thread_join(thread);

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 2);
    g_assert_cmpint(video_player_queue_head_pts_for_test(player), ==, 133);

    video_player_destroy(player);
}

static void test_rewind_reset_clears_timing_and_queue_state(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    player->clock_started = TRUE;
    player->clock_start_us = 1234;
    player->clock_start_pts_ms = 5678;
    player->fallback_pts_ms = 9012;
    player->smooth_last_pts_ms = 111;
    player->smooth_pts_ms = 222;
    player->smooth_valid = TRUE;
    player->draining = TRUE;
    video_player_queue_push(player, make_test_frame(100));
    video_player_queue_push(player, make_test_frame(133));
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 2);

    video_player_queue_clear(player);
    video_player_reset_timing_state(player);

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);
    g_assert_false(player->clock_started);
    g_assert_cmpint(player->clock_start_us, ==, 0);
    g_assert_cmpint(player->clock_start_pts_ms, ==, 0);
    g_assert_cmpint(player->fallback_pts_ms, ==, 0);
    g_assert_cmpint(player->smooth_last_pts_ms, ==, 0);
    g_assert_cmpint(player->smooth_pts_ms, ==, 0);
    g_assert_false(player->smooth_valid);
    g_assert_false(player->draining);

    video_player_destroy(player);
}

static void test_should_not_drop_late_frame_without_clock(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    video_player_queue_push(player, make_test_frame(100));
    video_player_queue_push(player, make_test_frame(133));
    video_player_queue_push(player, make_test_frame(167));
    video_player_queue_push(player, make_test_frame(200));
    video_player_queue_push(player, make_test_frame(233));

    g_assert_false(player->clock_started);
    g_assert_false(video_player_should_drop_late_frame(player, 100));

    video_player_destroy(player);
}

static void test_should_not_drop_late_frame_before_rewind_resync(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    gint64 now_us = g_get_monotonic_time();
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_us = now_us - 200 * 1000;
    player->clock_start_pts_ms = 0;
    player->last_present_us = now_us - 10 * 1000;
    player->io_avg_valid = TRUE;
    player->io_avg_ms = (gdouble)player->frame_delay_ms;
    g_mutex_unlock(&player->state_mutex);
    player->rewind_needs_resync = TRUE;

    video_player_queue_push(player, make_test_frame(100));
    video_player_queue_push(player, make_test_frame(133));
    video_player_queue_push(player, make_test_frame(167));
    video_player_queue_push(player, make_test_frame(200));
    video_player_queue_push(player, make_test_frame(233));

    g_assert_false(video_player_should_drop_late_frame(player, 100));

    video_player_destroy(player);
}

static void test_debug_logging_honors_environment_toggle(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    video_player_debug_reset_for_test();
    g_unsetenv("PIXELTERM_DEBUG_VIDEO");
    g_assert_false(video_player_debug_enabled());

    video_player_debug_reset_for_test();
    g_setenv("PIXELTERM_DEBUG_VIDEO", "1", TRUE);
    g_assert_true(video_player_debug_enabled());
    g_assert_nonnull(video_player_debug_get_stream());
    g_unsetenv("PIXELTERM_DEBUG_VIDEO");
    video_player_debug_reset_for_test();

    video_player_destroy(player);
}

static void test_debug_logging_closes_stream_when_last_player_is_destroyed(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    video_player_debug_reset_for_test();
    g_setenv("PIXELTERM_DEBUG_VIDEO", "1", TRUE);
    g_assert_nonnull(video_player_debug_get_stream());
    g_assert_true(video_player_debug_has_current_stream_for_test());

    video_player_destroy(player);

    g_assert_false(video_player_debug_has_current_stream_for_test());
    g_unsetenv("PIXELTERM_DEBUG_VIDEO");
    video_player_debug_reset_for_test();
}

void register_video_player_tests(void) {
    g_test_add_func("/video_player/reset_timing_state/clears_loop_sensitive_fields",
                    test_reset_timing_state_clears_loop_sensitive_fields);
    g_test_add_func("/video_player/render_queue/insert_sorted_orders_by_pts",
                    test_render_queue_insert_sorted_orders_by_pts);
    g_test_add_func("/video_player/render_queue/clear_removes_all_rendered_frames",
                    test_queue_clear_removes_all_rendered_frames);
    g_test_add_func("/video_player/decode_queue/clear_removes_all_decoded_frames",
                    test_decode_queue_clear_removes_all_decoded_frames);
    g_test_add_func("/video_player/decode_queue/sixel_mode_waits_instead_of_replacing_oldest",
                    test_decode_queue_sixel_mode_waits_instead_of_replacing_oldest);
    g_test_add_func("/video_player/decode_queue/text_mode_waits_instead_of_replacing_oldest",
                    test_decode_queue_text_mode_waits_instead_of_replacing_oldest);
    g_test_add_func("/video_player/generation_counter/starts_initialized",
                    test_generation_counter_starts_initialized);
    g_test_add_func("/video_player/render_layout_generation/starts_initialized",
                    test_render_layout_generation_starts_initialized);
    g_test_add_func("/video_player/render_layout_generation/increments_only_on_layout_change",
                    test_render_layout_generation_increments_only_on_layout_change);
    g_test_add_func("/video_player/drop_late_frame/does_not_drop_when_backlog_is_shallow",
                    test_should_not_drop_late_frame_when_backlog_is_shallow);
    g_test_add_func("/video_player/drop_late_frame/does_not_drop_when_backlog_is_medium",
                    test_should_not_drop_late_frame_when_backlog_is_medium);
    g_test_add_func("/video_player/drop_late_frame/drops_when_backlog_is_deep",
                    test_should_drop_late_frame_when_backlog_is_deep);
    g_test_add_func("/video_player/drop_late_frame/does_not_drop_when_silence_exceeded",
                    test_should_not_drop_late_frame_when_backlog_is_deep_and_silence_exceeded);
    g_test_add_func("/video_player/drop_late_frame/does_not_drop_without_clock",
                    test_should_not_drop_late_frame_without_clock);
    g_test_add_func("/video_player/drop_late_frame/does_not_drop_before_rewind_resync",
                    test_should_not_drop_late_frame_before_rewind_resync);
    g_test_add_func("/video_player/queue_push/waits_for_capacity_instead_of_dropping_new_frame",
                    test_queue_push_waits_for_capacity_instead_of_dropping_new_frame);
    g_test_add_func("/video_player/queue_depth/uses_smaller_queue_for_large_render_geometry",
                    test_update_queue_depth_uses_smaller_queue_for_large_render_geometry);
    g_test_add_func("/video_player/queue_depth/uses_medium_queue_for_mid_render_geometry",
                    test_update_queue_depth_uses_medium_queue_for_mid_render_geometry);
    g_test_add_func("/video_player/queue_depth/uses_low_latency_queue_for_sixel_large_geometry",
                    test_update_queue_depth_uses_low_latency_queue_for_sixel_large_geometry);
    g_test_add_func("/video_player/queue_depth/keeps_larger_queue_for_small_render_geometry",
                    test_update_queue_depth_keeps_larger_queue_for_small_render_geometry);
    g_test_add_func("/video_player/rewind_reset/clears_timing_and_queue_state",
                    test_rewind_reset_clears_timing_and_queue_state);
    g_test_add_func("/video_player/debug_logging/honors_environment_toggle",
                    test_debug_logging_honors_environment_toggle);
    g_test_add_func("/video_player/debug_logging/closes_stream_when_last_player_is_destroyed",
                    test_debug_logging_closes_stream_when_last_player_is_destroyed);
    g_test_add_func("/video_player/queue_take_for_playback/waits_even_when_future_queue_is_full",
                    test_queue_take_for_playback_waits_even_when_future_queue_is_full);
    g_test_add_func("/video_player/queue_take_for_playback/waits_when_future_queue_is_not_full",
                    test_queue_take_for_playback_waits_when_future_queue_is_not_full);
    g_test_add_func("/video_player/calc_delay/uses_queue_head_even_with_high_io_avg",
                    test_calc_delay_uses_queue_head_even_with_high_io_avg);
    g_test_add_func("/video_player/calc_delay/retries_quickly_when_playing_and_queue_is_empty",
                    test_calc_delay_retries_quickly_when_playing_and_queue_is_empty);
}
