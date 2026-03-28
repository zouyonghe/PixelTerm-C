#include <glib.h>
#include <glib/gstdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <unistd.h>

#include "video_player_clock_internal.h"
#include "video_player_test_internal.h"

typedef RenderedFrame VideoFrame;

static gsize test_video_player_sync_once = 0;

static GMutex decode_queue_push_mutex;
static GCond decode_queue_push_cond;
static gboolean decode_queue_push_blocking = FALSE;

static GMutex queue_push_mutex;
static GCond queue_push_cond;
static gboolean queue_push_blocking = FALSE;
static GMutex decode_queue_item_mutex;
static GCond decode_queue_item_cond;
static gboolean decode_queue_item_received = FALSE;
static gint seek_hook_call_count = 0;
static gint seek_hook_result = 0;
static gint64 seek_hook_last_timestamp = G_MININT64;
static gint seek_hook_last_flags = 0;
static gint seek_preview_hook_call_count = 0;
static gint64 seek_preview_hook_target_ms = -1;
static gint seek_preview_hook_return_count = 0;
static gboolean seek_preview_hook_return_value = TRUE;
static gboolean seek_preview_hook_enqueue_frame = FALSE;
static gint64 seek_preview_hook_frame_pts_offset_ms = 0;

static const gchar *k_seek_preview_video_fixture_base64 =
    "AAAAIGZ0eXBpc29tAAACAGlzb21pc28yYXZjMW1wNDEAAAAIZnJlZQAAAuxtZGF0AAACUQYF//9N"
    "3EXpvebZSLeWLNgg2SPu73gyNjQgLSBjb3JlIDE2NSByMzIyMiBiMzU2MDVhIC0gSC4yNjQvTVBF"
    "Ry00IEFWQyBjb2RlYyAtIENvcHlsZWZ0IDIwMDMtMjAyNSAtIGh0dHA6Ly93d3cudmlkZW9sYW4u"
    "b3JnL3gyNjQuaHRtbCAtIG9wdGlvbnM6IGNhYmFjPTAgcmVmPTEgZGVibG9jaz0wOjA6MCBhbmFs"
    "eXNlPTA6MCBtZT1kaWEgc3VibWU9MCBwc3k9MSBwc3lfcmQ9MS4wMDowLjAwIG1peGVkX3JlZj0w"
    "IG1lX3JhbmdlPTE2IGNocm9tYV9tZT0xIHRyZWxsaXM9MCA4eDhkY3Q9MCBjcW09MCBkZWFkem9u"
    "ZT0yMSwxMSBmYXN0X3Bza2lwPTEgY2hyb21hX3FwX29mZnNldD0wIHRocmVhZHM9MSBsb29rYWhl"
    "YWRfdGhyZWFkcz0xIHNsaWNlZF90aHJlYWRzPTAgbnI9MCBkZWNpbWF0ZT0xIGludGVybGFjZWQ9"
    "MCBibHVyYXlfY29tcGF0PTAgY29uc3RyYWluZWRfaW50cmE9MCBiZnJhbWVzPTAgd2VpZ2h0cD0w"
    "IGtleWludD0xIGtleWludF9taW49MSBzY2VuZWN1dD0wIGludHJhX3JlZnJlc2g9MCByYz1jcmYg"
    "bWJ0cmVlPTAgY3JmPTIzLjAgcWNvbXA9MC42MCBxcG1pbj0wIHFwbWF4PTY5IHFwc3RlcD00IGlw"
    "X3JhdGlvPTEuNDAgYXE9MACAAAAAE2WIhDoRigACGPHAAED2OAAIeWAAAAAUZYiCAToRigACt/HA"
    "AEjmOAALa2AAAAAUZYiEBOhGKAAK38cAASOY4AAtrYAAAAAUZYiCAToRigACt/HAAEjmOAALa2AA"
    "AAAUZYiEBOhGKAAK38cAASOY4AAtrYAAAAAUZYiCAToRigACt/HAAEjmOAALa2AAAAMnbW9vdgAA"
    "AGxtdmhkAAAAAAAAAAAAAAAAAAAD6AAAF3AAAQAAAQAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAA"
    "AAABAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgAAAlF0cmFr"
    "AAAAXHRraGQAAAADAAAAAAAAAAAAAAABAAAAAAAAF3AAAAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAA"
    "AAAAAAAAAAABAAAAAAAAAAAAAAAAAABAAAAAAAIAAAACAAAAAAAkZWR0cwAAABxlbHN0AAAAAAAA"
    "AAEAABdwAAAAAAABAAAAAAHJbWRpYQAAACBtZGhkAAAAAAAAAAAAAAAAAABAAAABgABVxAAAAAAA"
    "LWhkbHIAAAAAAAAAAHZpZGUAAAAAAAAAAAAAAABWaWRlb0hhbmRsZXIAAAABdG1pbmYAAAAUdm1o"
    "ZAAAAAEAAAAAAAAAAAAAACRkaW5mAAAAHGRyZWYAAAAAAAAAAQAAAAx1cmwgAAAAAQAAATRzdGJs"
    "AAAAuHN0c2QAAAAAAAAAAQAAAKhhdmMxAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAAIAAgBIAAAA"
    "SAAAAAAAAAABFUxhdmM2Mi4yOC4xMDAgbGlieDI2NAAAAAAAAAAAAAAAGP//AAAALmF2Y0MBQsAK"
    "/+EAF2dCwArd+IiMBEAAAAMAQAAAAwCDxIngAQAEaM4PyAAAABBwYXNwAAAAAQAAAAEAAAAUYnRy"
    "dAAAAAAAAAPaAAAAAAAAABhzdHRzAAAAAAAAAAEAAAAGAABAAAAAABxzdHNjAAAAAAAAAAEAAAAB"
    "AAAABgAAAAEAAAAsc3RzegAAAAAAAAAAAAAABgAAAmwAAAAYAAAAGAAAABgAAAAYAAAAGAAAABRz"
    "dGNvAAAAAAAAAAEAAAAwAAAAYnVkdGEAAABabWV0YQAAAAAAAAAhaGRscgAAAAAAAAAAbWRpcmFw"
    "cGwAAAAAAAAAAAAAAAAtaWxzdAAAACWpdG9vAAAAHWRhdGEAAAABAAAAAExhdmY2Mi4xMi4xMDA=";

typedef struct {
    GMutex *mutex;
    GCond *cond;
    gboolean *flag;
} QueueWaitSignal;

typedef struct {
    VideoPlayer *player;
    gint64 pts_ms;
    guint generation;
    GMutex mutex;
    GCond cond;
    gboolean completed;
} QueueInsertCall;

typedef struct {
    VideoPlayer *player;
    gint64 pts_ms;
    GMutex mutex;
    GCond cond;
    gboolean started;
} FallbackSetCall;

typedef struct {
    VideoPlayer *player;
    gint64 raw_pts_ms;
    gint frame_delay;
    gint64 resolved_raw_pts_ms;
    gint64 next_fallback_pts_ms;
    GMutex mutex;
    GCond cond;
    gboolean started;
} FallbackAdvanceCall;

typedef struct {
    VideoPlayer *player;
    GMutex mutex;
    GCond cond;
    gboolean started;
} ParkedWorkerCall;

static VideoFrame *make_test_frame(gint64 pts_ms);
static VideoFrame *make_test_frame_with_generation(gint64 pts_ms, guint generation);

static void remove_path(gpointer data) {
    if (!data) {
        return;
    }
    g_remove((const gchar *)data);
    g_free(data);
}

static gchar *write_seek_preview_video_fixture(void) {
    gsize video_len = 0;
    guchar *video_bytes = g_base64_decode(k_seek_preview_video_fixture_base64, &video_len);
    g_assert_nonnull(video_bytes);
    g_assert_cmpuint(video_len, >, 0);

    gchar *path = NULL;
    gint fd = g_file_open_tmp("pixelterm-seek-preview-XXXXXX.mp4", &path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);

    g_assert_true(g_file_set_contents(path, (const gchar *)video_bytes, (gssize)video_len, NULL));
    g_free(video_bytes);

    g_test_queue_destroy(remove_path, path);
    return path;
}

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
        g_mutex_init(&decode_queue_item_mutex);
        g_cond_init(&decode_queue_item_cond);
        g_once_init_leave(&test_video_player_sync_once, 1);
    }
}

static int test_seek_hook(AVFormatContext *format_context, int stream_index, int64_t timestamp, int flags) {
    (void)format_context;
    (void)stream_index;
    seek_hook_call_count++;
    seek_hook_last_timestamp = timestamp;
    seek_hook_last_flags = flags;
    return seek_hook_result;
}

static gboolean test_seek_preview_hook(VideoPlayer *player, gint64 target_ms) {
    seek_preview_hook_call_count++;
    seek_preview_hook_target_ms = target_ms;
    seek_preview_hook_return_count++;
    if (player && seek_preview_hook_enqueue_frame) {
        guint generation = (guint)g_atomic_int_get(&player->playback_generation);
        if (generation == 0) {
            generation = 1;
        }
        video_player_queue_push(player,
                                make_test_frame_with_generation(target_ms + seek_preview_hook_frame_pts_offset_ms,
                                                                generation));
    }
    return seek_preview_hook_return_value;
}

static void queue_wait_hook_signal(void *user_data) {
    QueueWaitSignal *signal = (QueueWaitSignal *)user_data;
    if (!signal) {
        return;
    }

    g_mutex_lock(signal->mutex);
    *(signal->flag) = TRUE;
    g_cond_broadcast(signal->cond);
    g_mutex_unlock(signal->mutex);
}

static gboolean timeout_source_noop(gpointer user_data) {
    (void)user_data;
    return G_SOURCE_CONTINUE;
}

static gpointer queue_insert_thread_main(gpointer user_data) {
    QueueInsertCall *call = (QueueInsertCall *)user_data;
    if (!call) {
        return NULL;
    }

    guint generation = call->generation;
    if (generation == 0 && call->player) {
        generation = (guint)g_atomic_int_get(&call->player->playback_generation);
    }
    if (generation == 0) {
        generation = 1;
    }

    video_player_queue_insert_sorted(call->player, make_test_frame_with_generation(call->pts_ms, generation));

    g_mutex_lock(&call->mutex);
    call->completed = TRUE;
    g_cond_broadcast(&call->cond);
    g_mutex_unlock(&call->mutex);
    return NULL;
}

static gpointer queue_push_call_thread_main(gpointer user_data) {
    QueueInsertCall *call = (QueueInsertCall *)user_data;
    if (!call) {
        return NULL;
    }

    guint generation = call->generation;
    if (generation == 0 && call->player) {
        generation = (guint)g_atomic_int_get(&call->player->playback_generation);
    }
    if (generation == 0) {
        generation = 1;
    }

    video_player_queue_push(call->player, make_test_frame_with_generation(call->pts_ms, generation));

    g_mutex_lock(&call->mutex);
    call->completed = TRUE;
    g_cond_broadcast(&call->cond);
    g_mutex_unlock(&call->mutex);
    return NULL;
}

static gpointer fallback_set_thread_main(gpointer user_data) {
    FallbackSetCall *call = (FallbackSetCall *)user_data;
    if (!call) {
        return NULL;
    }

    g_mutex_lock(&call->mutex);
    call->started = TRUE;
    g_cond_broadcast(&call->cond);
    g_mutex_unlock(&call->mutex);

    video_player_set_fallback_pts_ms(call->player, call->pts_ms);
    return NULL;
}

static gpointer fallback_advance_thread_main(gpointer user_data) {
    FallbackAdvanceCall *call = (FallbackAdvanceCall *)user_data;
    if (!call) {
        return NULL;
    }

    g_mutex_lock(&call->mutex);
    call->started = TRUE;
    g_cond_broadcast(&call->cond);
    g_mutex_unlock(&call->mutex);

    call->resolved_raw_pts_ms = video_player_resolve_and_advance_fallback_pts_ms(call->player,
                                                                                  call->raw_pts_ms,
                                                                                  call->frame_delay,
                                                                                  &call->next_fallback_pts_ms);
    return NULL;
}

static gpointer parked_worker_thread_main(gpointer user_data) {
    ParkedWorkerCall *call = (ParkedWorkerCall *)user_data;
    if (!call || !call->player) {
        return NULL;
    }

    g_mutex_lock(&call->mutex);
    call->started = TRUE;
    g_cond_broadcast(&call->cond);
    g_mutex_unlock(&call->mutex);

    g_mutex_lock(&call->player->queue_mutex);
    while (!call->player->worker_stop) {
        g_cond_wait(&call->player->frame_queue_has_space, &call->player->queue_mutex);
    }
    g_mutex_unlock(&call->player->queue_mutex);
    return NULL;
}

static GThread *start_parked_worker_for_test(const gchar *name, ParkedWorkerCall *call, VideoPlayer *player) {
    if (!call || !player) {
        return NULL;
    }

    call->player = player;
    call->started = FALSE;
    g_mutex_init(&call->mutex);
    g_cond_init(&call->cond);

    GThread *thread = g_thread_new(name, parked_worker_thread_main, call);
    wait_for_flag_or_fail(&call->mutex, &call->cond, &call->started, "Timed out waiting for parked worker thread to start");
    return thread;
}

static void clear_parked_worker_for_test(ParkedWorkerCall *call) {
    if (!call) {
        return;
    }

    g_cond_clear(&call->cond);
    g_mutex_clear(&call->mutex);
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

static gboolean init_minimal_seek_context(VideoPlayer *player) {
    if (!player) {
        return FALSE;
    }

    AVFormatContext *format_context = avformat_alloc_context();
    if (!format_context) {
        return FALSE;
    }

    AVStream *stream = avformat_new_stream(format_context, NULL);
    if (!stream) {
        avformat_free_context(format_context);
        return FALSE;
    }
    stream->time_base = (AVRational){1, 1000};

    AVCodecContext *codec_context = avcodec_alloc_context3(NULL);
    if (!codec_context) {
        avformat_free_context(format_context);
        return FALSE;
    }

    player->format_context = format_context;
    player->codec_context = codec_context;
    player->video_stream_index = stream->index;
    player->has_video = TRUE;
    return TRUE;
}

static void teardown_minimal_seek_context(VideoPlayer *player) {
    if (!player) {
        return;
    }
    if (player->codec_context) {
        avcodec_free_context(&player->codec_context);
    }
    if (player->format_context) {
        avformat_free_context(player->format_context);
        player->format_context = NULL;
    }
    player->video_stream_index = -1;
    player->has_video = FALSE;
}

static void test_teardown_minimal_seek_context_frees_manual_format_context(void) {
    if (g_test_subprocess()) {
        VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
        g_assert_nonnull(player);
        g_assert_true(init_minimal_seek_context(player));

        teardown_minimal_seek_context(player);

        g_assert_null(player->codec_context);
        g_assert_null(player->format_context);
        video_player_destroy(player);
        return;
    }

    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_passed();
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

    video_player_decode_queue_push(player, frame);
    return NULL;
}

static gpointer decode_queue_wait_thread_main(gpointer user_data) {
    VideoPlayer *player = (VideoPlayer *)user_data;
    DecodedFrame *frame = video_player_decode_queue_wait_and_take(player);
    if (frame) {
        decoded_frame_destroy(frame);
        video_player_render_work_finished_for_test(player);
    }

    g_mutex_lock(&decode_queue_item_mutex);
    decode_queue_item_received = TRUE;
    g_cond_broadcast(&decode_queue_item_cond);
    g_mutex_unlock(&decode_queue_item_mutex);
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

    QueueWaitSignal signal = {
        .mutex = &decode_queue_push_mutex,
        .cond = &decode_queue_push_cond,
        .flag = &decode_queue_push_blocking,
    };
    g_mutex_lock(&decode_queue_push_mutex);
    decode_queue_push_blocking = FALSE;
    g_mutex_unlock(&decode_queue_push_mutex);
    video_player_set_queue_wait_hook_for_test(player,
                                              VIDEO_PLAYER_TEST_QUEUE_DECODE,
                                              queue_wait_hook_signal,
                                              &signal);

    GThread *thread = g_thread_new("decode-queue-push-test", decode_queue_push_thread_main, player);
    wait_for_flag_or_fail(&decode_queue_push_mutex,
                          &decode_queue_push_cond,
                          &decode_queue_push_blocking,
                          "Timed out waiting for decode queue push thread to block on full decode queue");

    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 4);
    g_assert_cmpint(decode_queue_head_pts_for_test(player), ==, 100);

    DecodedFrame *oldest = video_player_decode_queue_take(player);
    g_assert_nonnull(oldest);
    g_assert_cmpint(oldest->pts_ms, ==, 100);
    decoded_frame_destroy(oldest);

    g_thread_join(thread);
    video_player_set_queue_wait_hook_for_test(NULL,
                                              VIDEO_PLAYER_TEST_QUEUE_DECODE,
                                              NULL,
                                              NULL);

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

    QueueWaitSignal signal = {
        .mutex = &decode_queue_push_mutex,
        .cond = &decode_queue_push_cond,
        .flag = &decode_queue_push_blocking,
    };
    g_mutex_lock(&decode_queue_push_mutex);
    decode_queue_push_blocking = FALSE;
    g_mutex_unlock(&decode_queue_push_mutex);
    video_player_set_queue_wait_hook_for_test(player,
                                              VIDEO_PLAYER_TEST_QUEUE_DECODE,
                                              queue_wait_hook_signal,
                                              &signal);

    GThread *thread = g_thread_new("decode-queue-push-test", decode_queue_push_thread_main, player);
    wait_for_flag_or_fail(&decode_queue_push_mutex,
                          &decode_queue_push_cond,
                          &decode_queue_push_blocking,
                          "Timed out waiting for decode queue push thread to block on full decode queue");

    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 4);
    g_assert_cmpint(decode_queue_head_pts_for_test(player), ==, 100);

    DecodedFrame *oldest = video_player_decode_queue_take(player);
    g_assert_nonnull(oldest);
    g_assert_cmpint(oldest->pts_ms, ==, 100);
    decoded_frame_destroy(oldest);

    g_thread_join(thread);
    video_player_set_queue_wait_hook_for_test(NULL,
                                              VIDEO_PLAYER_TEST_QUEUE_DECODE,
                                              NULL,
                                              NULL);

    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 4);
    g_assert_cmpint(decode_queue_head_pts_for_test(player), ==, 133);

    video_player_destroy(player);
}

static void test_decode_queue_wait_and_take_blocks_until_item_arrives(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    init_test_sync_primitives();
    g_mutex_lock(&decode_queue_item_mutex);
    decode_queue_item_received = FALSE;
    g_mutex_unlock(&decode_queue_item_mutex);

    GThread *thread = g_thread_new("decode-queue-wait-test", decode_queue_wait_thread_main, player);
    g_usleep(10 * 1000);

    g_mutex_lock(&decode_queue_item_mutex);
    g_assert_false(decode_queue_item_received);
    g_mutex_unlock(&decode_queue_item_mutex);

    DecodedFrame *frame = g_new0(DecodedFrame, 1);
    decoded_frame_fill(frame, 233, 1);
    video_player_decode_queue_push(player, frame);

    wait_for_flag_or_fail(&decode_queue_item_mutex,
                          &decode_queue_item_cond,
                          &decode_queue_item_received,
                          "Timed out waiting for blocking decode take to receive an item");

    g_thread_join(thread);
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

static void test_set_fallback_pts_waits_on_state_mutex(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    player->fallback_pts_ms = 1111;

    FallbackSetCall call = {
        .player = player,
        .pts_ms = 2222,
    };
    g_mutex_init(&call.mutex);
    g_cond_init(&call.cond);

    g_mutex_lock(&player->state_mutex);
    GThread *thread = g_thread_new("fallback-set-test", fallback_set_thread_main, &call);
    wait_for_flag_or_fail(&call.mutex,
                          &call.cond,
                          &call.started,
                          "Timed out waiting for fallback setter thread to start");
    g_usleep(10 * 1000);
    g_assert_cmpint(player->fallback_pts_ms, ==, 1111);
    g_mutex_unlock(&player->state_mutex);

    g_thread_join(thread);

    g_assert_cmpint(player->fallback_pts_ms, ==, 2222);

    g_cond_clear(&call.cond);
    g_mutex_clear(&call.mutex);
    video_player_destroy(player);
}

static void test_resolve_and_advance_fallback_pts_waits_on_state_mutex(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    player->fallback_pts_ms = 7000;

    FallbackAdvanceCall call = {
        .player = player,
        .raw_pts_ms = G_MININT64,
        .frame_delay = 33,
        .resolved_raw_pts_ms = G_MININT64,
        .next_fallback_pts_ms = G_MININT64,
    };
    g_mutex_init(&call.mutex);
    g_cond_init(&call.cond);

    g_mutex_lock(&player->state_mutex);
    GThread *thread = g_thread_new("fallback-advance-test", fallback_advance_thread_main, &call);
    wait_for_flag_or_fail(&call.mutex,
                          &call.cond,
                          &call.started,
                          "Timed out waiting for fallback advance thread to start");
    g_usleep(10 * 1000);
    g_assert_cmpint(player->fallback_pts_ms, ==, 7000);
    g_mutex_unlock(&player->state_mutex);

    g_thread_join(thread);

    g_assert_cmpint(call.resolved_raw_pts_ms, ==, 7000);
    g_assert_cmpint(call.next_fallback_pts_ms, ==, 7033);
    g_assert_cmpint(player->fallback_pts_ms, ==, 7033);

    g_cond_clear(&call.cond);
    g_mutex_clear(&call.mutex);
    video_player_destroy(player);
}

static void test_current_position_prefers_last_presented_pts(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_us = g_get_monotonic_time() - 3000 * 1000;
    player->clock_start_pts_ms = 1000;
    player->last_presented_pts_ms = 4200;
    g_mutex_unlock(&player->state_mutex);

    g_assert_cmpint(video_player_current_position_ms_for_test(player), ==, 4200);

    video_player_destroy(player);
}

static void test_current_position_uses_clock_when_no_presented_pts(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    gint64 now_us = g_get_monotonic_time();
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_us = now_us - 2500 * 1000;
    player->clock_start_pts_ms = 1000;
    player->last_presented_pts_ms = G_MININT64;
    player->is_playing = TRUE;
    g_mutex_unlock(&player->state_mutex);

    gint64 position_ms = video_player_current_position_ms_for_test(player);
    gint64 now_us_after = g_get_monotonic_time();
    gint64 elapsed_ms = (now_us_after - now_us) / 1000;
    gint64 expected_ms = 3500 + elapsed_ms;
    gint64 diff_ms = ABS(position_ms - expected_ms);
    g_assert_cmpint(diff_ms, <=, 200);

    video_player_destroy(player);
}

static void test_current_position_uses_fallback_pts_when_clock_not_started(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    player->fallback_pts_ms = 6500;
    g_mutex_lock(&player->state_mutex);
    player->clock_started = FALSE;
    player->clock_start_us = 0;
    player->clock_start_pts_ms = 0;
    player->last_presented_pts_ms = G_MININT64;
    player->is_playing = TRUE;
    g_mutex_unlock(&player->state_mutex);

    g_assert_cmpint(video_player_current_position_ms_for_test(player), ==, 6500);

    video_player_destroy(player);
}

static void test_current_position_clamps_negative_fallback_pts_to_zero(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    player->fallback_pts_ms = -250;
    g_mutex_lock(&player->state_mutex);
    player->clock_started = FALSE;
    player->clock_start_us = 0;
    player->clock_start_pts_ms = 0;
    player->last_presented_pts_ms = G_MININT64;
    player->is_playing = TRUE;
    g_mutex_unlock(&player->state_mutex);

    g_assert_cmpint(video_player_current_position_ms(player), ==, 0);

    video_player_destroy(player);
}

static void test_seek_target_clamps_to_zero_and_duration(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    g_mutex_lock(&player->state_mutex);
    player->last_presented_pts_ms = 3000;
    g_mutex_unlock(&player->state_mutex);

    g_assert_cmpint(video_player_seek_target_ms_for_test(player, -5000, 12000), ==, 0);
    g_assert_cmpint(video_player_seek_target_ms_for_test(player, 5000, 6000), ==, 6000);

    video_player_destroy(player);
}

static void test_seek_relative_zero_delta_is_noop(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }
    if (!init_minimal_seek_context(player)) {
        video_player_destroy(player);
        g_test_skip("ffmpeg seek test context unavailable");
        return;
    }

    player->fallback_pts_ms = 1234;
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_pts_ms = 1234;
    player->last_presented_pts_ms = 1234;
    g_mutex_unlock(&player->state_mutex);
    guint generation_before = player->playback_generation;
    seek_hook_call_count = 0;
    seek_hook_result = 0;
    video_player_set_seek_hook_for_test(test_seek_hook);

    g_assert_cmpint(video_player_seek_relative_ms(player, 0), ==, ERROR_NONE);
    g_assert_cmpint(seek_hook_call_count, ==, 0);
    g_assert_cmpuint(player->playback_generation, ==, generation_before);
    g_assert_cmpint(player->fallback_pts_ms, ==, 1234);
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);

    video_player_set_seek_hook_for_test(NULL);
    teardown_minimal_seek_context(player);
    video_player_destroy(player);
}

static void test_seek_relative_failed_seek_preserves_state(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }
    if (!init_minimal_seek_context(player)) {
        video_player_destroy(player);
        g_test_skip("ffmpeg seek test context unavailable");
        return;
    }

    player->fallback_pts_ms = 2222;
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_pts_ms = 2222;
    player->last_presented_pts_ms = 2222;
    g_mutex_unlock(&player->state_mutex);

    video_player_queue_push(player, make_test_frame(2200));
    DecodedFrame *decoded = g_new0(DecodedFrame, 1);
    decoded_frame_fill(decoded, 2200, 1);
    video_player_decode_queue_push(player, decoded);

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 1);
    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 1);
    seek_hook_call_count = 0;
    seek_hook_result = -1;
    video_player_set_seek_hook_for_test(test_seek_hook);

    g_assert_cmpint(video_player_seek_relative_ms(player, 1000), ==, ERROR_INVALID_IMAGE);
    g_assert_cmpint(seek_hook_call_count, ==, 1);
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 1);
    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 1);
    g_assert_cmpint(player->fallback_pts_ms, ==, 2222);
    g_assert_true(player->clock_started);
    g_assert_cmpint(player->clock_start_pts_ms, ==, 2222);

    video_player_set_seek_hook_for_test(NULL);
    teardown_minimal_seek_context(player);
    video_player_destroy(player);
}

static void test_seek_relative_paused_seek_refreshes_preview(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }
    if (!init_minimal_seek_context(player)) {
        video_player_destroy(player);
        g_test_skip("ffmpeg seek test context unavailable");
        return;
    }

    player->draining = TRUE;
    player->fallback_pts_ms = 4000;
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_pts_ms = 4000;
    player->last_presented_pts_ms = 4000;
    g_mutex_unlock(&player->state_mutex);

    seek_hook_call_count = 0;
    seek_hook_result = 0;
    seek_preview_hook_call_count = 0;
    seek_preview_hook_target_ms = -1;
    seek_preview_hook_return_count = 0;
    seek_preview_hook_return_value = TRUE;
    video_player_set_seek_hook_for_test(test_seek_hook);
    video_player_set_seek_preview_hook_for_test(test_seek_preview_hook);

    g_assert_cmpint(video_player_seek_relative_ms(player, 1000), ==, ERROR_NONE);
    g_assert_cmpint(seek_hook_call_count, ==, 1);
    g_assert_cmpint(seek_preview_hook_call_count, ==, 1);
    g_assert_cmpint(seek_preview_hook_target_ms, ==, 5000);
    g_assert_false(player->draining);
    g_assert_false(player->is_playing);
    g_assert_cmpuint(player->timer_id, ==, 0);

    video_player_set_seek_preview_hook_for_test(NULL);
    video_player_set_seek_hook_for_test(NULL);
    teardown_minimal_seek_context(player);
    video_player_destroy(player);
}

static void test_seek_relative_paused_repeated_seeks_preserve_latest_target(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }
    if (!init_minimal_seek_context(player)) {
        video_player_destroy(player);
        g_test_skip("ffmpeg seek test context unavailable");
        return;
    }

    player->fallback_pts_ms = 4000;
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_pts_ms = 4000;
    player->last_presented_pts_ms = 4000;
    g_mutex_unlock(&player->state_mutex);

    seek_hook_call_count = 0;
    seek_hook_result = 0;
    seek_hook_last_timestamp = G_MININT64;
    seek_hook_last_flags = 0;
    seek_preview_hook_call_count = 0;
    seek_preview_hook_target_ms = -1;
    seek_preview_hook_return_count = 0;
    seek_preview_hook_return_value = TRUE;
    seek_preview_hook_enqueue_frame = TRUE;
    seek_preview_hook_frame_pts_offset_ms = -500;
    video_player_set_seek_hook_for_test(test_seek_hook);
    video_player_set_seek_preview_hook_for_test(test_seek_preview_hook);

    g_assert_cmpint(video_player_seek_relative_ms(player, 1000), ==, ERROR_NONE);
    g_assert_cmpint(seek_hook_call_count, ==, 1);
    g_assert_cmpint(seek_hook_last_timestamp, ==, 5000);
    g_assert_cmpint(seek_preview_hook_target_ms, ==, 5000);

    g_assert_cmpint(video_player_seek_relative_ms(player, 1000), ==, ERROR_NONE);
    g_assert_cmpint(seek_hook_call_count, ==, 2);
    g_assert_cmpint(seek_hook_last_timestamp, ==, 6000);
    g_assert_cmpint(seek_preview_hook_target_ms, ==, 6000);
    g_assert_cmpint(seek_hook_last_flags, ==, 0);

    seek_preview_hook_enqueue_frame = FALSE;
    seek_preview_hook_frame_pts_offset_ms = 0;
    video_player_set_seek_preview_hook_for_test(NULL);
    video_player_set_seek_hook_for_test(NULL);
    teardown_minimal_seek_context(player);
    video_player_destroy(player);
}

static void test_seek_relative_preview_bails_after_decode_attempt_limit(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }
    if (!init_minimal_seek_context(player)) {
        video_player_destroy(player);
        g_test_skip("ffmpeg seek test context unavailable");
        return;
    }

    player->fallback_pts_ms = 7000;
    seek_hook_call_count = 0;
    seek_hook_result = 0;
    seek_preview_hook_call_count = 0;
    seek_preview_hook_target_ms = -1;
    seek_preview_hook_return_count = 0;
    seek_preview_hook_return_value = TRUE;
    video_player_set_seek_hook_for_test(test_seek_hook);
    video_player_set_seek_preview_hook_for_test(NULL);
    video_player_set_max_preview_decode_attempts_for_test(0);

    g_assert_cmpint(video_player_seek_relative_ms(player, 1000), ==, ERROR_NONE);
    g_assert_cmpint(seek_hook_call_count, ==, 1);
    g_assert_cmpint(seek_preview_hook_call_count, ==, 0);
    g_assert_cmpint(seek_preview_hook_return_count, ==, 0);
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);

    video_player_set_max_preview_decode_attempts_for_test(-1);
    video_player_set_seek_hook_for_test(NULL);
    teardown_minimal_seek_context(player);
    video_player_destroy(player);
}

static void test_seek_preview_default_attempt_limit_is_bounded(void) {
    gint original = video_player_get_max_preview_decode_attempts_for_test();
    video_player_set_max_preview_decode_attempts_for_test(-1);

    g_assert_cmpint(video_player_get_max_preview_decode_attempts_for_test(), >, 0);

    video_player_set_max_preview_decode_attempts_for_test(original);
}

static void test_render_seek_preview_uses_hook_before_decoder_setup(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    seek_preview_hook_call_count = 0;
    seek_preview_hook_target_ms = -1;
    seek_preview_hook_return_count = 0;
    seek_preview_hook_return_value = TRUE;
    video_player_set_seek_preview_hook_for_test(test_seek_preview_hook);

    g_assert_true(video_player_render_seek_preview(player, 4321));
    g_assert_cmpint(seek_preview_hook_call_count, ==, 1);
    g_assert_cmpint(seek_preview_hook_target_ms, ==, 4321);
    g_assert_cmpint(seek_preview_hook_return_count, ==, 1);

    video_player_set_seek_preview_hook_for_test(NULL);
    video_player_destroy(player);
}

static void test_seek_relative_resets_visual_state_under_state_mutex(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }
    if (!init_minimal_seek_context(player)) {
        video_player_destroy(player);
        g_test_skip("ffmpeg seek test context unavailable");
        return;
    }

    player->fallback_pts_ms = 4000;
    g_mutex_lock(&player->state_mutex);
    player->clock_started = TRUE;
    player->clock_start_pts_ms = 4000;
    player->last_presented_pts_ms = 4000;
    player->fixed_frame_valid = TRUE;
    player->last_frame_top_row = 12;
    player->last_frame_height = 8;
    g_mutex_unlock(&player->state_mutex);

    seek_hook_call_count = 0;
    seek_hook_result = 0;
    seek_preview_hook_call_count = 0;
    seek_preview_hook_target_ms = -1;
    seek_preview_hook_return_count = 0;
    seek_preview_hook_return_value = TRUE;
    video_player_set_seek_hook_for_test(test_seek_hook);
    video_player_set_seek_preview_hook_for_test(test_seek_preview_hook);

    g_assert_cmpint(video_player_seek_relative_ms(player, 1000), ==, ERROR_NONE);
    g_mutex_lock(&player->state_mutex);
    g_assert_cmpint(player->fallback_pts_ms, ==, 5000);
    g_assert_true(player->rewind_needs_resync);
    g_assert_false(player->fixed_frame_valid);
    g_assert_cmpint(player->last_frame_top_row, ==, 0);
    g_assert_cmpint(player->last_frame_height, ==, 0);
    g_mutex_unlock(&player->state_mutex);

    video_player_set_seek_preview_hook_for_test(NULL);
    video_player_set_seek_hook_for_test(NULL);
    teardown_minimal_seek_context(player);
    video_player_destroy(player);
}

static void test_play_after_eof_rewinds_explicitly_to_start(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }
    if (!init_minimal_seek_context(player)) {
        video_player_destroy(player);
        g_test_skip("ffmpeg seek test context unavailable");
        return;
    }

    ParkedWorkerCall decode_call = {0};
    ParkedWorkerCall render_a_call = {0};
    ParkedWorkerCall render_b_call = {0};

    g_mutex_lock(&player->state_mutex);
    player->last_presented_pts_ms = 16533;
    g_mutex_unlock(&player->state_mutex);
    video_player_handle_eof_for_test(player);
    g_assert_true(player->eof_ended);

    player->worker_thread = start_parked_worker_for_test("parked-decode-play-test", &decode_call, player);
    player->render_workers[0] = start_parked_worker_for_test("parked-render-a-play-test", &render_a_call, player);
    player->render_workers[1] = start_parked_worker_for_test("parked-render-b-play-test", &render_b_call, player);
    player->render_workers_started = TRUE;
    video_player_set_renderer(player, NULL);

    seek_hook_call_count = 0;
    seek_hook_result = 0;
    seek_hook_last_timestamp = G_MININT64;
    seek_hook_last_flags = 0;
    video_player_set_seek_hook_for_test(test_seek_hook);

    g_assert_cmpint(video_player_play(player), ==, ERROR_NONE);
    g_assert_cmpint(seek_hook_call_count, ==, 1);
    g_assert_cmpint(seek_hook_last_timestamp, ==, 0);
    g_assert_cmpint(seek_hook_last_flags, ==, AVSEEK_FLAG_BACKWARD);
    g_assert_false(player->eof_ended);
    g_assert_cmpint(player->fallback_pts_ms, ==, 0);
    g_assert_null(player->worker_thread);
    g_assert_false(player->render_workers_started);

    video_player_set_seek_hook_for_test(NULL);
    clear_parked_worker_for_test(&render_b_call);
    clear_parked_worker_for_test(&render_a_call);
    clear_parked_worker_for_test(&decode_call);
    teardown_minimal_seek_context(player);
    video_player_destroy(player);
}

static void test_seek_relative_after_eof_stops_parked_workers_before_preview(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    gchar *fixture_path = write_seek_preview_video_fixture();
    g_assert_nonnull(fixture_path);
    g_assert_cmpint(video_player_load(player, fixture_path), ==, ERROR_NONE);

    ParkedWorkerCall decode_call = {0};
    ParkedWorkerCall render_a_call = {0};
    ParkedWorkerCall render_b_call = {0};

    player->fallback_pts_ms = 4000;
    g_mutex_lock(&player->state_mutex);
    player->last_presented_pts_ms = 4000;
    player->last_present_us = 0;
    g_mutex_unlock(&player->state_mutex);
    video_player_handle_eof_for_test(player);
    g_assert_true(player->eof_ended);

    player->worker_thread = start_parked_worker_for_test("parked-decode-seek-test", &decode_call, player);
    player->render_workers[0] = start_parked_worker_for_test("parked-render-a-seek-test", &render_a_call, player);
    player->render_workers[1] = start_parked_worker_for_test("parked-render-b-seek-test", &render_b_call, player);
    player->render_workers_started = TRUE;

    g_assert_cmpint(video_player_seek_relative_ms(player, 1000), ==, ERROR_NONE);
    g_assert_null(player->worker_thread);
    g_assert_false(player->render_workers_started);
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);
    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 0);
    g_mutex_lock(&player->state_mutex);
    g_assert_false(player->eof_ended);
    g_assert_cmpint(player->fallback_pts_ms, ==, 5000);
    g_assert_cmpint(player->last_present_us, >, 0);
    g_assert_true(player->rewind_needs_resync);
    g_mutex_unlock(&player->state_mutex);
    g_assert_false(player->is_playing);
    g_assert_cmpuint(player->timer_id, ==, 0);

    clear_parked_worker_for_test(&render_b_call);
    clear_parked_worker_for_test(&render_a_call);
    clear_parked_worker_for_test(&decode_call);
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
    queued->generation = (guint)g_atomic_int_get(&player->playback_generation);
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
    gint64 clock_start_us = player->clock_start_us;
    gint64 clock_start_pts_ms = player->clock_start_pts_ms;
    g_mutex_unlock(&player->state_mutex);

    video_player_queue_push(player, make_test_frame(120));
    video_player_queue_push(player, make_test_frame(153));

    gint64 queue_head_pts_ms = video_player_queue_head_pts_for_test(player);
    gint64 before_call_us = g_get_monotonic_time();
    gint delay = video_player_calc_delay_ms(player);
    gint64 after_call_us = g_get_monotonic_time();

    gint64 target_before_ms = clock_start_pts_ms + ((before_call_us - clock_start_us) / 1000);
    gint64 target_after_ms = clock_start_pts_ms + ((after_call_us - clock_start_us) / 1000);
    gint64 min_expected_delay = queue_head_pts_ms - target_after_ms;
    gint64 max_expected_delay = queue_head_pts_ms - target_before_ms;

    g_assert_cmpint(delay, >=, (gint)min_expected_delay);
    g_assert_cmpint(delay, <=, (gint)max_expected_delay);

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

    QueueWaitSignal signal = {
        .mutex = &queue_push_mutex,
        .cond = &queue_push_cond,
        .flag = &queue_push_blocking,
    };
    g_mutex_lock(&queue_push_mutex);
    queue_push_blocking = FALSE;
    g_mutex_unlock(&queue_push_mutex);
    video_player_set_queue_wait_hook_for_test(player,
                                              VIDEO_PLAYER_TEST_QUEUE_RENDER,
                                              queue_wait_hook_signal,
                                              &signal);

    GThread *thread = g_thread_new("queue-push-test", queue_push_thread_main, player);
    wait_for_flag_or_fail(&queue_push_mutex,
                          &queue_push_cond,
                          &queue_push_blocking,
                          "Timed out waiting for render queue push thread to block on full render queue");
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 2);
    g_assert_cmpint(video_player_queue_head_pts_for_test(player), ==, 100);

    VideoFrame *taken = video_player_queue_take_first(player);
    g_assert_nonnull(taken);
    g_assert_cmpint(taken->pts_ms, ==, 100);
    video_frame_destroy(taken);

    g_thread_join(thread);
    video_player_set_queue_wait_hook_for_test(NULL,
                                              VIDEO_PLAYER_TEST_QUEUE_RENDER,
                                              NULL,
                                              NULL);

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

static void test_eof_handling_worker_eof_drains_tail_work_to_terminal_stop(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }
    if (!init_minimal_seek_context(player)) {
        video_player_destroy(player);
        g_test_skip("ffmpeg seek test context unavailable");
        return;
    }

    player->is_playing = TRUE;
    player->timer_id = g_timeout_add(60000, timeout_source_noop, NULL);
    g_assert_cmpuint(player->timer_id, !=, 0);
    guint active_timer_id = player->timer_id;
    g_mutex_lock(&player->state_mutex);
    player->last_presented_pts_ms = 16533;
    g_mutex_unlock(&player->state_mutex);

    video_player_queue_push(player, make_test_frame(16566));
    DecodedFrame *queued_decoded = g_new0(DecodedFrame, 1);
    decoded_frame_fill(queued_decoded, 16600, 1);
    video_player_decode_queue_push(player, queued_decoded);

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 1);
    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 1);
    g_assert_cmpint(video_player_current_position_ms_for_test(player), ==, 16533);

    video_player_handle_eof_for_test(player);

    g_assert_true(player->eof_pending);
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 1);
    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 1);
    g_assert_cmpint(player->render_in_flight, ==, 0);
    g_mutex_lock(&player->state_mutex);
    g_assert_cmpint(player->last_presented_pts_ms, ==, 16533);
    g_assert_false(player->rewind_needs_resync);
    g_mutex_unlock(&player->state_mutex);
    g_assert_true(player->is_playing);
    g_assert_cmpuint(player->timer_id, ==, active_timer_id);
    g_assert_false(player->eof_ended);
    g_assert_false(player->draining);
    g_assert_cmpint(video_player_current_position_ms_for_test(player), ==, 16533);

    DecodedFrame *in_flight = video_player_decode_queue_wait_and_take(player);
    g_assert_nonnull(in_flight);
    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 0);
    g_assert_cmpint(player->render_in_flight, ==, 1);

    g_assert_true(video_player_render_frame_for_test(player));
    g_assert_true(player->eof_pending);
    g_assert_false(player->eof_ended);
    g_assert_true(player->is_playing);
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);
    g_assert_cmpint(player->render_in_flight, ==, 1);
    g_mutex_lock(&player->state_mutex);
    g_assert_cmpint(player->last_presented_pts_ms, ==, 16566);
    g_mutex_unlock(&player->state_mutex);

    decoded_frame_destroy(in_flight);
    video_player_render_work_finished_for_test(player);

    g_assert_false(player->eof_pending);
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);
    g_assert_cmpuint(g_queue_get_length(player->decode_queue), ==, 0);
    g_assert_true(player->eof_ended);
    g_assert_false(player->is_playing);
    g_assert_cmpuint(player->timer_id, ==, 0);
    g_assert_cmpint(video_player_current_position_ms_for_test(player), ==, 16566);

    teardown_minimal_seek_context(player);
    video_player_destroy(player);
}

static void test_queue_insert_sorted_rechecks_last_presented_after_full_queue_wait(void) {
    VideoPlayer *player = video_player_new(2, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    init_test_sync_primitives();
    player->max_queue_size = 1;
    guint current_generation_after_eof = player->playback_generation + 1;
    video_player_queue_push(player, make_test_frame(17000));
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 1);

    g_mutex_lock(&player->state_mutex);
    player->last_presented_pts_ms = 16000;
    g_mutex_unlock(&player->state_mutex);

    QueueWaitSignal signal = {
        .mutex = &queue_push_mutex,
        .cond = &queue_push_cond,
        .flag = &queue_push_blocking,
    };
    QueueInsertCall call = {
        .player = player,
        .pts_ms = 16200,
        .generation = current_generation_after_eof,
    };
    g_mutex_init(&call.mutex);
    g_cond_init(&call.cond);
    call.completed = FALSE;

    g_mutex_lock(&queue_push_mutex);
    queue_push_blocking = FALSE;
    g_mutex_unlock(&queue_push_mutex);
    video_player_set_queue_wait_hook_for_test(player,
                                              VIDEO_PLAYER_TEST_QUEUE_RENDER,
                                              queue_wait_hook_signal,
                                              &signal);

    GThread *thread = g_thread_new("queue-insert-stale-test", queue_insert_thread_main, &call);
    wait_for_flag_or_fail(&queue_push_mutex,
                          &queue_push_cond,
                          &queue_push_blocking,
                          "Timed out waiting for sorted insert thread to block on full render queue");

    g_mutex_lock(&player->state_mutex);
    player->last_presented_pts_ms = 16533;
    g_mutex_unlock(&player->state_mutex);

    video_player_handle_terminal_eof_for_test(player);

    g_thread_join(thread);
    video_player_set_queue_wait_hook_for_test(NULL,
                                              VIDEO_PLAYER_TEST_QUEUE_RENDER,
                                              NULL,
                                              NULL);

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);

    g_cond_clear(&call.cond);
    g_mutex_clear(&call.mutex);

    video_player_destroy(player);
}

static void test_queue_insert_sorted_rejects_old_generation_after_eof_invalidation(void) {
    VideoPlayer *player = video_player_new(2, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    init_test_sync_primitives();
    player->max_queue_size = 1;
    guint old_generation = player->playback_generation;
    video_player_queue_push(player, make_test_frame_with_generation(17000, old_generation));
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 1);

    g_mutex_lock(&player->state_mutex);
    player->last_presented_pts_ms = 16533;
    g_mutex_unlock(&player->state_mutex);

    QueueWaitSignal signal = {
        .mutex = &queue_push_mutex,
        .cond = &queue_push_cond,
        .flag = &queue_push_blocking,
    };
    QueueInsertCall call = {
        .player = player,
        .pts_ms = 16600,
        .generation = old_generation,
    };
    g_mutex_init(&call.mutex);
    g_cond_init(&call.cond);
    call.completed = FALSE;

    g_mutex_lock(&queue_push_mutex);
    queue_push_blocking = FALSE;
    g_mutex_unlock(&queue_push_mutex);
    video_player_set_queue_wait_hook_for_test(player,
                                              VIDEO_PLAYER_TEST_QUEUE_RENDER,
                                              queue_wait_hook_signal,
                                              &signal);

    GThread *thread = g_thread_new("queue-insert-generation-test", queue_insert_thread_main, &call);
    wait_for_flag_or_fail(&queue_push_mutex,
                          &queue_push_cond,
                          &queue_push_blocking,
                          "Timed out waiting for old-generation insert thread to block on full render queue");

    video_player_handle_terminal_eof_for_test(player);

    g_thread_join(thread);
    video_player_set_queue_wait_hook_for_test(NULL,
                                              VIDEO_PLAYER_TEST_QUEUE_RENDER,
                                              NULL,
                                              NULL);

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);

    g_cond_clear(&call.cond);
    g_mutex_clear(&call.mutex);

    video_player_destroy(player);
}

static void test_queue_push_rejects_old_generation_after_eof_invalidation(void) {
    VideoPlayer *player = video_player_new(2, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    init_test_sync_primitives();
    player->max_queue_size = 1;
    guint old_generation = player->playback_generation;
    video_player_queue_push(player, make_test_frame_with_generation(17000, old_generation));
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 1);

    g_mutex_lock(&player->state_mutex);
    player->last_presented_pts_ms = 16533;
    g_mutex_unlock(&player->state_mutex);

    QueueWaitSignal signal = {
        .mutex = &queue_push_mutex,
        .cond = &queue_push_cond,
        .flag = &queue_push_blocking,
    };
    QueueInsertCall call = {
        .player = player,
        .pts_ms = 16600,
        .generation = old_generation,
    };
    g_mutex_init(&call.mutex);
    g_cond_init(&call.cond);
    call.completed = FALSE;

    g_mutex_lock(&queue_push_mutex);
    queue_push_blocking = FALSE;
    g_mutex_unlock(&queue_push_mutex);
    video_player_set_queue_wait_hook_for_test(player,
                                              VIDEO_PLAYER_TEST_QUEUE_RENDER,
                                              queue_wait_hook_signal,
                                              &signal);

    GThread *thread = g_thread_new("queue-push-generation-test", queue_push_call_thread_main, &call);
    wait_for_flag_or_fail(&queue_push_mutex,
                          &queue_push_cond,
                          &queue_push_blocking,
                          "Timed out waiting for old-generation push thread to block on full render queue");

    video_player_handle_terminal_eof_for_test(player);

    g_thread_join(thread);
    video_player_set_queue_wait_hook_for_test(NULL,
                                              VIDEO_PLAYER_TEST_QUEUE_RENDER,
                                              NULL,
                                              NULL);

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);

    g_cond_clear(&call.cond);
    g_mutex_clear(&call.mutex);

    video_player_destroy(player);
}

static void test_debug_logging_stale_drop_completes_after_eof_handling(void) {
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 0, 0);
        g_test_trap_assert_passed();
        return;
    }

    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    video_player_debug_reset_for_test();
    g_setenv("PIXELTERM_DEBUG_VIDEO", "1", TRUE);

    g_mutex_lock(&player->state_mutex);
    player->last_presented_pts_ms = 16533;
    g_mutex_unlock(&player->state_mutex);
    video_player_handle_eof_for_test(player);
    video_player_debug_close_stream();
    g_assert_false(video_player_debug_has_current_stream_for_test());

    QueueInsertCall call = {
        .player = player,
        .pts_ms = 16433,
    };
    g_mutex_init(&call.mutex);
    g_cond_init(&call.cond);
    call.completed = FALSE;

    GThread *thread = g_thread_new("queue-insert-debug-stale-test", queue_insert_thread_main, &call);
    wait_for_flag_or_fail(&call.mutex,
                          &call.cond,
                          &call.completed,
                          "Timed out waiting for stale-drop path to complete with debug logging enabled");
    g_thread_join(thread);

    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);
    g_assert_true(video_player_debug_has_current_stream_for_test());

    g_cond_clear(&call.cond);
    g_mutex_clear(&call.mutex);
    video_player_debug_reset_for_test();

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

static void test_debug_logging_disabled_skips_metric_locks_before_queue_wait(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    init_test_sync_primitives();
    video_player_debug_reset_for_test();
    g_unsetenv("PIXELTERM_DEBUG_VIDEO");

    player->max_queue_size = 1;
    video_player_queue_push(player, make_test_frame(100));
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 1);

    QueueWaitSignal signal = {
        .mutex = &queue_push_mutex,
        .cond = &queue_push_cond,
        .flag = &queue_push_blocking,
    };
    g_mutex_lock(&queue_push_mutex);
    queue_push_blocking = FALSE;
    g_mutex_unlock(&queue_push_mutex);
    video_player_set_queue_wait_hook_for_test(player,
                                              VIDEO_PLAYER_TEST_QUEUE_RENDER,
                                              queue_wait_hook_signal,
                                              &signal);

    g_mutex_lock(&player->state_mutex);
    GThread *thread = g_thread_new("queue-push-debug-fast-path-test", queue_push_thread_main, player);
    wait_for_flag_or_fail(&queue_push_mutex,
                          &queue_push_cond,
                          &queue_push_blocking,
                          "Timed out waiting for render queue push thread to reach queue wait while debug logging is disabled");
    g_mutex_unlock(&player->state_mutex);

    g_mutex_lock(&player->queue_mutex);
    player->worker_stop = TRUE;
    g_cond_broadcast(&player->frame_queue_has_space);
    g_mutex_unlock(&player->queue_mutex);

    g_thread_join(thread);
    video_player_set_queue_wait_hook_for_test(NULL,
                                              VIDEO_PLAYER_TEST_QUEUE_RENDER,
                                              NULL,
                                              NULL);
    video_player_debug_reset_for_test();

    video_player_destroy(player);
}

static void test_seek_frame_with_test_hook_uses_registered_callback(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, 1.0);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }
    if (!init_minimal_seek_context(player)) {
        video_player_destroy(player);
        g_test_skip("ffmpeg seek test context unavailable");
        return;
    }

    seek_hook_call_count = 0;
    seek_hook_result = 123;
    video_player_set_seek_hook_for_test(test_seek_hook);

    g_assert_cmpint(video_player_seek_frame_with_test_hook(player, 4321, AVSEEK_FLAG_BACKWARD), ==, 123);
    g_assert_cmpint(seek_hook_call_count, ==, 1);

    video_player_set_seek_hook_for_test(NULL);
    teardown_minimal_seek_context(player);
    video_player_destroy(player);
}

void register_video_player_tests(void) {
    g_test_add_func("/video_player/reset_timing_state/clears_loop_sensitive_fields",
                    test_reset_timing_state_clears_loop_sensitive_fields);
    g_test_add_func("/video_player/fallback_pts/set_waits_on_state_mutex",
                    test_set_fallback_pts_waits_on_state_mutex);
    g_test_add_func("/video_player/fallback_pts/resolve_and_advance_waits_on_state_mutex",
                    test_resolve_and_advance_fallback_pts_waits_on_state_mutex);
    g_test_add_func("/video_player/current_position/prefers_last_presented_pts",
                    test_current_position_prefers_last_presented_pts);
    g_test_add_func("/video_player/current_position/uses_clock_when_no_presented_pts",
                    test_current_position_uses_clock_when_no_presented_pts);
    g_test_add_func("/video_player/current_position/uses_fallback_pts_when_clock_not_started",
                    test_current_position_uses_fallback_pts_when_clock_not_started);
    g_test_add_func("/video_player/current_position/clamps_negative_fallback_pts_to_zero",
                    test_current_position_clamps_negative_fallback_pts_to_zero);
    g_test_add_func("/video_player/seek_context/teardown_frees_manual_format_context",
                    test_teardown_minimal_seek_context_frees_manual_format_context);
    g_test_add_func("/video_player/seek_target/clamps_to_zero_and_duration",
                    test_seek_target_clamps_to_zero_and_duration);
    g_test_add_func("/video_player/seek_relative/zero_delta_is_noop",
                    test_seek_relative_zero_delta_is_noop);
    g_test_add_func("/video_player/seek_relative/failed_seek_preserves_state",
                    test_seek_relative_failed_seek_preserves_state);
    g_test_add_func("/video_player/seek_relative/paused_seek_refreshes_preview",
                    test_seek_relative_paused_seek_refreshes_preview);
    g_test_add_func("/video_player/seek_relative/paused_repeated_seeks_preserve_latest_target",
                    test_seek_relative_paused_repeated_seeks_preserve_latest_target);
    g_test_add_func("/video_player/seek_relative/preview_bails_after_decode_attempt_limit",
                    test_seek_relative_preview_bails_after_decode_attempt_limit);
    g_test_add_func("/video_player/seek_relative/default_attempt_limit_is_bounded",
                    test_seek_preview_default_attempt_limit_is_bounded);
    g_test_add_func("/video_player/seek_preview/uses_hook_before_decoder_setup",
                    test_render_seek_preview_uses_hook_before_decoder_setup);
    g_test_add_func("/video_player/seek_relative/resets_visual_state_under_state_mutex",
                    test_seek_relative_resets_visual_state_under_state_mutex);
    g_test_add_func("/video_player/play/after_eof_rewinds_explicitly_to_start",
                    test_play_after_eof_rewinds_explicitly_to_start);
    g_test_add_func("/video_player/seek_relative/after_eof_stops_parked_workers_before_preview",
                    test_seek_relative_after_eof_stops_parked_workers_before_preview);
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
    g_test_add_func("/video_player/decode_queue/wait_and_take_blocks_until_item_arrives",
                    test_decode_queue_wait_and_take_blocks_until_item_arrives);
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
    g_test_add_func("/video_player/eof_handling/worker_eof_drains_tail_work_to_terminal_stop",
                    test_eof_handling_worker_eof_drains_tail_work_to_terminal_stop);
    g_test_add_func("/video_player/render_queue/rechecks_last_presented_after_full_queue_wait",
                    test_queue_insert_sorted_rechecks_last_presented_after_full_queue_wait);
    g_test_add_func("/video_player/render_queue/rejects_old_generation_after_eof_invalidation",
                    test_queue_insert_sorted_rejects_old_generation_after_eof_invalidation);
    g_test_add_func("/video_player/render_queue/push_rejects_old_generation_after_eof_invalidation",
                    test_queue_push_rejects_old_generation_after_eof_invalidation);
    g_test_add_func("/video_player/debug_logging/stale_drop_completes_after_eof_handling",
                    test_debug_logging_stale_drop_completes_after_eof_handling);
    g_test_add_func("/video_player/debug_logging/honors_environment_toggle",
                    test_debug_logging_honors_environment_toggle);
    g_test_add_func("/video_player/debug_logging/closes_stream_when_last_player_is_destroyed",
                    test_debug_logging_closes_stream_when_last_player_is_destroyed);
    g_test_add_func("/video_player/debug_logging/disabled_skips_metric_locks_before_queue_wait",
                    test_debug_logging_disabled_skips_metric_locks_before_queue_wait);
    g_test_add_func("/video_player/seek_hook_helper/uses_registered_callback",
                    test_seek_frame_with_test_hook_uses_registered_callback);
    g_test_add_func("/video_player/queue_take_for_playback/waits_even_when_future_queue_is_full",
                    test_queue_take_for_playback_waits_even_when_future_queue_is_full);
    g_test_add_func("/video_player/queue_take_for_playback/waits_when_future_queue_is_not_full",
                    test_queue_take_for_playback_waits_when_future_queue_is_not_full);
    g_test_add_func("/video_player/calc_delay/uses_queue_head_even_with_high_io_avg",
                    test_calc_delay_uses_queue_head_even_with_high_io_avg);
    g_test_add_func("/video_player/calc_delay/retries_quickly_when_playing_and_queue_is_empty",
                    test_calc_delay_retries_quickly_when_playing_and_queue_is_empty);
}
