#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <glib.h>
#include <glib/gstdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "process_env.h"
#include "kitty_graphics.h"
#include "terminal_output.h"
#include "gif_player.h"
#include "gif_player_test_internal.h"
#include "video_player_decode_internal.h"
#include "video_player_clock_internal.h"
#include "video_player_test_internal.h"
#include "test_output_stream.h"

static gsize test_video_player_sync_once = 0;

static GMutex decode_queue_push_mutex;
static GCond decode_queue_push_cond;
static gint decode_queue_push_blocking = FALSE;

static GMutex queue_push_mutex;
static GCond queue_push_cond;
static gint queue_push_blocking = FALSE;
static GMutex decode_queue_item_mutex;
static GCond decode_queue_item_cond;
static gint decode_queue_item_received = FALSE;
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

/* Six 2x2 keyframes at 1 fps: red, green, blue, yellow, cyan, white. */
static const gchar *k_seek_preview_video_fixture_base64 =
    "AAAAIGZ0eXBpc29tAAACAGlzb21pc28yYXZjMW1wNDEAAAAIZnJlZQAAAuNtZGF0AAACUQYF//9N"
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
    "X3JhdGlvPTEuNDAgYXE9MACAAAAAE2WIhDoRigACGPHAAED2OAAIeWAAAAAUZYiCAToRigACP9HA"
    "AFHmOAAK3OAAAAAUZYiEBOhGKAANX8cAAW1o4AAh84AAAAAUZYiCAToRigADQ9HAAFtmOAAIe2AA"
    "AAAUZYiEBOhGKAAKj0cAASNo4AAts4AAAAALZYiCATomKAAOn2AAAAMVbW9vdgAAAGxtdmhkAAAA"
    "AAAAAAAAAAAAAAAD6AAAF3AAAQAAAQAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAABAAAAAAAA"
    "AAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgAAAkB0cmFrAAAAXHRraGQA"
    "AAADAAAAAAAAAAAAAAABAAAAAAAAF3AAAAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAB"
    "AAAAAAAAAAAAAAAAAABAAAAAAAIAAAACAAAAAAAkZWR0cwAAABxlbHN0AAAAAAAAAAEAABdwAAAA"
    "AAABAAAAAAG4bWRpYQAAACBtZGhkAAAAAAAAAAAAAAAAAABAAAABgABVxAAAAAAALWhkbHIAAAAA"
    "AAAAAHZpZGUAAAAAAAAAAAAAAABWaWRlb0hhbmRsZXIAAAABY21pbmYAAAAUdm1oZAAAAAEAAAAA"
    "AAAAAAAAACRkaW5mAAAAHGRyZWYAAAAAAAAAAQAAAAx1cmwgAAAAAQAAASNzdGJsAAAAp3N0c2QA"
    "AAAAAAAAAQAAAJdhdmMxAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAAIAAgBIAAAASAAAAAAAAAAB"
    "FExhdmM2My4xLjEwMiBsaWJ4MjY0AAAAAAAAAAAAAAAAGP//AAAALWF2Y0MBQsAK/+EAFmdCwArd"
    "+IiIQAAAAwBAAAADAIPEieABAARozg/IAAAAFGJ0cnQAAAAAAAADzgAAAAAAAAAYc3R0cwAAAAAA"
    "AAABAAAABgAAQAAAAAAcc3RzYwAAAAAAAAABAAAAAQAAAAYAAAABAAAALHN0c3oAAAAAAAAAAAAA"
    "AAYAAAJsAAAAGAAAABgAAAAYAAAAGAAAAA8AAAAUc3RjbwAAAAAAAAABAAAAMAAAAGF1ZHRhAAAA"
    "WW1ldGEAAAAAAAAAIWhkbHIAAAAAAAAAAG1kaXJhcHBsAAAAAAAAAAAAAAAALGlsc3QAAAAkqXRv"
    "bwAAABxkYXRhAAAAAQAAAABMYXZmNjMuMS4xMDI=";

typedef struct {
    GMutex *mutex;
    GCond *cond;
    gint *flag;
} QueueWaitSignal;

typedef struct {
    VideoPlayer *player;
    gint64 pts_ms;
    guint generation;
    GMutex mutex;
    GCond cond;
    gint completed;
} QueueInsertCall;

typedef struct {
    VideoPlayer *player;
    gint64 pts_ms;
    GMutex mutex;
    GCond cond;
    gint started;
} FallbackSetCall;

typedef struct {
    VideoPlayer *player;
    gint64 raw_pts_ms;
    gint frame_delay;
    gint64 resolved_raw_pts_ms;
    gint64 next_fallback_pts_ms;
    GMutex mutex;
    GCond cond;
    gint started;
} FallbackAdvanceCall;

typedef struct {
    VideoPlayer *player;
    GMutex mutex;
    GCond cond;
    gint started;
    gint finished;
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

gchar *write_seek_preview_video_fixture(void) {
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

static void wait_for_flag_or_fail(GMutex *mutex, GCond *cond, gint *flag, const gchar *message) {
    gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_MILLISECOND * 500;

    g_mutex_lock(mutex);
    while (!g_atomic_int_get(flag)) {
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
    g_atomic_int_set(signal->flag, TRUE);
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
    g_atomic_int_set(&call->completed, TRUE);
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
    g_atomic_int_set(&call->completed, TRUE);
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
    g_atomic_int_set(&call->started, TRUE);
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
    g_atomic_int_set(&call->started, TRUE);
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
    g_atomic_int_set(&call->started, TRUE);
    g_cond_broadcast(&call->cond);
    g_mutex_unlock(&call->mutex);

    g_mutex_lock(&call->player->queue_mutex);
    while (!g_atomic_int_get(&call->player->worker_stop)) {
        g_cond_wait(&call->player->frame_queue_has_space, &call->player->queue_mutex);
    }
    g_mutex_unlock(&call->player->queue_mutex);

    g_mutex_lock(&call->mutex);
    g_atomic_int_set(&call->finished, TRUE);
    g_cond_broadcast(&call->cond);
    g_mutex_unlock(&call->mutex);
    return NULL;
}

static GThread *start_parked_worker_for_test(const gchar *name, ParkedWorkerCall *call, VideoPlayer *player) {
    if (!call || !player) {
        return NULL;
    }

    call->player = player;
    g_atomic_int_set(&call->started, FALSE);
    g_atomic_int_set(&call->finished, FALSE);
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
        VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    g_atomic_int_set(&decode_queue_item_received, TRUE);
    g_cond_broadcast(&decode_queue_item_cond);
    g_mutex_unlock(&decode_queue_item_mutex);
    return NULL;
}

static void test_decode_queue_sixel_mode_waits_instead_of_replacing_oldest(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    g_atomic_int_set(&decode_queue_push_blocking, FALSE);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    g_atomic_int_set(&decode_queue_push_blocking, FALSE);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    init_test_sync_primitives();
    g_mutex_lock(&decode_queue_item_mutex);
    g_atomic_int_set(&decode_queue_item_received, FALSE);
    g_mutex_unlock(&decode_queue_item_mutex);

    GThread *thread = g_thread_new("decode-queue-wait-test", decode_queue_wait_thread_main, player);
    g_usleep(10 * 1000);

    g_mutex_lock(&decode_queue_item_mutex);
    g_assert_false(g_atomic_int_get(&decode_queue_item_received));
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    g_assert_cmpuint(player->playback_generation, ==, 1);

    video_player_destroy(player);
}

static void test_render_layout_generation_starts_initialized(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    g_assert_cmpuint(player->render_layout_generation, ==, 1);

    video_player_destroy(player);
}

static void test_render_layout_generation_increments_only_on_layout_change(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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

static void test_paused_seek_reports_output_failure(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    g_assert_nonnull(player);
    g_assert_cmpint(video_player_load(player, write_seek_preview_video_fixture()), ==, ERROR_NONE);
    seek_hook_result = 0;
    seek_preview_hook_return_value = TRUE;
    seek_preview_hook_enqueue_frame = TRUE;
    seek_preview_hook_frame_pts_offset_ms = 0;
    video_player_set_seek_hook_for_test(test_seek_hook);
    video_player_set_seek_preview_hook_for_test(test_seek_preview_hook);
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    int failed = open("/dev/null", O_RDONLY);
    g_assert_cmpint(saved, >=, 0);
    g_assert_cmpint(failed, >=, 0);
    g_assert_cmpint(dup2(failed, STDOUT_FILENO), >=, 0);
    close(failed);
    ErrorCode result = video_player_seek_relative_ms(player, 1000);
    int sink = open("/dev/null", O_WRONLY);
    g_assert_cmpint(sink, >=, 0);
    g_assert_cmpint(dup2(sink, STDOUT_FILENO), >=, 0);
    close(sink);
    g_assert_true(terminal_output_flush());
    g_assert_cmpint(dup2(saved, STDOUT_FILENO), >=, 0);
    close(saved);
    video_player_set_seek_hook_for_test(NULL);
    video_player_set_seek_preview_hook_for_test(NULL);
    seek_preview_hook_enqueue_frame = FALSE;
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_cmpint(player->fallback_pts_ms, ==, 1000);
    g_assert_false(video_player_is_playing(player));
    video_player_destroy(player);
}

static void test_seek_relative_paused_seek_refreshes_preview(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    g_assert_cmpint(video_player_set_renderer(player, NULL), ==, ERROR_NONE);

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

static void test_set_renderer_restarts_workers_when_replacing_during_playback(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    ImageRenderer *replacement = renderer_create();
    if (!replacement) {
        video_player_destroy(player);
        g_test_skip("renderer unavailable");
        return;
    }

    ParkedWorkerCall decode_call = {0};
    ParkedWorkerCall render_a_call = {0};
    ParkedWorkerCall render_b_call = {0};

    g_mutex_lock(&player->state_mutex);
    player->is_playing = TRUE;
    g_mutex_unlock(&player->state_mutex);
    player->worker_thread = start_parked_worker_for_test("parked-decode-renderer-test", &decode_call, player);
    player->render_workers[0] = start_parked_worker_for_test("parked-render-a-renderer-test", &render_a_call, player);
    player->render_workers[1] = start_parked_worker_for_test("parked-render-b-renderer-test", &render_b_call, player);
    player->render_workers_started = TRUE;

    g_assert_cmpint(video_player_set_renderer(player, replacement), ==, ERROR_NONE);

    wait_for_flag_or_fail(&decode_call.mutex,
                          &decode_call.cond,
                          &decode_call.finished,
                          "Timed out waiting for replaced decode worker to stop");
    wait_for_flag_or_fail(&render_a_call.mutex,
                          &render_a_call.cond,
                          &render_a_call.finished,
                          "Timed out waiting for replaced render worker A to stop");
    wait_for_flag_or_fail(&render_b_call.mutex,
                          &render_b_call.cond,
                          &render_b_call.finished,
                          "Timed out waiting for replaced render worker B to stop");
    g_assert_nonnull(player->worker_thread);
    g_assert_true(player->render_workers_started);
    g_assert_nonnull(player->render_workers[0]);
    g_assert_nonnull(player->render_workers[1]);

    video_player_stop(player);
    renderer_destroy(replacement);
    clear_parked_worker_for_test(&render_b_call);
    clear_parked_worker_for_test(&render_a_call);
    clear_parked_worker_for_test(&decode_call);
    video_player_destroy(player);
}

static void test_set_renderer_null_stops_playback_and_timer(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    g_mutex_lock(&player->state_mutex);
    player->is_playing = TRUE;
    player->timer_id = g_timeout_add(10, timeout_source_noop, NULL);
    guint active_timer_id = player->timer_id;
    g_mutex_unlock(&player->state_mutex);
    g_assert_cmpuint(active_timer_id, !=, 0);

    g_assert_cmpint(video_player_set_renderer(player, NULL), ==, ERROR_NONE);

    g_assert_false(video_player_is_playing(player));
    g_mutex_lock(&player->state_mutex);
    g_assert_cmpuint(player->timer_id, ==, 0);
    g_mutex_unlock(&player->state_mutex);

    video_player_destroy(player);
}

static void test_schedule_tick_skips_when_eof_already_ended(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    g_mutex_lock(&player->state_mutex);
    player->is_playing = TRUE;
    player->eof_ended = TRUE;
    g_mutex_unlock(&player->state_mutex);

    video_player_schedule_tick_for_test(player);

    g_mutex_lock(&player->state_mutex);
    g_assert_cmpuint(player->timer_id, ==, 0);
    g_mutex_unlock(&player->state_mutex);

    video_player_destroy(player);
}

static void test_schedule_tick_skips_when_not_playing(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    g_mutex_lock(&player->state_mutex);
    player->is_playing = FALSE;
    player->eof_ended = FALSE;
    g_mutex_unlock(&player->state_mutex);

    video_player_schedule_tick_for_test(player);

    g_mutex_lock(&player->state_mutex);
    g_assert_cmpuint(player->timer_id, ==, 0);
    g_mutex_unlock(&player->state_mutex);

    video_player_destroy(player);
}

static void test_schedule_tick_skips_without_renderer(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    g_assert_cmpint(video_player_set_renderer(player, NULL), ==, ERROR_NONE);
    g_mutex_lock(&player->state_mutex);
    player->is_playing = TRUE;
    player->eof_ended = FALSE;
    g_mutex_unlock(&player->state_mutex);

    video_player_schedule_tick_for_test(player);

    g_mutex_lock(&player->state_mutex);
    g_assert_cmpuint(player->timer_id, ==, 0);
    g_mutex_unlock(&player->state_mutex);

    video_player_destroy(player);
}

static void test_seek_relative_after_eof_stops_parked_workers_before_preview(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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

    seek_preview_hook_call_count = 0;
    seek_preview_hook_target_ms = -1;
    seek_preview_hook_return_count = 0;
    seek_preview_hook_return_value = TRUE;
    seek_preview_hook_enqueue_frame = TRUE;
    seek_preview_hook_frame_pts_offset_ms = 0;
    video_player_set_seek_preview_hook_for_test(test_seek_preview_hook);

    g_assert_cmpint(video_player_seek_relative_ms(player, 1000), ==, ERROR_NONE);
    g_assert_cmpint(seek_preview_hook_call_count, ==, 1);
    g_assert_cmpint(seek_preview_hook_target_ms, ==, 5000);
    g_assert_cmpint(seek_preview_hook_return_count, ==, 1);
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

    seek_preview_hook_enqueue_frame = FALSE;
    seek_preview_hook_frame_pts_offset_ms = 0;
    video_player_set_seek_preview_hook_for_test(NULL);

    clear_parked_worker_for_test(&render_b_call);
    clear_parked_worker_for_test(&render_a_call);
    clear_parked_worker_for_test(&decode_call);
    video_player_destroy(player);
}

static void test_seek_relative_after_eof_real_preview_does_not_crash(void) {
#ifdef PIXELTERM_TSAN
    g_test_skip("system Chafa/GLib worker pools are not ThreadSanitizer-instrumented");
    return;
#endif
    if (g_test_subprocess()) {
        VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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

        player->worker_thread = start_parked_worker_for_test("parked-decode-real-preview-test", &decode_call, player);
        player->render_workers[0] = start_parked_worker_for_test("parked-render-a-real-preview-test", &render_a_call, player);
        player->render_workers[1] = start_parked_worker_for_test("parked-render-b-real-preview-test", &render_b_call, player);
        player->render_workers_started = TRUE;

        video_player_set_seek_preview_hook_for_test(NULL);
        g_assert_cmpint(video_player_seek_relative_ms(player, 1000), ==, ERROR_NONE);

        clear_parked_worker_for_test(&render_b_call);
        clear_parked_worker_for_test(&render_a_call);
        clear_parked_worker_for_test(&decode_call);
        video_player_destroy(player);
        return;
    }

    g_test_trap_subprocess(NULL, 0, 0);
    g_test_trap_assert_passed();
}

static void test_render_queue_insert_sorted_orders_by_pts(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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

static guint count_debug_event(const gchar *log, const gchar *event) {
    gchar *needle = g_strdup_printf("[video-debug] %s ", event);
    gchar **parts = g_strsplit(log, needle, -1);
    guint count = g_strv_length(parts) - 1;
    g_strfreev(parts);
    g_free(needle);
    return count;
}

static void assert_converted_fixture_pixels(const DecodedFrame *frame) {
    g_assert_nonnull(frame->pixels);
    g_assert_cmpint(frame->width, ==, 2);
    g_assert_cmpint(frame->height, ==, 2);
    g_assert_cmpint(frame->rowstride, >=, 8);
    static const guint8 colors[][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255},
        {255, 255, 0}, {0, 255, 255}, {255, 255, 255}
    };
    g_assert_cmpint(frame->pts_ms, >=, 0);
    g_assert_cmpint(frame->pts_ms % 1000, ==, 0);
    guint index = frame->pts_ms / 1000;
    g_assert_cmpuint(index, <, G_N_ELEMENTS(colors));
    /* Allow YUV rounding across FFmpeg versions. */
    for (gint y = 0; y < frame->height; y++) {
        for (gint x = 0; x < frame->width; x++) {
            const guint8 *pixel = frame->pixels + y * frame->rowstride + 4 * x;
            for (guint channel = 0; channel < 3; channel++) {
                if (colors[index][channel] == 255) {
                    g_assert_cmpuint(pixel[channel], >=, 250);
                } else {
                    g_assert_cmpuint(pixel[channel], <=, 3);
                }
            }
            g_assert_cmpuint(pixel[3], ==, 255);
        }
    }
}

static void test_decode_worker_conversion_gating(gconstpointer data) {
    gint mode = GPOINTER_TO_INT(data);
    gboolean late = mode == 1;
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE,
                                           TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    g_assert_nonnull(player);
    g_assert_cmpint(video_player_load(player, write_seek_preview_video_fixture()), ==, ERROR_NONE);

    gchar *log_path = NULL;
    gint fd = g_file_open_tmp("pixelterm-conversion-XXXXXX.log", &log_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    g_test_queue_destroy(remove_path, log_path);
    video_player_debug_reset_for_test();
    pixelterm_env_set_for_test("PIXELTERM_DEBUG_VIDEO", "1");
    pixelterm_env_set_for_test("PIXELTERM_DEBUG_VIDEO_LOG", log_path);

    /* Only the decode queue is drained; render backlog remains fixed. */
    player->max_queue_size = 16;
    for (gint i = 0; i < 5; i++) {
        video_player_queue_push(player, make_test_frame_with_generation(
            200000 + i * 1000, (guint)g_atomic_int_get(&player->playback_generation)));
    }
    player->clock_started = mode != 0;
    player->clock_start_us = g_get_monotonic_time();
    player->clock_start_pts_ms = 100000;
    player->last_presented_pts_ms = 99999;
    if (mode == 2) {
        player->last_presented_pts_ms = G_MININT64;
        player->last_present_us = g_get_monotonic_time() - 10 * G_TIME_SPAN_SECOND;
    }
    player->rewind_needs_resync = mode == 3;
    video_player_start_decode_worker_for_test(player);

    gboolean pending = FALSE;
    guint output_frames = 0;
    gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
    do {
        DecodedFrame *decoded_frame = video_player_decode_queue_take(player);
        if (decoded_frame) {
            assert_converted_fixture_pixels(decoded_frame);
            output_frames++;
        }
        decoded_frame_destroy(decoded_frame);
        g_mutex_lock(&player->state_mutex);
        pending = player->eof_pending;
        g_mutex_unlock(&player->state_mutex);
        if (!pending) {
            g_usleep(1000);
        }
    } while (!pending && g_get_monotonic_time() < deadline);
    DecodedFrame *tail;
    while ((tail = video_player_decode_queue_take(player))) {
        assert_converted_fixture_pixels(tail);
        output_frames++;
        decoded_frame_destroy(tail);
    }
    video_player_stop(player);
    video_player_destroy(player);
    video_player_debug_reset_for_test();
    pixelterm_env_unset_for_test("PIXELTERM_DEBUG_VIDEO");
    pixelterm_env_unset_for_test("PIXELTERM_DEBUG_VIDEO_LOG");

    gchar *log = NULL;
    g_assert_true(g_file_get_contents(log_path, &log, NULL, NULL));
    g_test_message("decode trace:\n%s", log);
    guint decoded = count_debug_event(log, "worker-decode-time");
    guint converted = count_debug_event(log, "worker-convert-time");
    guint dropped = count_debug_event(log, "worker-drop-late");
    g_free(log);
    g_assert_true(pending);
    g_assert_cmpuint(decoded, ==, 6);
    g_assert_cmpuint(converted, ==, late ? 0 : decoded);
    g_assert_cmpuint(dropped, ==, late ? decoded : 0);
    g_assert_cmpuint(output_frames, ==, late ? 0 : decoded);
}

static void test_queue_take_for_playback_waits_even_when_future_queue_is_full(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(2, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    g_atomic_int_set(&queue_push_blocking, FALSE);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(2, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    g_atomic_int_set(&call.completed, FALSE);

    g_mutex_lock(&queue_push_mutex);
    g_atomic_int_set(&queue_push_blocking, FALSE);
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
    VideoPlayer *player = video_player_new(2, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    g_atomic_int_set(&call.completed, FALSE);

    g_mutex_lock(&queue_push_mutex);
    g_atomic_int_set(&queue_push_blocking, FALSE);
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
    VideoPlayer *player = video_player_new(2, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    g_atomic_int_set(&call.completed, FALSE);

    g_mutex_lock(&queue_push_mutex);
    g_atomic_int_set(&queue_push_blocking, FALSE);
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

    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    video_player_debug_reset_for_test();
    pixelterm_env_set_for_test("PIXELTERM_DEBUG_VIDEO", "1");

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
    g_atomic_int_set(&call.completed, FALSE);

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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    video_player_debug_reset_for_test();
    pixelterm_env_unset_for_test("PIXELTERM_DEBUG_VIDEO");
    g_assert_false(video_player_debug_enabled());

    video_player_debug_reset_for_test();
    pixelterm_env_set_for_test("PIXELTERM_DEBUG_VIDEO", "1");
    g_assert_true(video_player_debug_enabled());
    g_assert_nonnull(video_player_debug_get_stream());
    pixelterm_env_unset_for_test("PIXELTERM_DEBUG_VIDEO");
    video_player_debug_reset_for_test();

    video_player_destroy(player);
}

static void test_debug_logging_accepts_performance_events(void) {
    g_assert_true(video_player_debug_should_log_for_test("worker-convert-time"));
    g_assert_true(video_player_debug_should_log_for_test("worker-copy-time"));
    g_assert_true(video_player_debug_should_log_for_test("render-submit"));
}

static void test_debug_logging_closes_stream_when_last_player_is_destroyed(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    video_player_debug_reset_for_test();
    pixelterm_env_set_for_test("PIXELTERM_DEBUG_VIDEO", "1");
    g_assert_nonnull(video_player_debug_get_stream());
    g_assert_true(video_player_debug_has_current_stream_for_test());

    video_player_destroy(player);

    g_assert_false(video_player_debug_has_current_stream_for_test());
    pixelterm_env_unset_for_test("PIXELTERM_DEBUG_VIDEO");
    video_player_debug_reset_for_test();
}

static void test_debug_logging_disabled_skips_metric_locks_before_queue_wait(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    init_test_sync_primitives();
    video_player_debug_reset_for_test();
    pixelterm_env_unset_for_test("PIXELTERM_DEBUG_VIDEO");

    player->max_queue_size = 1;
    video_player_queue_push(player, make_test_frame(100));
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 1);

    QueueWaitSignal signal = {
        .mutex = &queue_push_mutex,
        .cond = &queue_push_cond,
        .flag = &queue_push_blocking,
    };
    g_mutex_lock(&queue_push_mutex);
    g_atomic_int_set(&queue_push_blocking, FALSE);
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
    g_atomic_int_set(&player->worker_stop, TRUE);
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
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
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

static void test_render_worker_config_uses_player_color_enhance_without_renderer(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    if (player->renderer && player->owns_renderer) {
        renderer_destroy(player->renderer);
    }
    player->renderer = NULL;
    player->owns_renderer = FALSE;
    video_player_set_color_enhance(player, COLOR_ENHANCE_VIVID);

    RendererConfig config = video_player_render_worker_config_for_test(player);

    g_assert_cmpint(config.color_enhance, ==, COLOR_ENHANCE_VIVID);
    video_player_destroy(player);
}

static void test_public_layout_accessors_return_consistent_snapshots(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE,
                                          TEXT_SYMBOL_MODE_AUTO, 1.0,
                                          KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    gint top_row = -1;
    gint height = -1;
    g_assert_false(video_player_get_last_frame_bounds(player, &top_row, &height));
    g_assert_cmpint(top_row, ==, 0);
    g_assert_cmpint(height, ==, 0);

    g_mutex_lock(&player->state_mutex);
    player->last_frame_top_row = 4;
    player->last_frame_height = 2;
    player->last_frame_lines = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(player->last_frame_lines, g_strdup("first"));
    g_ptr_array_add(player->last_frame_lines, g_strdup("second"));
    g_mutex_unlock(&player->state_mutex);

    g_assert_true(video_player_get_last_frame_bounds(player, &top_row, &height));
    g_assert_cmpint(top_row, ==, 4);
    g_assert_cmpint(height, ==, 2);

    gchar *line = video_player_dup_cached_line_at_row(player, 5);
    g_assert_cmpstr(line, ==, "second");
    line[0] = 'S';
    g_free(line);

    line = video_player_dup_cached_line_at_row(player, 5);
    g_assert_cmpstr(line, ==, "second");
    g_free(line);
    g_assert_null(video_player_dup_cached_line_at_row(player, 3));

    video_player_destroy(player);
}

static void test_public_state_setters_update_player_configuration(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE,
                                          TEXT_SYMBOL_MODE_AUTO, 1.0,
                                          KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    video_player_set_show_stats(player, TRUE);
    g_mutex_lock(&player->state_mutex);
    g_assert_true(player->show_stats);
    g_mutex_unlock(&player->state_mutex);

    video_player_set_color_enhance(player, COLOR_ENHANCE_VIVID);
    g_mutex_lock(&player->render_mutex);
    g_assert_cmpint(player->color_enhance, ==, COLOR_ENHANCE_VIVID);
    if (player->renderer) {
        g_assert_cmpint(player->renderer->config.color_enhance,
                        ==,
                        COLOR_ENHANCE_VIVID);
    }
    g_mutex_unlock(&player->render_mutex);

    g_assert_false(video_player_is_loaded_file(player, "movie.mp4"));
    g_mutex_lock(&player->state_mutex);
    player->filepath = g_strdup("movie.mp4");
    g_mutex_unlock(&player->state_mutex);
    g_assert_true(video_player_is_loaded_file(player, "movie.mp4"));
    g_assert_false(video_player_is_loaded_file(player, "other.mp4"));

    video_player_set_show_stats(NULL, TRUE);
    video_player_set_color_enhance(NULL, COLOR_ENHANCE_VIVID);
    g_assert_false(video_player_is_loaded_file(NULL, "movie.mp4"));
    g_assert_false(video_player_is_loaded_file(player, NULL));
    g_assert_false(video_player_get_last_frame_bounds(NULL, NULL, NULL));
    g_assert_null(video_player_dup_cached_line_at_row(NULL, 1));

    video_player_destroy(player);
}

static void assert_renderer_protocol(VideoPlayer *player,
                                     gboolean force_text,
                                     gboolean force_sixel,
                                     gboolean force_iterm2,
                                     gboolean force_kitty) {
    g_mutex_lock(&player->render_mutex);
    g_assert_cmpint(player->renderer->config.force_text, ==, force_text);
    g_assert_cmpint(player->renderer->config.force_sixel, ==, force_sixel);
    g_assert_cmpint(player->renderer->config.force_iterm2, ==, force_iterm2);
    g_assert_cmpint(player->renderer->config.force_kitty, ==, force_kitty);
    g_mutex_unlock(&player->render_mutex);
}

static void test_public_protocol_cycle_preserves_existing_order(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE,
                                          TEXT_SYMBOL_MODE_AUTO, 1.0,
                                          KITTY_TRANSFER_AUTO);
    if (!player) {
        g_test_skip("video player unavailable");
        return;
    }

    g_assert_true(video_player_protocol_cycle_needs_redraw(player));
    g_assert_true(video_player_cycle_protocol(player));
    assert_renderer_protocol(player, FALSE, TRUE, FALSE, FALSE);
    g_assert_false(video_player_protocol_cycle_needs_redraw(player));
    g_assert_false(video_player_cycle_protocol(player));
    assert_renderer_protocol(player, FALSE, FALSE, TRUE, FALSE);
    g_assert_false(video_player_protocol_cycle_needs_redraw(player));
    g_assert_false(video_player_cycle_protocol(player));
    assert_renderer_protocol(player, FALSE, FALSE, FALSE, TRUE);
    g_assert_true(video_player_protocol_cycle_needs_redraw(player));
    g_assert_true(video_player_cycle_protocol(player));
    assert_renderer_protocol(player, TRUE, FALSE, FALSE, FALSE);
    g_assert_false(video_player_cycle_protocol(NULL));

    video_player_destroy(player);
}

static void test_frame_buffer_size_rejects_overflow(void) {
    gsize buffer_size = 0;

    g_assert_false(video_player_frame_buffer_size_for_test(G_MAXINT, G_MAXINT, &buffer_size));
}

static void test_frame_buffer_size_rejects_non_positive_metadata(void) {
    gsize buffer_size = 0;

    g_assert_false(video_player_frame_buffer_size_for_test(0, 4, &buffer_size));
    g_assert_false(video_player_frame_buffer_size_for_test(4, 0, &buffer_size));
}

static void test_rgba_layout_accepts_valid_metadata(void) {
    gsize buffer_size = 0;

    g_assert_true(video_player_rgba_layout_within_limits_for_test(10, 10, 40, &buffer_size));
    g_assert_cmpuint(buffer_size, ==, 400);
}

static void test_rgba_layout_rejects_short_rowstride(void) {
    gsize buffer_size = 0;

    g_assert_false(video_player_rgba_layout_within_limits_for_test(10, 10, 39, &buffer_size));
    g_assert_cmpuint(buffer_size, ==, 0);
}

static void test_dimensions_within_limits_accepts_max_geometry(void) {
    g_assert_true(video_player_dimensions_within_limits_for_test(PIXELTERM_MAX_DECODED_PIXELS, 1));
}

static void test_dimensions_within_limits_rejects_large_geometry(void) {
    g_assert_false(video_player_dimensions_within_limits_for_test(PIXELTERM_MAX_DECODED_PIXELS + 1, 1));
}

static void test_dimensions_within_limits_rejects_non_positive_metadata(void) {
    g_assert_false(video_player_dimensions_within_limits_for_test(0, 10));
    g_assert_false(video_player_dimensions_within_limits_for_test(10, 0));
    g_assert_false(video_player_dimensions_within_limits_for_test(-1, 10));
    g_assert_false(video_player_dimensions_within_limits_for_test(10, -1));
    g_assert_false(video_player_dimensions_within_limits_for_test(-1, -1));
}

static void test_create_sws_context_rejects_invalid_dimensions(void) {
    AVCodecContext codec_context = {0};
    codec_context.pix_fmt = AV_PIX_FMT_YUV420P;

    g_assert_null(video_player_create_sws_context(&codec_context, 0, 10));
    g_assert_null(video_player_create_sws_context(&codec_context, 10, 0));
    g_assert_null(video_player_create_sws_context(&codec_context, -1, 10));
    g_assert_null(video_player_create_sws_context(&codec_context, 10, -1));
}

static void test_create_sws_context_rejects_invalid_pixel_formats(void) {
    AVCodecContext codec_context = {0};
    const enum AVPixelFormat formats[] = {
        AV_PIX_FMT_NONE,
        (enum AVPixelFormat)-2,
        AV_PIX_FMT_NB,
        AV_PIX_FMT_VAAPI,
    };

    for (gsize i = 0; i < G_N_ELEMENTS(formats); i++) {
        codec_context.pix_fmt = formats[i];
        g_assert_null(video_player_create_sws_context(&codec_context, 16, 16));
    }
}

static void test_create_sws_context_accepts_supported_pixel_formats(void) {
    AVCodecContext codec_context = {0};
    const enum AVPixelFormat formats[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_YUVJ440P,
    };

    for (gsize i = 0; i < G_N_ELEMENTS(formats); i++) {
        codec_context.pix_fmt = formats[i];
        struct SwsContext *sws_context = video_player_create_sws_context(&codec_context, 16, 16);
        g_assert_nonnull(sws_context);
        sws_freeContext(sws_context);
    }
}

static void test_io_interrupt_follows_worker_stop_flag(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE,
                                          TEXT_SYMBOL_MODE_AUTO, 1.0,
                                          KITTY_TRANSFER_AUTO);
    g_assert_nonnull(player);

    g_atomic_int_set(&player->worker_stop, FALSE);
    g_assert_false(video_player_io_should_interrupt(player));
    g_atomic_int_set(&player->worker_stop, TRUE);
    g_assert_true(video_player_io_should_interrupt(player));

    g_atomic_int_set(&player->worker_stop, FALSE);
    video_player_destroy(player);
}

static void test_frame_layout_detects_dynamic_stream_changes(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE,
                                          TEXT_SYMBOL_MODE_AUTO, 1.0,
                                          KITTY_TRANSFER_AUTO);
    g_assert_nonnull(player);
    player->video_width = 320;
    player->video_height = 240;
    player->source_pixel_format = AV_PIX_FMT_YUV420P;

    AVFrame *frame = av_frame_alloc();
    g_assert_nonnull(frame);
    frame->width = 320;
    frame->height = 240;
    frame->format = AV_PIX_FMT_YUV420P;
    g_assert_true(video_player_frame_layout_matches(player, frame));

    frame->width = 640;
    g_assert_false(video_player_frame_layout_matches(player, frame));
    frame->width = 320;
    frame->format = AV_PIX_FMT_YUV444P;
    g_assert_false(video_player_frame_layout_matches(player, frame));

    av_frame_free(&frame);
    video_player_destroy(player);
}

static gpointer finished_worker_thread_for_test(gpointer user_data) {
    gint *finished = (gint *)user_data;
    g_assert_nonnull(finished);
    g_atomic_int_set(finished, TRUE);
    return NULL;
}

static void test_worker_restart_reclaims_stale_thread_handle(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE,
                                          TEXT_SYMBOL_MODE_AUTO, 1.0,
                                          KITTY_TRANSFER_AUTO);
    g_assert_nonnull(player);

    gint old_worker_finished = FALSE;
    player->worker_thread = g_thread_new("finished-worker-test",
                                         finished_worker_thread_for_test,
                                         &old_worker_finished);
    g_assert_nonnull(player->worker_thread);
    g_atomic_int_set(&player->worker_stop, TRUE);

    video_player_start_worker_for_test(player);

    g_assert_nonnull(player->worker_thread);
    g_assert_false(g_atomic_int_get(&player->worker_stop));
    g_assert_true(g_atomic_int_get(&old_worker_finished));

    video_player_stop(player);
    video_player_destroy(player);
}

static void test_direct_kitty_preserves_fallback_boundaries(void) {
    ImageRenderer *renderer = renderer_create();
    g_assert_nonnull(renderer);
    renderer->config.max_width = 1;
    renderer->config.max_height = 1;
    renderer->config.gamma = 2.0;
    guint8 pixels[] = {30, 60, 90, 255};
    DecodedFrame decoded = {.pixels = pixels, .width = 1, .height = 1, .rowstride = 4};
    GString *command = video_player_render_direct_kitty_for_test(renderer, &decoded);
    g_assert_null(command);
    renderer->config.gamma = 1.0;
    renderer->config.color_enhance = COLOR_ENHANCE_VIVID;
    command = video_player_render_direct_kitty_for_test(renderer, &decoded);
    g_assert_null(command);
    renderer->config.color_enhance = COLOR_ENHANCE_OFF;
    pixels[3] = 128;
    command = video_player_render_direct_kitty_for_test(renderer, &decoded);
    g_assert_null(command);
    pixels[3] = 255;
    pixelterm_env_unset_for_test("TMUX");
    pixelterm_env_unset_for_test("STY");
    command = video_player_render_direct_kitty_for_test(renderer, &decoded);
    g_assert_nonnull(command);
    g_assert_nonnull(strstr(command->str, "f=24,s=1,v=1,"));
    g_assert_nonnull(strstr(command->str, ";Hjxa\033\\"));
    g_string_free(command, TRUE);
    renderer->config.preserve_aspect_ratio = FALSE;
    g_assert_null(video_player_render_direct_kitty_for_test(renderer, &decoded));
    renderer->config.preserve_aspect_ratio = TRUE;
    pixelterm_env_set_for_test("TMUX", "/test/tmux");
    command = video_player_render_direct_kitty_for_test(renderer, &decoded);
    g_assert_null(command);
    pixelterm_env_reset_for_test();
    renderer_destroy(renderer);
}

static void test_direct_kitty_image_is_replaced_and_cleaned_up(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE,
                                           TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    g_assert_cmpint(video_player_load(player, write_seek_preview_video_fixture()), ==, ERROR_NONE);
    guint32 image_id = player->kitty_direct_image_id;
    g_assert_cmpuint(image_id, ==, 0);
    player->kitty_direct_image_id = image_id = 42; /* Simulate an allocated reservation. */
    FILE *capture = tmpfile();
    g_assert_nonnull(capture);
    fflush(stdout);
    gint saved_stdout = dup(STDOUT_FILENO);
    g_assert_cmpint(saved_stdout, >=, 0);
    g_assert_cmpint(dup2(fileno(capture), STDOUT_FILENO), >=, 0);
    for (gint i = 0; i < 3; i++) {
        VideoFrame *frame = make_test_frame_with_generation(i * 1000,
            (guint)g_atomic_int_get(&player->playback_generation));
        const guint8 pixel[] = {1, 2, 3};
        KittyGraphicsFrame *encoded = kitty_graphics_frame_new_direct_rgb(pixel, 1, 1, 3, 1, 1, image_id);
        g_assert_nonnull(encoded);
        g_string_free(frame->rendered, TRUE);
        frame->rendered = encoded->command;
        encoded->command = NULL;
        kitty_graphics_frame_free(encoded);
        frame->kitty_direct_image_id = image_id;
        frame->pixel_mode = CHAFA_PIXEL_MODE_KITTY;
        video_player_queue_push(player, frame);
        player->clock_started = FALSE;
        g_assert_true(video_player_render_frame_for_test(player));
        g_assert_true(player->kitty_direct_image_present);
        if (i == 1) {
            video_player_queue_push(player, make_test_frame_with_generation(2500,
                (guint)g_atomic_int_get(&player->playback_generation)));
            player->clock_started = FALSE;
            g_assert_true(video_player_render_frame_for_test(player));
            g_assert_false(player->kitty_direct_image_present);
        }
    }
    video_player_stop(player);
    g_assert_false(player->kitty_direct_image_present);
    video_player_destroy(player);
    fflush(stdout);
    g_assert_cmpint(dup2(saved_stdout, STDOUT_FILENO), >=, 0);
    close(saved_stdout);
    rewind(capture);
    gchar output[4096] = {0};
    gsize length = fread(output, 1, sizeof(output) - 1, capture);
    g_assert_cmpuint(length, >, 0);
    fclose(capture);
    gchar *delete_command = g_strdup_printf("\033_Ga=d,d=I,i=%u,q=2\033\\", image_id);
    gchar **parts = g_strsplit(output, delete_command, -1);
    g_assert_cmpuint(g_strv_length(parts) - 1, ==, 1);
    g_strfreev(parts);
    g_free(delete_command);
    g_assert_nonnull(strstr(output, "\033_Ga=d,d=i,i=42,q=2\033\\"));
    gchar *image_header = g_strdup_printf("a=T,f=24,s=1,v=1,c=1,r=1,i=%u,", image_id);
    parts = g_strsplit(output, image_header, -1);
    g_assert_cmpuint(g_strv_length(parts) - 1, ==, 3);
    g_strfreev(parts);
    g_free(image_header);
}

static void test_direct_kitty_worker_respects_transfer_mode(gconstpointer data) {
    KittyTransferMode mode = GPOINTER_TO_INT(data);
    VideoPlayer *player = video_player_new(4, FALSE, FALSE, TRUE, FALSE,
                                           TEXT_SYMBOL_MODE_AUTO, 1.0, mode);
    g_assert_nonnull(player);
    g_assert_cmpint(video_player_load(player, write_seek_preview_video_fixture()), ==, ERROR_NONE);
    player->kitty_shm_enabled = FALSE; /* Exercise the unavailable-SHM fallback. */
    player->kitty_direct_image_id = 42; /* Nonzero so SHM mode must enforce its own guard. */
    pixelterm_env_unset_for_test("TMUX");
    pixelterm_env_unset_for_test("STY");
    video_player_start_worker_for_test(player);
    VideoFrame *frame = NULL;
    gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
    while (!frame && g_get_monotonic_time() < deadline) {
        frame = video_player_queue_take_first(player);
        if (!frame) g_usleep(1000);
    }
    video_player_pause(player);
    g_assert_nonnull(frame);
    if (mode == KITTY_TRANSFER_SHM) {
        g_assert_cmpuint(frame->kitty_direct_image_id, ==, 0);
        g_assert_nonnull(strstr(frame->rendered->str, "f=32"));
    } else {
        g_assert_cmpuint(frame->kitty_direct_image_id, ==, player->kitty_direct_image_id);
        g_assert_nonnull(strstr(frame->rendered->str, "f=24"));
    }
    video_frame_destroy(frame);
    player->kitty_direct_image_id = 0; /* The fixture never allocated a real terminal resource. */
    video_player_destroy(player);
    pixelterm_env_reset_for_test();
}

#if defined(__GLIBC__) || defined(__APPLE__)
typedef struct {
    guint attempts;
    GString *output;
    gboolean partial;
    gboolean delivered_but_failed;
    gboolean fail_recovery;
    gboolean fail_all;
    GString *attempted;
} DeleteOutput;

static ssize_t delete_output_write(void *data, const char *bytes, size_t length) {
    DeleteOutput *output = data;
    if (output->attempted) g_string_append_len(output->attempted, bytes, length);
    if (output->fail_all) { output->attempts++; errno = EIO; return -1; }
    if (output->attempts++ == 0) {
        if (output->delivered_but_failed) {
            g_string_append_len(output->output, bytes, length);
            errno = EIO;
            return -1;
        }
        if (output->partial) {
            g_string_append_len(output->output, bytes, length / 2);
            errno = EIO;
            return -1;
        }
        errno = EIO;
        return -1;
    }
    if (output->fail_recovery && output->attempts == 2) {
        errno = EIO;
        return -1;
    }
    g_string_append_len(output->output, bytes, length);
    return (ssize_t)length;
}

static void test_failed_delete_buffer_is_discarded(void) {
    DeleteOutput output = {.output = g_string_new(NULL), .attempted = g_string_new(NULL)};
    FILE *capture = test_output_stream(&output, delete_output_write);
    g_assert_nonnull(capture);
    char buffer[256];
    g_assert_cmpint(setvbuf(capture, buffer, _IOFBF, sizeof(buffer)), ==, 0);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    const char *deletion = "\033_Ga=d,d=I,i=42,q=2\033\\";
    gboolean submitted = terminal_output_write(deletion, strlen(deletion));
    gboolean recovered = terminal_output_flush();
    fclose(capture);
    stdout = saved;
    g_assert_false(submitted);
    g_assert_true(recovered);
    g_assert_cmpstr(output.attempted->str, ==, "\033_Ga=d,d=I,i=42,q=2\033\\\033\\");
    g_assert_cmpstr(output.output->str, ==, "\033\\");
    g_string_free(output.attempted, TRUE);
    g_string_free(output.output, TRUE);
}

static void test_deferred_cleanup_retry_deadline(void) {
    DeleteOutput output = {.output = g_string_new(NULL), .delivered_but_failed = TRUE};
    FILE *capture = test_output_stream(&output, delete_output_write);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    kitty_graphics_defer_image_delete(42);
    gint64 before_failure = g_get_monotonic_time();
    gboolean ready_after_failure = terminal_output_ready(before_failure);
    guint first = output.attempts;
    gboolean ready_before_deadline = terminal_output_ready(before_failure + 99999);
    guint before_deadline = output.attempts;
    gboolean ready_after_recovery = terminal_output_ready(g_get_monotonic_time() + 100000);
    fclose(capture);
    stdout = saved;
    g_assert_false(ready_after_failure);
    g_assert_false(ready_before_deadline);
    g_assert_true(ready_after_recovery);
    g_assert_cmpuint(first, ==, 1);
    g_assert_cmpuint(before_deadline, ==, 1);
    g_assert_cmpuint(output.attempts, ==, 2);
    g_assert_cmpstr(output.output->str, ==, "\033\\\033_Ga=d,d=I,i=42,q=2\033\\\033\\");
    g_string_free(output.output, TRUE);
}

static void test_background_frame_waits_for_restore_deadline(void) {
    VideoPlayer *player = video_player_new(4, FALSE, FALSE, FALSE, TRUE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    g_assert_cmpint(video_player_load(player, write_seek_preview_video_fixture()), ==, ERROR_NONE);
    VideoFrame *frame = make_test_frame_with_generation(0, player->playback_generation);
    g_string_assign(frame->rendered, "DEFERRED-VIDEO-FRAME");
    video_player_queue_push(player, frame);
    player->is_playing = TRUE;
    ParkedWorkerCall worker;
    player->worker_thread = start_parked_worker_for_test("deferred-video-worker", &worker, player);
    DeleteOutput output = {.output = g_string_new(NULL), .attempted = g_string_new(NULL)};
    FILE *capture = test_output_stream(&output, delete_output_write);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    terminal_output_defer("\033[?25h", TERMINAL_OUTPUT_RESTORE);
    g_assert_false(terminal_output_ready(g_get_monotonic_time()));
    guint attempts = output.attempts;
    video_player_schedule_tick_for_test(player);
    for (guint i = 0; i < 3; i++) {
        guint timer_id = player->timer_id;
        GSource *source = g_main_context_find_source_by_id(NULL, timer_id);
        g_assert_nonnull(source);
        g_source_set_ready_time(source, 0);
        g_assert_true(g_main_context_iteration(NULL, FALSE));
        g_assert_cmpuint(player->timer_id, !=, 0);
        g_assert_cmpuint(player->timer_id, !=, timer_id);
        g_assert_cmpuint(output.attempts, ==, attempts);
        g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 1);
        g_assert_true(g_queue_peek_head(player->frame_queue) == frame);
        g_assert_true(video_player_is_playing(player));
        g_assert_nonnull(player->worker_thread);
        g_assert_false(player->clock_started);
        g_assert_cmpint(player->last_presented_pts_ms, ==, G_MININT64);
    }
    g_assert_true(terminal_output_ready(g_get_monotonic_time() + 100000));
    g_assert_true(video_player_render_frame_for_test(player));
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);
    g_assert_cmpint(player->last_presented_pts_ms, ==, 0);
    g_assert_true(video_player_is_playing(player));
    fclose(capture);
    stdout = saved;
    g_assert_cmpstr(output.output->str, ==,
        "\033\\\033[?25h\033\\\033[HDEFERRED-VIDEO-FRAME\033[J");
    const gchar *submitted = strstr(output.attempted->str, "DEFERRED-VIDEO-FRAME");
    g_assert_nonnull(submitted);
    g_assert_null(strstr(submitted + strlen("DEFERRED-VIDEO-FRAME"), "DEFERRED-VIDEO-FRAME"));
    video_player_destroy(player);
    g_assert_true(g_atomic_int_get(&worker.finished));
    clear_parked_worker_for_test(&worker);
    g_string_free(output.output, TRUE);
    g_string_free(output.attempted, TRUE);
}

static void test_cleanup_failure_aborts_media_change(gconstpointer cycle) {
    VideoPlayer *player = video_player_new(4, FALSE, FALSE, TRUE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    const gchar *path = write_seek_preview_video_fixture();
    g_assert_cmpint(video_player_load(player, path), ==, ERROR_NONE);
    AVFormatContext *prior_media = player->format_context;
    guint layout_generation = player->render_layout_generation;
    player->kitty_direct_image_id = 42;
    player->kitty_direct_image_present = TRUE;
    ParkedWorkerCall worker;
    player->worker_thread = start_parked_worker_for_test("failed-cleanup-worker", &worker, player);
    player->is_playing = TRUE;
    DeleteOutput output = {.output = g_string_new(NULL), .delivered_but_failed = TRUE};
    FILE *capture = test_output_stream(&output, delete_output_write);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    gboolean needs_redraw = TRUE;
    ErrorCode result = cycle ? video_player_cycle_protocol_checked(player, &needs_redraw) :
        video_player_load(player, "/nonexistent-pixelterm-replacement.mp4");
    gboolean recovered = terminal_output_flush();
    fclose(capture);
    stdout = saved;
    g_assert_true(g_atomic_int_get(&worker.finished));
    clear_parked_worker_for_test(&worker);
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    if (cycle) g_assert_false(needs_redraw);
    g_assert_true(player->format_context == prior_media);
    g_assert_cmpstr(player->filepath, ==, path);
    g_assert_cmpuint(player->render_layout_generation, ==, layout_generation);
    assert_renderer_protocol(player, FALSE, FALSE, FALSE, TRUE);
    g_assert_false(video_player_is_playing(player));
    g_assert_null(player->worker_thread);
    g_assert_cmpuint(player->timer_id, ==, 0);
    g_assert_true(recovered);
    g_assert_cmpstr(output.output->str, ==, "\033\\\033_Ga=d,d=I,i=42,q=2\033\\\033\\");
    video_player_destroy(player);
    g_string_free(output.output, TRUE);
}

static void test_checked_protocol_cycle_reports_resume_failure(void) {
    VideoPlayer *player = video_player_new(4, FALSE, FALSE, FALSE, TRUE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    g_assert_cmpint(video_player_load(player, write_seek_preview_video_fixture()), ==, ERROR_NONE);
    chafa_canvas_config_set_pixel_mode(player->renderer->canvas_config, CHAFA_PIXEL_MODE_SYMBOLS);
    g_assert_true(video_player_protocol_cycle_needs_redraw(player));
    ParkedWorkerCall worker;
    player->worker_thread = start_parked_worker_for_test("failed-cycle-resume-worker", &worker, player);
    player->is_playing = TRUE;
    DeleteOutput output = {.output = g_string_new(NULL), .fail_all = TRUE};
    FILE *capture = test_output_stream(&output, delete_output_write);
    FILE *recovery = tmpfile();
    g_assert_nonnull(capture);
    g_assert_nonnull(recovery);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    terminal_output_defer_recovery();
    gboolean needs_redraw = TRUE;
    ErrorCode result = video_player_cycle_protocol_checked(player, &needs_redraw);
    stdout = recovery;
    gboolean recovered = terminal_output_flush();
    fclose(capture);
    stdout = saved;
    fclose(recovery);
    g_assert_true(g_atomic_int_get(&worker.finished));
    clear_parked_worker_for_test(&worker);
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_true(needs_redraw);
    g_assert_false(video_player_is_playing(player));
    g_assert_null(player->worker_thread);
    g_assert_cmpuint(player->timer_id, ==, 0);
    g_assert_true(recovered);
    assert_renderer_protocol(player, FALSE, FALSE, FALSE, TRUE);
    video_player_destroy(player);
    g_string_free(output.output, TRUE);
}

static void test_set_renderer_cleanup_failure_preserves_renderer(gconstpointer data) {
    gint mode = GPOINTER_TO_INT(data);
    VideoPlayer *player = video_player_new(4, FALSE, FALSE, TRUE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    g_assert_cmpint(video_player_load(player, write_seek_preview_video_fixture()), ==, ERROR_NONE);
    ImageRenderer *original = player->renderer;
    gboolean owned = !(mode & 2);
    player->owns_renderer = owned;
    ImageRenderer *replacement = mode & 1 ? renderer_create() : NULL;
    if (mode & 1) {
        g_assert_nonnull(replacement);
        replacement->config.force_text = TRUE;
    }
    player->kitty_direct_image_id = 42;
    player->kitty_direct_image_present = TRUE;
    ParkedWorkerCall workers[3];
    player->worker_thread = start_parked_worker_for_test("failed-swap-decode", &workers[0], player);
    player->render_workers[0] = start_parked_worker_for_test("failed-swap-render-0", &workers[1], player);
    player->render_workers[1] = start_parked_worker_for_test("failed-swap-render-1", &workers[2], player);
    player->render_workers_started = TRUE;
    player->is_playing = TRUE;
    player->timer_id = g_timeout_add(10, timeout_source_noop, NULL);
    guint timer_id = player->timer_id;
    DeleteOutput output = {.output = g_string_new(NULL), .delivered_but_failed = TRUE};
    FILE *capture = test_output_stream(&output, delete_output_write);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    ErrorCode result = video_player_set_renderer(player, replacement);
    gboolean recovered = terminal_output_flush();
    fclose(capture);
    stdout = saved;
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_true(player->renderer == original);
    g_assert_cmpint(player->owns_renderer, ==, owned);
    assert_renderer_protocol(player, FALSE, FALSE, FALSE, TRUE);
    g_assert_false(video_player_is_playing(player));
    g_assert_null(player->worker_thread);
    g_assert_false(player->render_workers_started);
    g_assert_null(player->render_workers[0]);
    g_assert_null(player->render_workers[1]);
    g_assert_cmpuint(player->timer_id, ==, 0);
    g_assert_null(g_main_context_find_source_by_id(NULL, timer_id));
    for (guint i = 0; i < G_N_ELEMENTS(workers); i++) {
        g_assert_true(g_atomic_int_get(&workers[i].finished));
        clear_parked_worker_for_test(&workers[i]);
    }
    g_assert_true(recovered);
    g_assert_cmpuint(player->kitty_direct_image_id, ==, 0);
    g_assert_cmpstr(output.output->str, ==, "\033\\\033_Ga=d,d=I,i=42,q=2\033\\\033\\");
    video_player_destroy(player);
    if (!owned) renderer_destroy(original);
    renderer_destroy(replacement);
    g_string_free(output.output, TRUE);
}

static void test_hide_failure_does_not_emit_frame(void) {
    VideoPlayer *player = video_player_new(4, TRUE, FALSE, FALSE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    g_assert_cmpint(video_player_load(player, write_seek_preview_video_fixture()), ==, ERROR_NONE);
    player->kitty_direct_image_id = 42;
    player->kitty_direct_image_present = TRUE;
    VideoFrame *frame = make_test_frame_with_generation(0, player->playback_generation);
    g_string_assign(frame->rendered, "\033_Ga=T,f=24,s=1,v=1;AAAA\033\\");
    frame->pixel_mode = CHAFA_PIXEL_MODE_KITTY;
    video_player_queue_push(player, frame);
    DeleteOutput output = {.output = g_string_new(NULL), .partial = TRUE, .fail_recovery = TRUE,
        .attempted = g_string_new(NULL)};
    FILE *capture = test_output_stream(&output, delete_output_write);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    gboolean rendered = video_player_render_frame_for_test(player);
    terminal_output_flush();
    fclose(capture);
    stdout = saved;
    player->kitty_direct_image_id = 0;
    video_player_destroy(player);
    g_assert_false(rendered);
    const gchar *hide = "\033_Ga=d,d=i,i=42,q=2\033\\";
    const gchar *attempt = strstr(output.attempted->str, hide);
    g_assert_nonnull(attempt);
    g_assert_null(strstr(attempt + strlen(hide), hide));
    g_assert_null(strstr(output.attempted->str, "\033_Ga=T,"));
    g_assert_null(strstr(output.output->str, "\033_Ga=T,"));
    g_string_free(output.attempted, TRUE);
    g_string_free(output.output, TRUE);
}

static void test_gif_delete_failure_discards_buffer(void) {
    GifPlayer *player = gif_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);
    player->kitty_animation_prepared = TRUE;
    player->kitty_animation_id = 42;
    DeleteOutput output = {.output = g_string_new(NULL), .delivered_but_failed = TRUE};
    FILE *capture = test_output_stream(&output, delete_output_write);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    gif_player_stop(player);
    terminal_output_flush();
    fclose(capture);
    stdout = saved;
    gif_player_destroy(player);
    g_assert_cmpstr(output.output->str, ==, "\033\x5c\033_Ga=d,d=I,i=42,q=2\033\x5c\033\x5c");
    g_string_free(output.output, TRUE);
}

static void test_gif_frame_failure_recovers(gconstpointer data) {
    GifPlayer *player = gif_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);
    KittyGraphicsFrame *frame = g_new0(KittyGraphicsFrame, 1);
    frame->command = g_string_new("\033_Ga=T,f=24,s=1,v=1;AAAA\033\\");
    frame->display_width_cells = frame->display_height_cells = 1;
    DeleteOutput output = {.output = g_string_new(NULL), .partial = TRUE};
    FILE *capture = test_output_stream(&output, delete_output_write);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    gboolean success = gif_player_submit_kitty_frame_for_test(player, frame, GPOINTER_TO_INT(data));
    guint attempts = output.attempts;
    fclose(capture);
    stdout = saved;
    gif_player_destroy(player);
    g_assert_false(success);
    g_assert_cmpuint(attempts, >=, 2);
    g_assert_true(g_str_has_suffix(output.output->str, "\033\\"));
    g_string_free(output.output, TRUE);
}

static void test_gif_fallback_waits_for_recovery(void) {
    GifPlayer *player = gif_player_new(4, TRUE, FALSE, FALSE, FALSE, TEXT_SYMBOL_MODE_AUTO, 1.0);
    g_assert_nonnull(player);
    DeleteOutput output = {.output = g_string_new(NULL), .fail_all = TRUE};
    FILE *capture = test_output_stream(&output, delete_output_write);
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    terminal_output_defer_recovery();
    GString *fallback = g_string_new("GIF-FALLBACK");
    gif_player_present_rendered_frame_for_test(player, fallback, 1, 1, FALSE);
    output.fail_all = FALSE;
    fflush(capture);
    gsize premature_length = output.output->len;
    gboolean recovered = terminal_output_flush();
    gif_player_present_rendered_frame_for_test(player, fallback, 1, 1, FALSE);
    fflush(capture);
    fclose(capture);
    stdout = saved;
    gif_player_destroy(player);
    g_assert_cmpuint(premature_length, ==, 0);
    g_assert_true(recovered);
    g_assert_true(g_str_has_prefix(output.output->str, "\033\\"));
    g_assert_nonnull(strstr(output.output->str, "GIF-FALLBACK"));
    g_string_free(output.output, TRUE);
    g_string_free(fallback, TRUE);
}

static void test_direct_kitty_destroy_retries_failed_flush(gconstpointer data) {
    VideoPlayer *player = video_player_new(4, FALSE, FALSE, TRUE, FALSE,
                                           TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    player->kitty_direct_image_id = 42;
    player->kitty_direct_image_present = TRUE;
    DeleteOutput output = {.output = g_string_new(NULL), .partial = GPOINTER_TO_INT(data)};
    FILE *capture = test_output_stream(&output, delete_output_write);
    g_assert_nonnull(capture);
    FILE *saved_stdout = stdout;
    fflush(saved_stdout);
    stdout = capture;
    video_player_destroy(player);
    guint attempts_before_return = output.attempts;
    terminal_output_flush();
    fclose(capture);
    stdout = saved_stdout;
    g_assert_cmpuint(attempts_before_return, ==, 2);
    g_assert_cmpuint(output.attempts, ==, 2);
    if (output.partial) {
        g_assert_true(g_str_has_suffix(output.output->str, "\033\\"));
        g_assert_null(strstr(output.output->str, "\033_Ga=d,d=I,i=42,q=2\033\\"));
    } else {
        g_assert_cmpstr(output.output->str, ==, "\033\\");
    }
    g_string_free(output.output, TRUE);
}
#endif

static void test_direct_kitty_output_errors_and_detach(gconstpointer data) {
    /* 0: delete failure, 1: detach, 2: resume failure, 3: text replacement,
     * 4: destroy, 6: frame failure without an error output parameter. */
    gint mode = GPOINTER_TO_INT(data);
    gboolean frame_failure = mode == 2 || mode == 6;
    VideoPlayer *player = video_player_new(4, FALSE, FALSE, TRUE, FALSE,
                                           TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    g_assert_cmpint(video_player_load(player, write_seek_preview_video_fixture()), ==, ERROR_NONE);
    player->kitty_direct_image_id = 42; /* Simulate an allocated reservation. */
    ImageRenderer *replacement = NULL;
    if (mode == 3) {
        replacement = renderer_create();
        g_assert_nonnull(replacement);
        replacement->config.force_text = TRUE;
        g_assert_cmpint(renderer_initialize(replacement, NULL), ==, ERROR_NONE);
    }
    ParkedWorkerCall worker;
    if (frame_failure) {
        player->worker_thread = start_parked_worker_for_test("failed-output-worker", &worker, player);
        player->is_playing = TRUE;
    }
    gchar *log_path = NULL;
    gint log_fd = g_file_open_tmp("pixelterm-output-errors-XXXXXX.log", &log_path, NULL);
    g_assert_cmpint(log_fd, >=, 0);
    close(log_fd);
    g_test_queue_destroy(remove_path, log_path);
    video_player_debug_reset_for_test();
    pixelterm_env_set_for_test("PIXELTERM_DEBUG_VIDEO", "1");
    pixelterm_env_set_for_test("PIXELTERM_DEBUG_VIDEO_LOG", log_path);
    FILE *capture = tmpfile();
    g_assert_nonnull(capture);
    fflush(stdout);
    gint saved_stdout = dup(STDOUT_FILENO);
    gint failing_fd = open("/dev/null", O_RDONLY);
    FILE *errors = tmpfile();
    g_assert_nonnull(errors);
    fflush(stderr);
    int saved_stderr = dup(STDERR_FILENO);
    g_assert_cmpint(saved_stderr, >=, 0);
    g_assert_cmpint(dup2(fileno(errors), STDERR_FILENO), >=, 0);
    g_assert_cmpint(saved_stdout, >=, 0);
    g_assert_cmpint(failing_fd, >=, 0);
    g_assert_cmpint(dup2(mode == 1 || mode == 3 ? fileno(capture) : failing_fd, STDOUT_FILENO), >=, 0);
    close(failing_fd);
    player->kitty_direct_image_present = !frame_failure;
    gboolean rendered = FALSE;
    if (frame_failure) {
        VideoFrame *frame = make_test_frame_with_generation(0,
            (guint)g_atomic_int_get(&player->playback_generation));
        frame->kitty_direct_image_id = player->kitty_direct_image_id;
        frame->pixel_mode = CHAFA_PIXEL_MODE_KITTY;
        video_player_queue_push(player, frame);
        if (mode == 6) {
            video_player_schedule_tick_for_test(player);
            guint timer_id = player->timer_id;
            GSource *source = g_main_context_find_source_by_id(NULL, timer_id);
            g_assert_nonnull(source);
            g_source_set_ready_time(source, 0);
            g_assert_true(g_main_context_iteration(NULL, FALSE));
            rendered = video_player_is_playing(player);
            g_assert_cmpuint(player->timer_id, ==, 0);
            g_assert_null(g_main_context_find_source_by_id(NULL, timer_id));
            g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);
        } else {
            rendered = video_player_resume_playback_loop_for_test(player) == ERROR_NONE;
        }
    } else if (mode == 1) {
        g_assert_cmpint(video_player_set_renderer(player, NULL), ==, ERROR_NONE);
    } else if (mode == 3) {
        g_assert_cmpint(video_player_set_renderer(player, replacement), ==, ERROR_NONE);
    } else {
        g_assert_cmpint(video_player_stop(player), ==, ERROR_OUTPUT_IO);
    }
    gboolean image_present = player->kitty_direct_image_present;
    gint64 presented_pts = player->last_presented_pts_ms;
    gint64 presented_us = player->last_present_us;
    gboolean workers_stopped = player->worker_thread == NULL && !player->render_workers_started;
    guint32 owned_image_id = player->kitty_direct_image_id;
    if (mode == 4) {
        video_player_destroy(player);
        player = NULL;
    }
    if (mode == 0) {
        g_assert_cmpint(video_player_play(player), ==, ERROR_OUTPUT_IO);
        g_assert_false(video_player_is_playing(player));
        g_assert_null(player->worker_thread);
        g_assert_false(player->render_workers_started);
    }
    g_assert_cmpint(dup2(fileno(capture), STDOUT_FILENO), >=, 0);
    gboolean resumed_output = FALSE;
    if (frame_failure) g_assert_true(terminal_output_ready(g_get_monotonic_time() + 100000));
    if (mode == 0) {
        g_assert_true(terminal_output_flush());
        g_assert_cmpint(ftell(capture), >, 0); /* Cleanup works while the stopped player is alive. */
    }
    if (mode == 4) kitty_graphics_allocate_image_id(); /* A new output owner retries old releases. */
    if (mode == 0) {
        VideoFrame *next = make_test_frame_with_generation(1,
            (guint)g_atomic_int_get(&player->playback_generation));
        video_player_queue_push(player, next);
        player->clock_started = FALSE;
        resumed_output = video_player_render_frame_for_test(player);
        g_assert_true(resumed_output);
        g_assert_cmpuint(player->kitty_direct_image_id, ==, 0);
    }
    if (frame_failure) {
        VideoFrame *next = make_test_frame_with_generation(1,
            (guint)g_atomic_int_get(&player->playback_generation));
        next->kitty_direct_image_id = owned_image_id;
        next->pixel_mode = CHAFA_PIXEL_MODE_KITTY;
        video_player_queue_push(player, next);
        player->clock_started = FALSE;
        resumed_output = video_player_render_frame_for_test(player);
    }
    if (player) video_player_stop(player); /* Retry cleanup after the output recovers. */
    gboolean cleanup_retried = player && !player->kitty_direct_image_present;
    video_player_destroy(player);
    if (frame_failure) clear_parked_worker_for_test(&worker);
    renderer_destroy(replacement);
    fflush(stdout);
    rewind(capture);
    gchar output[4096] = {0};
    gsize output_length = fread(output, 1, sizeof(output) - 1, capture);
    g_assert_cmpuint(output_length, >, 0);
    g_assert_cmpint(ferror(capture), ==, 0);
    g_assert_cmpint(dup2(saved_stdout, STDOUT_FILENO), >=, 0);
    close(saved_stdout);
    fclose(capture);
    video_player_debug_reset_for_test();
    pixelterm_env_reset_for_test();
    if (mode == 0) g_assert_false(image_present);
    fflush(stderr);
    rewind(errors);
    gchar diagnostic[512] = {0};
    gsize diagnostic_length = fread(diagnostic, 1, sizeof(diagnostic) - 1, errors);
    g_assert_cmpint(dup2(saved_stderr, STDERR_FILENO), >=, 0);
    close(saved_stderr);
    fclose(errors);
    if (mode == 0 || mode == 4 || frame_failure) {
        g_assert_cmpuint(diagnostic_length, >, 0);
        g_assert_nonnull(strstr(diagnostic, "Video playback failed: Terminal output failed"));
    }
    if (mode == 1) g_assert_false(image_present);
    if (mode == 3) g_assert_false(image_present);
    if (mode == 0 || mode == 4) {
        g_assert_null(strstr(output, "\033_Ga=d,d=I,i=42,q=2\033\\"));
        g_assert_nonnull(strstr(output, "\033\\"));
    } else {
        g_assert_nonnull(strstr(output, "\033_Ga=d,d=I,i=42,q=2\033\\"));
    }
    if (frame_failure) {
        g_assert_true(g_str_has_prefix(output, "\033\\"));
        g_assert_false(rendered);
        g_assert_true(resumed_output);
        g_assert_true(workers_stopped);
        g_assert_cmpint(presented_pts, ==, G_MININT64);
        g_assert_cmpint(presented_us, ==, 0);
        gchar *log = NULL;
        g_assert_true(g_file_get_contents(log_path, &log, NULL, NULL));
        g_assert_cmpuint(count_debug_event(log, "render-output-error"), ==, 1);
        g_assert_cmpuint(count_debug_event(log, "render-submit"), ==, 1);
        g_assert_null(strstr(log, "render-submit a=0 "));
        g_free(log);
    }
    if (mode != 4) g_assert_true(cleanup_retried);
}

static void test_protocol_cycle_invalidates_old_frames(void) {
    VideoPlayer *player = video_player_new(4, FALSE, FALSE, TRUE, FALSE,
                                           TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    guint generation = (guint)g_atomic_int_get(&player->playback_generation);
    guint layout_generation = player->render_layout_generation;
    video_player_queue_push(player, make_test_frame_with_generation(0, generation));
    video_player_cycle_protocol(player);
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);
    g_assert_cmpuint((guint)g_atomic_int_get(&player->playback_generation), !=, generation);
    g_assert_cmpuint(player->render_layout_generation, !=, layout_generation);
    video_player_queue_insert_sorted(player, make_test_frame_with_generation(1, generation));
    g_assert_cmpuint(video_player_queue_length_for_test(player), ==, 0);
    video_player_destroy(player);
}

static void test_protocol_cycle_restarts_active_workers(void) {
    VideoPlayer *player = video_player_new(4, FALSE, FALSE, TRUE, FALSE,
                                           TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    g_assert_cmpint(video_player_load(player, write_seek_preview_video_fixture()), ==, ERROR_NONE);
    guint old_generation = (guint)g_atomic_int_get(&player->playback_generation);
    VideoFrame *old = make_test_frame_with_generation(0, old_generation);
    old->pixel_mode = CHAFA_PIXEL_MODE_KITTY;
    video_player_queue_push(player, old);
    ParkedWorkerCall call;
    player->worker_thread = start_parked_worker_for_test("protocol-cycle-worker", &call, player);
    player->is_playing = TRUE;
    video_player_cycle_protocol(player); /* Kitty -> text without the key handler's stop/play. */
    g_assert_true(g_atomic_int_get(&call.finished));
    clear_parked_worker_for_test(&call);
    VideoFrame *frame = NULL;
    gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
    while (!frame && g_get_monotonic_time() < deadline) {
        frame = video_player_queue_take_first(player);
        if (!frame) g_usleep(1000);
    }
    video_player_stop(player);
    g_assert_nonnull(frame);
    g_assert_cmpuint(frame->generation, !=, old_generation);
    g_assert_cmpint(frame->pixel_mode, ==, CHAFA_PIXEL_MODE_SYMBOLS);
    video_frame_destroy(frame);
    video_player_destroy(player);
}

static GString *render_fixture_blue_frame(const gchar *path, ImageRenderer *renderer) {
    AVFormatContext *format = NULL;
    g_assert_cmpint(avformat_open_input(&format, path, NULL, NULL), ==, 0);
    g_assert_cmpint(avformat_find_stream_info(format, NULL), >=, 0);
    const AVCodec *codec = NULL;
    gint stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    g_assert_cmpint(stream, >=, 0);
    AVCodecContext *decoder = avcodec_alloc_context3(codec);
    g_assert_nonnull(decoder);
    g_assert_cmpint(avcodec_parameters_to_context(decoder, format->streams[stream]->codecpar), ==, 0);
    g_assert_cmpint(avcodec_open2(decoder, codec, NULL), ==, 0);
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *rgba = av_frame_alloc();
    g_assert_nonnull(packet);
    g_assert_nonnull(frame);
    g_assert_nonnull(rgba);

    /* Decode from the start, independently of the production seek/preview path. */
    for (guint index = 0; index <= 2; index++) {
        gint result;
        while ((result = avcodec_receive_frame(decoder, frame)) == AVERROR(EAGAIN)) {
            g_assert_cmpint(av_read_frame(format, packet), ==, 0);
            g_assert_cmpint(packet->stream_index, ==, stream);
            g_assert_cmpint(avcodec_send_packet(decoder, packet), ==, 0);
            av_packet_unref(packet);
        }
        g_assert_cmpint(result, ==, 0);
        g_assert_cmpint(av_rescale_q(frame->best_effort_timestamp,
                                   format->streams[stream]->time_base,
                                   (AVRational){1, 1000}), ==, index * 1000);
    }
    g_assert_cmpint(frame->width, ==, 2);
    g_assert_cmpint(frame->height, ==, 2);
    rgba->format = AV_PIX_FMT_RGBA;
    rgba->width = frame->width;
    rgba->height = frame->height;
    g_assert_cmpint(av_frame_get_buffer(rgba, 32), ==, 0);
    struct SwsContext *sws = sws_getContext(frame->width, frame->height, frame->format,
        rgba->width, rgba->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    g_assert_nonnull(sws);
    g_assert_cmpint(sws_scale(sws, (const guint8 * const *)frame->data, frame->linesize,
                             0, frame->height, rgba->data, rgba->linesize), ==, rgba->height);
    DecodedFrame decoded = {.pixels = rgba->data[0], .width = rgba->width,
        .height = rgba->height, .rowstride = rgba->linesize[0], .pts_ms = 2000};
    assert_converted_fixture_pixels(&decoded);
    GString *rendered = renderer_render_image_data(renderer, decoded.pixels,
        decoded.width, decoded.height, decoded.rowstride, 4);
    sws_freeContext(sws);
    av_frame_free(&rgba);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&decoder);
    avformat_close_input(&format);
    return rendered;
}

static void test_refresh_paused_frame_submits_without_playing(void) {
    VideoPlayer *player = video_player_new(1, TRUE, FALSE, FALSE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    const gchar *fixture_path = write_seek_preview_video_fixture();
    g_assert_cmpint(video_player_load(player, fixture_path), ==, ERROR_NONE);
    video_player_set_render_area(player, 1, 1, 1, 1, 1, 1);
    player->renderer->config.preserve_aspect_ratio = FALSE;
    player->last_presented_pts_ms = 2100;
    player->fallback_pts_ms = 2100;
    ImageRenderer *expected_renderer = renderer_create();
    g_assert_nonnull(expected_renderer);
    RendererConfig config = player->renderer->config;
    config.max_width = config.max_height = 1;
    g_assert_cmpint(renderer_initialize(expected_renderer, &config), ==, ERROR_NONE);
    /* A backward seek to 2100 ms selects the solid-blue keyframe at 2000 ms. */
    GString *expected = render_fixture_blue_frame(fixture_path, expected_renderer);
    g_assert_nonnull(expected);
    g_assert_cmpuint(expected->len, >, 0);
    FILE *capture = tmpfile();
    g_assert_nonnull(capture);
    FILE *saved = stdout;
    fflush(saved);
    stdout = capture;
    ErrorCode result = video_player_refresh_paused_frame(player);
    fflush(capture);
    stdout = saved;
    rewind(capture);
    gchar bytes[4096] = {0};
    gsize length = fread(bytes, 1, sizeof(bytes) - 1, capture);
    gboolean read_failed = ferror(capture) != 0;
    gboolean playing = video_player_is_playing(player);
    guint timer = player->timer_id;
    gint64 position = video_player_current_position_ms_for_test(player);
    fclose(capture);
    video_player_destroy(player);
    g_assert_cmpint(result, ==, ERROR_NONE);
    g_assert_false(read_failed);
    g_assert_false(playing);
    g_assert_cmpuint(timer, ==, 0);
    g_assert_cmpint(position, ==, 2100);
    g_assert_cmpuint(length, >, expected->len);
    g_assert_nonnull(g_strstr_len(bytes, length, expected->str));
    g_string_free(expected, TRUE);
    renderer_destroy(expected_renderer);
}

void register_video_player_tests(void) {
    g_test_add_func("/video_player/paused_refresh/submits_without_playing", test_refresh_paused_frame_submits_without_playing);
    g_test_add_func("/video_player/seek_relative/paused_output_failure", test_paused_seek_reports_output_failure);
#if defined(__GLIBC__) || defined(__APPLE__)
    g_test_add_data_func("/video_player/set_renderer/cleanup_failure_owned_detach", GINT_TO_POINTER(0), test_set_renderer_cleanup_failure_preserves_renderer);
    g_test_add_data_func("/video_player/set_renderer/cleanup_failure_owned_replace", GINT_TO_POINTER(1), test_set_renderer_cleanup_failure_preserves_renderer);
    g_test_add_data_func("/video_player/set_renderer/cleanup_failure_borrowed_detach", GINT_TO_POINTER(2), test_set_renderer_cleanup_failure_preserves_renderer);
    g_test_add_data_func("/video_player/set_renderer/cleanup_failure_borrowed_replace", GINT_TO_POINTER(3), test_set_renderer_cleanup_failure_preserves_renderer);
    g_test_add_data_func("/video_player/load/cleanup_failure_preserves_media", NULL, test_cleanup_failure_aborts_media_change);
    g_test_add_data_func("/video_player/public_api/protocol_cycle_cleanup_failure", GINT_TO_POINTER(1), test_cleanup_failure_aborts_media_change);
    g_test_add_func("/video_player/public_api/protocol_cycle_resume_failure", test_checked_protocol_cycle_reports_resume_failure);
    g_test_add_func("/video_player/direct_kitty/deferred_cleanup_retry_deadline", test_deferred_cleanup_retry_deadline);
    g_test_add_func("/video_player/output/background_frame_waits_for_restore_deadline",
                    test_background_frame_waits_for_restore_deadline);
    g_test_add_func("/video_player/direct_kitty/discard_failed_delete_buffer", test_failed_delete_buffer_is_discarded);
    g_test_add_func("/video_player/direct_kitty/hide_failure_blocks_frame", test_hide_failure_does_not_emit_frame);
    g_test_add_func("/gif_player/native_animation/delete_failure_discards_buffer", test_gif_delete_failure_discards_buffer);
    g_test_add_data_func("/gif_player/native_animation/root_failure_recovers", GINT_TO_POINTER(1), test_gif_frame_failure_recovers);
    g_test_add_data_func("/gif_player/native_animation/frame_failure_recovers", NULL, test_gif_frame_failure_recovers);
    g_test_add_func("/gif_player/native_animation/fallback_waits_for_recovery", test_gif_fallback_waits_for_recovery);
    g_test_add_data_func("/video_player/direct_kitty/destroy_retries_failed_flush", NULL,
                         test_direct_kitty_destroy_retries_failed_flush);
    g_test_add_data_func("/video_player/direct_kitty/destroy_retries_partial_delete", GINT_TO_POINTER(1),
                         test_direct_kitty_destroy_retries_failed_flush);
#endif
    g_test_add_data_func("/video_player/direct_kitty/retry_failed_delete", GINT_TO_POINTER(0),
                         test_direct_kitty_output_errors_and_detach);
    g_test_add_data_func("/video_player/direct_kitty/destroy_retains_failed_release", GINT_TO_POINTER(4),
                         test_direct_kitty_output_errors_and_detach);
    g_test_add_data_func("/video_player/direct_kitty/detach_cleans_image", GINT_TO_POINTER(1),
                         test_direct_kitty_output_errors_and_detach);
    g_test_add_data_func("/video_player/direct_kitty/failed_frame_not_presented", GINT_TO_POINTER(2),
                         test_direct_kitty_output_errors_and_detach);
    g_test_add_data_func("/video_player/direct_kitty/failed_frame_reports_without_error_out", GINT_TO_POINTER(6),
                         test_direct_kitty_output_errors_and_detach);
    g_test_add_data_func("/video_player/direct_kitty/paused_text_replacement", GINT_TO_POINTER(3),
                         test_direct_kitty_output_errors_and_detach);
    g_test_add_func("/video_player/public_api/protocol_cycle_invalidates_old_frames",
                    test_protocol_cycle_invalidates_old_frames);
    g_test_add_func("/video_player/public_api/protocol_cycle_restarts_active_workers",
                    test_protocol_cycle_restarts_active_workers);
    g_test_add_data_func("/video_player/direct_kitty/worker_explicit_shm",
                         GINT_TO_POINTER(KITTY_TRANSFER_SHM), test_direct_kitty_worker_respects_transfer_mode);
    g_test_add_data_func("/video_player/direct_kitty/worker_explicit_direct",
                         GINT_TO_POINTER(KITTY_TRANSFER_DIRECT), test_direct_kitty_worker_respects_transfer_mode);
    g_test_add_data_func("/video_player/direct_kitty/worker_auto",
                         GINT_TO_POINTER(KITTY_TRANSFER_AUTO), test_direct_kitty_worker_respects_transfer_mode);
    g_test_add_func("/video_player/direct_kitty/image_lifecycle",
                    test_direct_kitty_image_is_replaced_and_cleaned_up);
    g_test_add_func("/video_player/direct_kitty/fallback_boundaries",
                    test_direct_kitty_preserves_fallback_boundaries);
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
    g_test_add_func("/video_player/set_renderer/restarts_workers_when_replacing_during_playback",
                    test_set_renderer_restarts_workers_when_replacing_during_playback);
    g_test_add_func("/video_player/set_renderer/null_stops_playback_and_timer",
                    test_set_renderer_null_stops_playback_and_timer);
    g_test_add_func("/video_player/schedule_tick/skips_when_eof_already_ended",
                    test_schedule_tick_skips_when_eof_already_ended);
    g_test_add_func("/video_player/schedule_tick/skips_when_not_playing",
                    test_schedule_tick_skips_when_not_playing);
    g_test_add_func("/video_player/schedule_tick/skips_without_renderer",
                    test_schedule_tick_skips_without_renderer);
    g_test_add_func("/video_player/seek_relative/after_eof_stops_parked_workers_before_preview",
                    test_seek_relative_after_eof_stops_parked_workers_before_preview);
    g_test_add_func("/video_player/seek_relative/after_eof_real_preview_does_not_crash",
                    test_seek_relative_after_eof_real_preview_does_not_crash);
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
    g_test_add_func("/video_player/render_worker_config/uses_player_color_enhance_without_renderer",
                    test_render_worker_config_uses_player_color_enhance_without_renderer);
    g_test_add_func("/video_player/public_api/layout_accessors_return_consistent_snapshots",
                    test_public_layout_accessors_return_consistent_snapshots);
    g_test_add_func("/video_player/public_api/state_setters_update_player_configuration",
                    test_public_state_setters_update_player_configuration);
    g_test_add_func("/video_player/public_api/protocol_cycle_preserves_existing_order",
                    test_public_protocol_cycle_preserves_existing_order);
    g_test_add_func("/video_player/frame_buffer_size/rejects_overflow",
                    test_frame_buffer_size_rejects_overflow);
    g_test_add_func("/video_player/frame_buffer_size/rejects_non_positive_metadata",
                    test_frame_buffer_size_rejects_non_positive_metadata);
    g_test_add_func("/video_player/rgba_layout/accepts_valid_metadata",
                    test_rgba_layout_accepts_valid_metadata);
    g_test_add_func("/video_player/rgba_layout/rejects_short_rowstride",
                    test_rgba_layout_rejects_short_rowstride);
    g_test_add_func("/video_player/dimensions_within_limits/accepts_max_geometry",
                    test_dimensions_within_limits_accepts_max_geometry);
    g_test_add_func("/video_player/dimensions_within_limits/rejects_large_geometry",
                    test_dimensions_within_limits_rejects_large_geometry);
    g_test_add_func("/video_player/dimensions_within_limits/rejects_non_positive_metadata",
                    test_dimensions_within_limits_rejects_non_positive_metadata);
    g_test_add_func("/video_player/create_sws_context/rejects_invalid_dimensions",
                    test_create_sws_context_rejects_invalid_dimensions);
    g_test_add_func("/video_player/create_sws_context/rejects_invalid_pixel_formats",
                    test_create_sws_context_rejects_invalid_pixel_formats);
    g_test_add_func("/video_player/create_sws_context/accepts_supported_pixel_formats",
                    test_create_sws_context_accepts_supported_pixel_formats);
    g_test_add_func("/video_player/io_interrupt/follows_worker_stop_flag",
                    test_io_interrupt_follows_worker_stop_flag);
    g_test_add_func("/video_player/frame_layout/detects_dynamic_stream_changes",
                    test_frame_layout_detects_dynamic_stream_changes);
    g_test_add_func("/video_player/worker_restart/reclaims_stale_thread_handle",
                    test_worker_restart_reclaims_stale_thread_handle);
    g_test_add_func("/video_player/drop_late_frame/does_not_drop_when_backlog_is_shallow",
                    test_should_not_drop_late_frame_when_backlog_is_shallow);
    g_test_add_func("/video_player/drop_late_frame/does_not_drop_when_backlog_is_medium",
                    test_should_not_drop_late_frame_when_backlog_is_medium);
    g_test_add_func("/video_player/drop_late_frame/drops_when_backlog_is_deep",
                    test_should_drop_late_frame_when_backlog_is_deep);
    g_test_add_data_func("/video_player/decode_worker/skips_conversion_for_late_frames",
                         GINT_TO_POINTER(TRUE), test_decode_worker_conversion_gating);
    g_test_add_data_func("/video_player/decode_worker/converts_frames_without_clock",
                         GINT_TO_POINTER(FALSE), test_decode_worker_conversion_gating);
    g_test_add_data_func("/video_player/decode_worker/converts_frames_after_silence",
                         GINT_TO_POINTER(2), test_decode_worker_conversion_gating);
    g_test_add_data_func("/video_player/decode_worker/converts_frames_before_rewind_resync",
                         GINT_TO_POINTER(3), test_decode_worker_conversion_gating);
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
    g_test_add_func("/video_player/debug_logging/accepts_performance_events",
                    test_debug_logging_accepts_performance_events);
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
