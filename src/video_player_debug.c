#include "video_player_debug_internal.h"

#include <errno.h>
#include <stdlib.h>

#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

static GMutex video_player_debug_mutex;
static FILE *video_player_debug_stream = NULL;

typedef struct {
    gboolean initialized;
    gboolean enabled;
    gchar *path;
} VideoPlayerDebugConfig;

static VideoPlayerDebugConfig video_player_debug_config = {0};

static GMutex video_player_queue_wait_hook_mutex;
static VideoPlayer *video_player_queue_wait_hook_player = NULL;
static VideoPlayerTestQueueKind video_player_queue_wait_hook_kind = VIDEO_PLAYER_TEST_QUEUE_RENDER;
static VideoPlayerQueueWaitHook video_player_queue_wait_hook = NULL;
static void *video_player_queue_wait_hook_data = NULL;

static VideoPlayerSeekHook video_player_seek_hook = NULL;

static void video_player_debug_close_stream_unlocked(void) {
    if (!video_player_debug_stream) {
        return;
    }

    fflush(video_player_debug_stream);
    fclose(video_player_debug_stream);
    video_player_debug_stream = NULL;
}

void video_player_debug_close_stream(void) {
    g_mutex_lock(&video_player_debug_mutex);
    video_player_debug_close_stream_unlocked();
    g_mutex_unlock(&video_player_debug_mutex);
}

static void video_player_debug_register_atexit(void) {
    static gsize registered = 0;

    if (g_once_init_enter(&registered)) {
        atexit(video_player_debug_close_stream);
        g_once_init_leave(&registered, 1);
    }
}

static void video_player_debug_config_init_unlocked(void) {
    if (video_player_debug_config.initialized) {
        return;
    }

    const gchar *enabled_env = g_getenv("PIXELTERM_DEBUG_VIDEO");
    video_player_debug_config.enabled =
        enabled_env && *enabled_env && g_strcmp0(enabled_env, "0") != 0;

    const gchar *path_env = g_getenv("PIXELTERM_DEBUG_VIDEO_LOG");
    if (!path_env || !*path_env) {
        path_env = "/tmp/pixelterm-video.log";
    }
    video_player_debug_config.path = g_strdup(path_env);
    video_player_debug_config.initialized = TRUE;
}

gboolean video_player_debug_enabled(void) {
    g_mutex_lock(&video_player_debug_mutex);
    video_player_debug_config_init_unlocked();
    gboolean enabled = video_player_debug_config.enabled;
    g_mutex_unlock(&video_player_debug_mutex);
    return enabled;
}

static FILE *video_player_debug_get_stream_unlocked(void) {
    video_player_debug_config_init_unlocked();
    if (!video_player_debug_config.enabled) {
        return NULL;
    }

    video_player_debug_register_atexit();
    if (!video_player_debug_stream) {
        video_player_debug_stream = fopen(video_player_debug_config.path, "a");
    }
    return video_player_debug_stream;
}

FILE *video_player_debug_get_stream(void) {
    g_mutex_lock(&video_player_debug_mutex);
    FILE *stream = video_player_debug_get_stream_unlocked();
    g_mutex_unlock(&video_player_debug_mutex);
    return stream;
}

gboolean video_player_debug_has_current_stream_for_test(void) {
    g_mutex_lock(&video_player_debug_mutex);
    gboolean has_stream = (video_player_debug_stream != NULL);
    g_mutex_unlock(&video_player_debug_mutex);
    return has_stream;
}

void video_player_debug_reset_for_test(void) {
    g_mutex_lock(&video_player_debug_mutex);
    video_player_debug_close_stream_unlocked();
    g_clear_pointer(&video_player_debug_config.path, g_free);
    video_player_debug_config.initialized = FALSE;
    video_player_debug_config.enabled = FALSE;
    g_mutex_unlock(&video_player_debug_mutex);
}

static gboolean video_player_debug_should_log(const gchar *event) {
    if (!event) {
        return FALSE;
    }
    return g_strcmp0(event, "play-start") == 0 ||
           g_strcmp0(event, "tick-stop") == 0 ||
           g_strcmp0(event, "tick-reschedule") == 0 ||
           g_strcmp0(event, "worker-eof-rewind") == 0 ||
           g_strcmp0(event, "worker-frame-ready") == 0 ||
           g_strcmp0(event, "worker-decode-time") == 0 ||
           g_strcmp0(event, "worker-render-time") == 0 ||
           g_strcmp0(event, "worker-push") == 0 ||
           g_strcmp0(event, "worker-skip-full") == 0 ||
           g_strcmp0(event, "worker-drop-late") == 0 ||
           g_strcmp0(event, "worker-render-null") == 0 ||
           g_strcmp0(event, "render-first") == 0 ||
           g_strcmp0(event, "render-time") == 0 ||
           g_strcmp0(event, "render-frame") == 0 ||
           g_strcmp0(event, "render-draw-time") == 0 ||
           g_strcmp0(event, "render-wait") == 0;
}

gboolean video_player_debug_should_log_for_test(const gchar *event) {
    return video_player_debug_should_log(event);
}

gboolean video_player_debug_should_write(const gchar *event) {
    g_mutex_lock(&video_player_debug_mutex);
    video_player_debug_config_init_unlocked();
    gboolean should_write = video_player_debug_config.enabled && video_player_debug_should_log(event);
    g_mutex_unlock(&video_player_debug_mutex);
    return should_write;
}

void video_player_debug_write_log(const gchar *event,
                                  gint64 a,
                                  gint64 b,
                                  gint64 c,
                                  gint64 d,
                                  guint backlog,
                                  gint frame_delay,
                                  gint slow_level) {
    g_mutex_lock(&video_player_debug_mutex);
    FILE *stream = video_player_debug_get_stream_unlocked();
    if (!stream) {
        g_mutex_unlock(&video_player_debug_mutex);
        return;
    }

    fprintf(stream,
            "[video-debug] %s a=%lld b=%lld c=%lld d=%lld backlog=%u frame_delay=%d slow=%d\n",
            event ? event : "event",
            (long long)a,
            (long long)b,
            (long long)c,
            (long long)d,
            backlog,
            frame_delay,
            slow_level);
    fflush(stream);
    g_mutex_unlock(&video_player_debug_mutex);
}

void video_player_set_queue_wait_hook_for_test(VideoPlayer *player,
                                               VideoPlayerTestQueueKind queue_kind,
                                               VideoPlayerQueueWaitHook hook,
                                               void *user_data) {
    g_mutex_lock(&video_player_queue_wait_hook_mutex);
    video_player_queue_wait_hook_player = player;
    video_player_queue_wait_hook_kind = queue_kind;
    video_player_queue_wait_hook = hook;
    video_player_queue_wait_hook_data = user_data;
    g_mutex_unlock(&video_player_queue_wait_hook_mutex);
}

void video_player_notify_queue_wait_hook(VideoPlayer *player, VideoPlayerTestQueueKind queue_kind) {
    g_mutex_lock(&video_player_queue_wait_hook_mutex);
    VideoPlayer *hook_player = video_player_queue_wait_hook_player;
    VideoPlayerTestQueueKind hook_kind = video_player_queue_wait_hook_kind;
    VideoPlayerQueueWaitHook hook = video_player_queue_wait_hook;
    void *hook_data = video_player_queue_wait_hook_data;
    g_mutex_unlock(&video_player_queue_wait_hook_mutex);

    if (hook && hook_player == player && hook_kind == queue_kind) {
        hook(hook_data);
    }
}

void video_player_set_seek_hook_for_test(VideoPlayerSeekHook hook) {
    video_player_seek_hook = hook;
}

int video_player_seek_frame_with_test_hook(VideoPlayer *player, int64_t target_ts, int flags) {
    if (!player || !player->format_context) {
        return AVERROR(EINVAL);
    }
    if (video_player_seek_hook) {
        return video_player_seek_hook(player->format_context, player->video_stream_index, target_ts, flags);
    }
    return av_seek_frame(player->format_context, player->video_stream_index, target_ts, flags);
}
