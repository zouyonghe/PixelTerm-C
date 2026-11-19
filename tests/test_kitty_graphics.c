#include <glib.h>
#include <string.h>

#include "kitty_graphics.h"
#include "process_env.h"
#include "terminal_probe.h"
#include "video_player_test_internal.h"
#include <unistd.h>
#include <fcntl.h>

typedef struct {
    gchar reply[256];
    gsize offset;
    gint64 now;
    gint mode;
    guint32 next_id;
    guint32 request_number;
    VideoPlayer *recovering_player;
    gint setattrs;
    struct termios state;
} AllocationTerminal;

static gboolean allocation_tty(gpointer data) { (void)data; return TRUE; }
static int allocation_getattr(struct termios *state, gpointer data) {
    *state = ((AllocationTerminal *)data)->state;
    return 0;
}
static int allocation_setattr(gint action, const struct termios *state, gpointer data) {
    (void)action; (void)state;
    AllocationTerminal *terminal = data;
    terminal->setattrs++;
    if ((terminal->mode == 8 && terminal->setattrs == 2) ||
        (terminal->mode == 10 && terminal->setattrs > 1)) return -1;
    terminal->state = *state;
    return 0;
}
static int allocation_flush(gint queue, gpointer data) { (void)queue; (void)data; return 0; }
static ssize_t allocation_write(const char *buf, size_t length, gpointer data) {
    AllocationTerminal *terminal = data;
    if (terminal->recovering_player) {
        g_assert_null(terminal->recovering_player->worker_thread);
        g_assert_false(terminal->recovering_player->render_workers_started);
    }
    const gchar *number = strstr(buf, "I=");
    g_assert_nonnull(number);
    g_assert_null(strstr(buf, ",i=")); /* Never guess an existing terminal image ID. */
    guint32 request = (guint32)g_ascii_strtoull(number + 2, NULL, 10);
    terminal->request_number = request;
    terminal->offset = 0;
    if (strstr(buf, ",q=1") || strstr(buf, ",q=2"))
        g_strlcpy(terminal->reply, "\033[0n", sizeof(terminal->reply));
    else if (terminal->mode == 3) terminal->reply[0] = '\0';
    else if (terminal->mode == 7) g_strlcpy(terminal->reply, "\033[0n", sizeof(terminal->reply));
    else {
        const gchar *id = terminal->mode == 4 ? "0" :
                          terminal->mode == 5 ? "4294967296" :
                          terminal->mode == 6 ? "-1" : NULL;
        gchar allocated[32];
        g_snprintf(allocated, sizeof(allocated), "%u", terminal->next_id++);
        g_snprintf(terminal->reply, sizeof(terminal->reply),
                   "x\033_GI=%u,i=%s;%s\033\\\033[0n",
                   terminal->mode == 2 ? request + 1 : request,
                   id ? id : allocated, terminal->mode == 1 ? "ERROR" : "OK");
    }
    return terminal->mode == 9 ? (ssize_t)length / 2 : (ssize_t)length;
}
static gint allocation_read(gint timeout, gpointer data) {
    AllocationTerminal *terminal = data;
    if (terminal->reply[terminal->offset]) return (guchar)terminal->reply[terminal->offset++];
    terminal->now += (gint64)MAX(timeout, 1) * 1000;
    return TERMINAL_PROBE_READ_TIMEOUT;
}
static gint64 allocation_time(gpointer data) { return ((AllocationTerminal *)data)->now; }
static const TerminalProbeTransportHooks allocation_hooks = {
    .stdin_is_tty = allocation_tty, .stdout_is_tty = allocation_tty,
    .tcgetattr_fn = allocation_getattr, .tcsetattr_fn = allocation_setattr,
    .tcflush_fn = allocation_flush, .write_fn = allocation_write,
    .read_char_with_timeout_fn = allocation_read, .monotonic_time_us_fn = allocation_time,
};

static void test_kitty_allocates_terminal_ids_and_preserves_keys(void) {
    AllocationTerminal terminal = {.next_id = 123};
    terminal_probe_set_transport_hooks_for_test(&allocation_hooks, &terminal);
    g_assert_cmpuint(kitty_graphics_allocate_image_id(), ==, 123);
    g_assert_cmpint(terminal_probe_take_pending_byte(), ==, 'x');
    g_assert_cmpuint(kitty_graphics_allocate_image_id(), ==, 124);
    g_assert_cmpint(terminal_probe_take_pending_byte(), ==, 'x');
    terminal_probe_reset_transport_hooks_for_test();
}

static void test_kitty_allocation_rejects_bad_replies(void) {
    for (gint mode = 1; mode <= 9; mode++) {
        AllocationTerminal terminal = {.next_id = 123, .mode = mode};
        terminal_probe_set_transport_hooks_for_test(&allocation_hooks, &terminal);
        FILE *capture = tmpfile();
        g_assert_nonnull(capture);
        fflush(stdout);
        gint saved = dup(STDOUT_FILENO);
        g_assert_cmpint(saved, >=, 0);
        g_assert_cmpint(dup2(fileno(capture), STDOUT_FILENO), >=, 0);
        guint32 id = kitty_graphics_allocate_image_id();
        if (mode == 8) g_assert_cmpint(terminal.setattrs, ==, 3);
        fflush(stdout);
        rewind(capture);
        gchar output[256] = {0};
        gsize output_length = fread(output, 1, sizeof(output) - 1, capture);
        g_assert_cmpuint(output_length, >, 0);
        g_assert_cmpint(ferror(capture), ==, 0);
        g_assert_cmpint(dup2(saved, STDOUT_FILENO), >=, 0);
        close(saved);
        fclose(capture);
        g_assert_cmpuint(id, ==, 0);
        if (mode == 8) {
            g_assert_cmpstr(output, ==, "\033\\\033_Ga=d,d=I,i=123,q=2\033\\");
        } else {
            g_assert_cmpstr(output, ==, "\033\\"); /* Never guess ownership from a shared image number. */
        }
        terminal_probe_reset_transport_hooks_for_test();
    }
}

static void test_probe_persistent_restore_failure_recovers(void) {
    AllocationTerminal terminal = {.next_id = 123, .mode = 10};
    terminal.state.c_lflag = ICANON | ECHO;
    terminal.state.c_cc[VMIN] = 1;
    terminal_probe_set_transport_hooks_for_test(&allocation_hooks, &terminal);
    char response[256];
    gssize result = terminal_probe_query_response("\033_Ga=t,I=123;AAAAAA==\033\\\033[5n",
        120, 20, 'n', response, sizeof(response));
    g_assert_cmpint(result, ==, -1);
    g_assert_cmpint(terminal.state.c_lflag & ICANON, ==, 0);
    g_assert_false(terminal_probe_retry_restore());
    terminal.mode = 0;
    g_assert_true(terminal_probe_retry_restore());
    g_assert_cmpint(terminal.state.c_lflag, ==, ICANON | ECHO);
    g_assert_cmpint(terminal.state.c_cc[VMIN], ==, 1);
    terminal_probe_reset_transport_hooks_for_test();
}

static gpointer allocation_stale_worker(gpointer data) {
    (void)data;
    return NULL;
}

static void test_outer_restore_replaces_pending_raw_snapshot(void) {
    AllocationTerminal terminal = {.next_id = 123, .mode = 10};
    terminal.state.c_cc[VMIN] = 0; /* The probe begins during app raw mode. */
    terminal_probe_set_transport_hooks_for_test(&allocation_hooks, &terminal);
    char response[256];
    g_assert_cmpint(terminal_probe_query_response("\033_Ga=t,I=123;AAAAAA==\033\\\033[5n",
        120, 20, 'n', response, sizeof(response)), ==, -1);
    struct termios original = terminal.state;
    original.c_lflag = ICANON | ECHO | IEXTEN;
    original.c_cc[VMIN] = 1;
    g_assert_false(terminal_probe_restore_original(&original));
    terminal.mode = 0;
    g_assert_true(terminal_probe_retry_restore());
    g_assert_cmpint(terminal.state.c_lflag, ==, original.c_lflag);
    g_assert_cmpint(terminal.state.c_cc[VMIN], ==, 1);
    terminal_probe_reset_transport_hooks_for_test();
}

static void test_kitty_shm_playback_skips_direct_allocation(void) {
    AllocationTerminal terminal = {.next_id = 123};
    terminal_probe_set_transport_hooks_for_test(&allocation_hooks, &terminal);
    pixelterm_env_unset_for_test("TMUX");
    pixelterm_env_unset_for_test("STY");
    VideoPlayer *player = video_player_new(4, FALSE, FALSE, TRUE, FALSE,
                                           TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_AUTO);
    g_assert_nonnull(player);
    player->kitty_shm_enabled = TRUE;
    g_assert_cmpint(video_player_load(player, write_seek_preview_video_fixture()), ==, ERROR_NONE);
    FILE *capture = tmpfile();
    g_assert_nonnull(capture);
    fflush(stdout);
    gint saved = dup(STDOUT_FILENO);
    g_assert_cmpint(saved, >=, 0);
    g_assert_cmpint(dup2(fileno(capture), STDOUT_FILENO), >=, 0);
    g_assert_cmpint(video_player_play(player), ==, ERROR_NONE);
    video_player_stop(player);
    video_player_destroy(player);
    fflush(stdout);
    g_assert_cmpint(dup2(saved, STDOUT_FILENO), >=, 0);
    close(saved);
    fclose(capture);
    terminal_probe_reset_transport_hooks_for_test();
    pixelterm_env_reset_for_test();
    g_assert_cmpuint(terminal.request_number, ==, 0);
}

static void test_kitty_failed_allocation_cleanup_recovers(void) {
    AllocationTerminal terminal = {.next_id = 123, .mode = 3};
    terminal_probe_set_transport_hooks_for_test(&allocation_hooks, &terminal);
    FILE *capture = tmpfile();
    g_assert_nonnull(capture);
    fflush(stdout);
    gint saved = dup(STDOUT_FILENO);
    gint failed = open("/dev/null", O_RDONLY);
    g_assert_cmpint(saved, >=, 0);
    g_assert_cmpint(failed, >=, 0);
    g_assert_cmpint(dup2(failed, STDOUT_FILENO), >=, 0);
    close(failed);
    guint32 first = kitty_graphics_allocate_image_id();
    guint32 request = terminal.request_number;
    guint32 blocked = kitty_graphics_allocate_image_id();
    guint32 request_after_blocked = terminal.request_number;
    g_assert_cmpint(dup2(fileno(capture), STDOUT_FILENO), >=, 0);
    terminal.mode = 0;
    guint32 second = kitty_graphics_allocate_image_id();
    fflush(stdout);
    rewind(capture);
    gchar output[256] = {0};
    gsize length = fread(output, 1, sizeof(output) - 1, capture);
    g_assert_cmpint(dup2(saved, STDOUT_FILENO), >=, 0);
    close(saved);
    fclose(capture);
    terminal_probe_reset_transport_hooks_for_test();
    /* Assertions after stdout restore so g_test output is visible on failure. */
    g_assert_cmpuint(request_after_blocked, ==, request);
    g_assert_cmpuint(first, ==, 0);
    g_assert_cmpuint(blocked, ==, 0);
    g_assert_cmpuint(second, ==, 123);
    g_assert_cmpuint(length, >, 0);
    /* Some stdio implementations retain failed buffered writes across dup2. */
    g_assert_cmpuint(length % 2, ==, 0);
    for (gsize i = 0; i < length; i += 2) {
        g_assert_cmpmem(output + i, 2, "\033\\", 2);
    }
}

static void test_kitty_player_uses_and_releases_terminal_reservation(gconstpointer data) {
    AllocationTerminal terminal = {.next_id = 123};
    terminal_probe_set_transport_hooks_for_test(&allocation_hooks, &terminal);
    pixelterm_env_unset_for_test("TMUX");
    pixelterm_env_unset_for_test("STY");
    VideoPlayer *player = video_player_new(4, FALSE, FALSE, TRUE, FALSE,
                                           TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    g_assert_cmpuint(player->kitty_direct_image_id, ==, 0);
    g_assert_cmpint(video_player_load(player, write_seek_preview_video_fixture()), ==, ERROR_NONE);
    if (GPOINTER_TO_INT(data)) {
        player->renderer->config.force_kitty = FALSE;
        chafa_canvas_config_set_pixel_mode(player->renderer->canvas_config, CHAFA_PIXEL_MODE_KITTY);
    }
    player->worker_thread = g_thread_new("allocation-stale-worker", allocation_stale_worker, NULL);
    g_atomic_int_set(&player->worker_stop, TRUE);
    terminal.recovering_player = player;
    gchar *log_path = NULL;
    gint fd = g_file_open_tmp("pixelterm-reservation-XXXXXX.log", &log_path, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(fd);
    video_player_debug_reset_for_test();
    pixelterm_env_set_for_test("PIXELTERM_DEBUG_VIDEO", "1");
    pixelterm_env_set_for_test("PIXELTERM_DEBUG_VIDEO_LOG", log_path);
    FILE *capture = tmpfile();
    g_assert_nonnull(capture);
    fflush(stdout);
    gint saved = dup(STDOUT_FILENO);
    g_assert_cmpint(saved, >=, 0);
    g_assert_cmpint(dup2(fileno(capture), STDOUT_FILENO), >=, 0);
    ErrorCode first_play = video_player_play(player);
    guint32 first_id = player->kitty_direct_image_id;
    gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
    while (player->last_presented_pts_ms == G_MININT64 && g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    gboolean presented = player->last_presented_pts_ms != G_MININT64;
    video_player_pause(player);
    guint32 paused_id = player->kitty_direct_image_id;
    ErrorCode resumed = video_player_play(player);
    guint32 resumed_id = player->kitty_direct_image_id;
    guint32 next_id = terminal.next_id;
    gchar padding[8192];
    memset(padding, ' ', sizeof(padding));
    gsize padding_written = fwrite(padding, 1, sizeof(padding), stdout);
    video_player_stop(player);
    guint32 stopped_id = player->kitty_direct_image_id;
    video_player_destroy(player);
    fflush(stdout);
    g_assert_cmpint(fseek(capture, 0, SEEK_END), ==, 0);
    long capture_length = ftell(capture);
    g_assert_cmpint(capture_length, >, 0);
    rewind(capture);
    gchar *output = g_malloc0((gsize)capture_length + 1);
    gsize output_length = fread(output, 1, (gsize)capture_length, capture);
    g_assert_cmpuint(output_length, ==, (gsize)capture_length);
    g_assert_cmpuint(output_length, >, 0);
    g_assert_cmpint(ferror(capture), ==, 0);
    g_assert_cmpint(dup2(saved, STDOUT_FILENO), >=, 0);
    close(saved);
    fclose(capture);
    video_player_debug_reset_for_test();
    pixelterm_env_reset_for_test();
    terminal_probe_reset_transport_hooks_for_test();
    g_assert_cmpint(first_play, ==, ERROR_NONE);
    g_assert_cmpuint(first_id, ==, 123);
    g_assert_cmpuint(paused_id, ==, 123);
    g_assert_cmpint(resumed, ==, ERROR_NONE);
    g_assert_cmpuint(resumed_id, ==, 123);
    g_assert_cmpuint(next_id, ==, 124);
    g_assert_cmpuint(stopped_id, ==, 0);
    g_assert_cmpuint(padding_written, ==, sizeof(padding));
    g_assert_true(presented);
    const gchar *frame = strstr(output, "\033_Ga=T,f=24,");
    g_assert_nonnull(frame);
    const gchar *control_end = strchr(frame, ';');
    const gchar *frame_id = strstr(frame, ",i=123,");
    g_assert_nonnull(control_end);
    g_assert_nonnull(frame_id);
    g_assert_true(frame_id < control_end);
    g_assert_nonnull(strstr(output, "\033_Ga=d,d=I,i=123,q=2\033\\"));
    g_free(output);
    gchar *log = NULL;
    g_assert_true(g_file_get_contents(log_path, &log, NULL, NULL));
    const gchar *start = strstr(log, "[video-debug] play-start ");
    const gchar *first = strstr(log, "[video-debug] render-first ");
    g_assert_nonnull(start);
    g_assert_nonnull(first);
    g_assert_true(start < first);
    g_free(log);
    remove(log_path);
    g_free(log_path);
}

static void test_kitty_graphics_shm_command_contains_expected_controls(void) {
    GString *command = kitty_graphics_build_shm_command("/pixelterm-test", 16, 8, 4, 2, 512);
    g_assert_nonnull(command);
    g_assert_true(g_str_has_prefix(command->str, "\033_G"));
    g_assert_nonnull(strstr(command->str, "a=T"));
    g_assert_nonnull(strstr(command->str, "f=32"));
    g_assert_nonnull(strstr(command->str, "s=16"));
    g_assert_nonnull(strstr(command->str, "v=8"));
    g_assert_nonnull(strstr(command->str, "t=s"));
    g_assert_nonnull(strstr(command->str, "S=512"));
    g_assert_nonnull(strstr(command->str, "c=4"));
    g_assert_nonnull(strstr(command->str, "r=2"));
    g_assert_nonnull(strstr(command->str, "C=1"));
    g_assert_nonnull(strstr(command->str, "q=2"));
    g_assert_true(g_str_has_suffix(command->str, "\033\\"));
    g_string_free(command, TRUE);
}

static void test_kitty_graphics_shm_command_rejects_invalid_input(void) {
    g_assert_null(kitty_graphics_build_shm_command(NULL, 16, 8, 4, 2, 512));
    g_assert_null(kitty_graphics_build_shm_command("/x", 0, 8, 4, 2, 512));
    g_assert_null(kitty_graphics_build_shm_command("/x", 16, 8, 0, 2, 512));
    g_assert_null(kitty_graphics_build_shm_command("/x", 16, 8, 4, 2, 0));
}

static void test_kitty_graphics_direct_rgb_command_roundtrips_chunks(void) {
    const gsize pixel_bytes = 6000;
    guint8 *pixels = g_malloc(pixel_bytes);
    for (gsize i = 0; i < pixel_bytes; i++) {
        pixels[i] = (guint8)(i * 17);
    }

    KittyGraphicsFrame *frame = kitty_graphics_frame_new_direct_rgb(
        pixels, 1000, 2, 3000, 64, 18, 42);
    g_assert_nonnull(frame);
    g_assert_null(frame->shm_name);
    g_assert_cmpint(frame->display_width_cells, ==, 64);
    g_assert_cmpint(frame->display_height_cells, ==, 18);
    g_assert_nonnull(strstr(frame->command->str, "a=T,f=24,s=1000,v=2,c=64,r=18,i=42,C=1,m=1,q=2;"));

    GByteArray *decoded = g_byte_array_new();
    gchar **commands = g_strsplit(frame->command->str, "\033\\", -1);
    guint chunks = 0;
    for (gsize i = 0; commands[i] && commands[i][0]; i++) {
        const gchar *separator = strchr(commands[i], ';');
        g_assert_nonnull(separator);
        gsize encoded_len = strlen(separator + 1);
        g_assert_cmpuint(encoded_len, <=, 4096);
        if (i + 1 < g_strv_length(commands) - 1) {
            g_assert_cmpuint(encoded_len % 4, ==, 0);
        }
        gsize chunk_len = 0;
        guchar *chunk = g_base64_decode(separator + 1, &chunk_len);
        g_byte_array_append(decoded, chunk, chunk_len);
        g_free(chunk);
        chunks++;
        if (i == 0) {
            g_assert_nonnull(strstr(commands[i], "m=1"));
        } else {
            g_assert_true(g_str_has_prefix(commands[i], "\033_Gm="));
        }
    }
    g_assert_cmpuint(chunks, ==, 2);
    g_assert_true(g_str_has_prefix(commands[1], "\033_Gm=0;"));
    g_assert_cmpuint(decoded->len, ==, pixel_bytes);
    g_assert_cmpmem(decoded->data, decoded->len, pixels, pixel_bytes);

    g_byte_array_unref(decoded);
    g_strfreev(commands);
    kitty_graphics_frame_free(frame);
    g_free(pixels);
}

static void test_kitty_graphics_direct_rgb_rejects_oversized_layout(void) {
    guint8 pixel[3] = {1, 2, 3};
    g_assert_null(kitty_graphics_frame_new_direct_rgb(pixel, 100000, 100000,
                                                      300000, 1, 1, 42));
    g_assert_null(kitty_graphics_frame_new_direct_rgb(pixel, 2, 1, 3, 1, 1, 42));
    g_assert_null(kitty_graphics_frame_new_direct_rgb(NULL, 1, 1, 3, 1, 1, 42));
    g_assert_null(kitty_graphics_frame_new_direct_rgb(pixel, 1, 1, 3, 1, 1, 0));
    g_assert_null(kitty_graphics_frame_new_direct_rgb(pixel, 4096, 4096, 12288, 1, 1, 42));
}

static void test_kitty_graphics_direct_rgb_omits_row_padding(void) {
    const guint8 pixels[] = {1, 2, 3, 99, 4, 5, 6, 99};
    KittyGraphicsFrame *frame = kitty_graphics_frame_new_direct_rgb(pixels, 1, 2, 4, 1, 2, 42);
    g_assert_nonnull(frame);
    g_assert_cmpstr(frame->command->str, ==,
                    "\033_Ga=T,f=24,s=1,v=2,c=1,r=2,i=42,C=1,m=0,q=2;AQIDBAUG\033\\");
    kitty_graphics_frame_free(frame);
}

static void test_kitty_direct_size_preserves_aspect_and_budget(void) {
    gint width = 0, height = 0;
    kitty_graphics_get_direct_size(120, 1000, 100, 100, &width, &height);
    g_assert_cmpint(width, ==, 12);
    g_assert_cmpint(height, ==, 100);
    kitty_graphics_get_direct_size(1000, 120, 100, 100, &width, &height);
    g_assert_cmpint(width, ==, 100);
    g_assert_cmpint(height, ==, 12);
    kitty_graphics_get_direct_size(4096, 4096, 0, 0, &width, &height);
    g_assert_cmpint(width, ==, height);
    g_assert_cmpint(width, ==, 1672);
    g_assert_cmpuint((gsize)width * height * 3, <=, KITTY_GRAPHICS_DIRECT_MAX_BYTES);
    kitty_graphics_get_direct_size(16, 8, 100, 100, &width, &height);
    g_assert_cmpint(width, ==, 16);
    g_assert_cmpint(height, ==, 8);
    kitty_graphics_get_direct_size(1, 16000000, 0, 0, &width, &height);
    g_assert_cmpint(width, ==, 1);
    g_assert_cmpuint((gsize)width * height * 3, <=, KITTY_GRAPHICS_DIRECT_MAX_BYTES);
    const gint dimensions[] = {1, 2, 3, 4096, 16000000, G_MAXINT};
    for (guint x = 0; x < G_N_ELEMENTS(dimensions); x++) {
        for (guint y = 0; y < G_N_ELEMENTS(dimensions); y++) {
            kitty_graphics_get_direct_size(dimensions[x], dimensions[y], 0, 0, &width, &height);
            g_assert_cmpint(width, >, 0);
            g_assert_cmpint(height, >, 0);
            g_assert_cmpuint((guint64)width * height * 3, <=, KITTY_GRAPHICS_DIRECT_MAX_BYTES);
        }
    }
}

static void test_kitty_graphics_animation_commands(void) {
    GString *root = kitty_graphics_build_animation_root_shm_command(
        "/pixelterm-root", 16, 8, 4, 2, 512, 42);
    GString *frame = kitty_graphics_build_animation_frame_shm_command(
        "/pixelterm-frame", 16, 8, 512, 42, 80);
    GString *root_delay = kitty_graphics_build_animation_frame_delay_command(42, 1, 120);
    GString *loading = kitty_graphics_build_animation_loading_command(42);
    GString *play = kitty_graphics_build_animation_play_command(42);
    GString *stop = kitty_graphics_build_animation_stop_command(42);
    GString *delete_image = kitty_graphics_build_delete_image_command(42);

    g_assert_cmpstr(root->str, ==,
                    "\033_Ga=T,f=32,s=16,v=8,t=s,S=512,c=4,r=2,C=1,i=42,q=2;L3BpeGVsdGVybS1yb290\033\\");
    g_assert_cmpstr(frame->str, ==,
                    "\033_Ga=f,f=32,s=16,v=8,t=s,S=512,i=42,z=80,X=1,q=2;L3BpeGVsdGVybS1mcmFtZQ==\033\\");
    g_assert_cmpstr(root_delay->str, ==, "\033_Ga=a,i=42,r=1,z=120,q=2\033\\");
    g_assert_cmpstr(loading->str, ==, "\033_Ga=a,i=42,s=2,q=2\033\\");
    g_assert_cmpstr(play->str, ==, "\033_Ga=a,i=42,s=3,v=1,q=2\033\\");
    g_assert_cmpstr(stop->str, ==, "\033_Ga=a,i=42,s=1,q=2\033\\");
    g_assert_cmpstr(delete_image->str, ==, "\033_Ga=d,d=I,i=42,q=2\033\\");

    g_string_free(root, TRUE);
    g_string_free(frame, TRUE);
    g_string_free(root_delay, TRUE);
    g_string_free(loading, TRUE);
    g_string_free(play, TRUE);
    g_string_free(stop, TRUE);
    g_string_free(delete_image, TRUE);
}

static void test_kitty_graphics_animation_commands_reject_invalid_input(void) {
    g_assert_null(kitty_graphics_build_animation_root_shm_command(
        NULL, 16, 8, 4, 2, 512, 42));
    g_assert_null(kitty_graphics_build_animation_root_shm_command(
        "/x", 16, 8, 4, 2, 512, 0));
    g_assert_null(kitty_graphics_build_animation_frame_shm_command(
        "/x", 16, 8, 512, 0, 80));
    g_assert_null(kitty_graphics_build_animation_frame_shm_command(
        "/x", 16, 8, 512, 42, 0));
    g_assert_null(kitty_graphics_build_animation_frame_delay_command(42, 0, 80));
    g_assert_null(kitty_graphics_build_animation_frame_delay_command(42, 1, 0));
    g_assert_null(kitty_graphics_build_animation_loading_command(0));
    g_assert_null(kitty_graphics_build_animation_play_command(0));
    g_assert_null(kitty_graphics_build_animation_stop_command(0));
    g_assert_null(kitty_graphics_build_delete_image_command(0));
}

static void test_kitty_graphics_animation_shm_frames(void) {
#ifdef __ANDROID__
    g_test_skip("POSIX shared memory unavailable on Android");
#else
    const guint8 pixels[4] = {255, 0, 0, 255};
    KittyGraphicsFrame *root = kitty_graphics_animation_root_new_shm_rgba(
        pixels, 1, 1, 4, 1, 1, 42);
    KittyGraphicsFrame *frame = kitty_graphics_animation_frame_new_shm_rgba(
        pixels, 1, 1, 4, 1, 1, 42, 80);

    if (!root || !frame) {
        kitty_graphics_frame_free(root);
        kitty_graphics_frame_free(frame);
        g_test_skip("native Kitty SHM unavailable");
        return;
    }
    g_assert_nonnull(root);
    g_assert_nonnull(strstr(root->command->str, "a=T"));
    g_assert_nonnull(strstr(root->command->str, "i=42"));
    g_assert_nonnull(frame);
    g_assert_nonnull(strstr(frame->command->str, "a=f"));
    g_assert_nonnull(strstr(frame->command->str, "i=42"));
    g_assert_nonnull(strstr(frame->command->str, "z=80"));

    kitty_graphics_frame_free(root);
    kitty_graphics_frame_free(frame);
#endif
}

static void test_kitty_graphics_shm_auto_enabled_rejects_remote_context(void) {
    pixelterm_env_unset_for_test("PIXELTERM_KITTY_SHM");
    pixelterm_env_set_for_test("SSH_CONNECTION", "host 1 host 2");
    pixelterm_env_set_for_test("TERM", "xterm-kitty");

    g_assert_false(kitty_graphics_shm_auto_enabled());

    pixelterm_env_reset_for_test();
}

static void test_kitty_graphics_shm_auto_enabled_allows_explicit_override(void) {
    pixelterm_env_set_for_test("PIXELTERM_KITTY_SHM", "1");
    pixelterm_env_set_for_test("SSH_CONNECTION", "host 1 host 2");
    pixelterm_env_set_for_test("TERM", "xterm-256color");

#ifdef __ANDROID__
    g_assert_false(kitty_graphics_shm_auto_enabled());
#else
    g_assert_true(kitty_graphics_shm_auto_enabled());
#endif

    pixelterm_env_reset_for_test();
}

static void test_kitty_graphics_shm_is_disabled_on_android(void) {
#ifdef __ANDROID__
    pixelterm_env_set_for_test("PIXELTERM_KITTY_SHM", "1");
    pixelterm_env_set_for_test("TERM", "xterm-kitty");

    g_assert_false(kitty_graphics_shm_auto_enabled());
    g_assert_false(kitty_graphics_should_use_shm(KITTY_TRANSFER_SHM));
    g_assert_false(kitty_graphics_should_use_shm(KITTY_TRANSFER_AUTO));

    pixelterm_env_reset_for_test();
#endif
}

static void test_kitty_graphics_frame_rejects_rowstride_overflow(void) {
    guint8 pixel = 0;

    g_assert_null(kitty_graphics_frame_new_shm_rgba(&pixel,
                                                    G_MAXINT,
                                                    1,
                                                    G_MAXINT,
                                                    1,
                                                    1));
}

static void test_kitty_graphics_frame_rejects_large_payload(void) {
    guint8 pixel[4] = {0, 0, 0, 255};

    g_assert_null(kitty_graphics_frame_new_shm_rgba(pixel,
                                                    4097,
                                                    512,
                                                    4097 * 4,
                                                    4097,
                                                    512));
}

void register_kitty_graphics_tests(void) {
    g_test_add_func("/kitty_graphics/allocation/terminal_ids_and_keys", test_kitty_allocates_terminal_ids_and_preserves_keys);
    g_test_add_func("/kitty_graphics/allocation/bad_replies", test_kitty_allocation_rejects_bad_replies);
    g_test_add_func("/kitty_graphics/allocation/persistent_restore_recovery", test_probe_persistent_restore_failure_recovers);
    g_test_add_func("/kitty_graphics/allocation/outer_restore_replaces_raw_snapshot", test_outer_restore_replaces_pending_raw_snapshot);
    g_test_add_func("/kitty_graphics/allocation/failed_cleanup_recovers", test_kitty_failed_allocation_cleanup_recovers);
    g_test_add_func("/kitty_graphics/allocation/shm_skips_direct", test_kitty_shm_playback_skips_direct_allocation);
    g_test_add_data_func("/kitty_graphics/allocation/player_lifecycle", NULL, test_kitty_player_uses_and_releases_terminal_reservation);
    g_test_add_data_func("/kitty_graphics/allocation/effective_kitty_lifecycle", GINT_TO_POINTER(1), test_kitty_player_uses_and_releases_terminal_reservation);
    g_test_add_func("/kitty_graphics/direct_rgb/aspect_and_budget",
                    test_kitty_direct_size_preserves_aspect_and_budget);
    g_test_add_func("/kitty_graphics/shm_command/controls", test_kitty_graphics_shm_command_contains_expected_controls);
    g_test_add_func("/kitty_graphics/shm_command/rejects_invalid_input", test_kitty_graphics_shm_command_rejects_invalid_input);
    g_test_add_func("/kitty_graphics/direct_rgb/roundtrips_chunks",
                    test_kitty_graphics_direct_rgb_command_roundtrips_chunks);
    g_test_add_func("/kitty_graphics/direct_rgb/rejects_oversized_layout",
                    test_kitty_graphics_direct_rgb_rejects_oversized_layout);
    g_test_add_func("/kitty_graphics/direct_rgb/omits_row_padding",
                    test_kitty_graphics_direct_rgb_omits_row_padding);
    g_test_add_func("/kitty_graphics/animation/commands", test_kitty_graphics_animation_commands);
    g_test_add_func("/kitty_graphics/animation/rejects_invalid_input", test_kitty_graphics_animation_commands_reject_invalid_input);
    g_test_add_func("/kitty_graphics/animation/shm_frames", test_kitty_graphics_animation_shm_frames);
    g_test_add_func("/kitty_graphics/shm_auto/rejects_remote_context", test_kitty_graphics_shm_auto_enabled_rejects_remote_context);
    g_test_add_func("/kitty_graphics/shm_auto/allows_explicit_override", test_kitty_graphics_shm_auto_enabled_allows_explicit_override);
    g_test_add_func("/kitty_graphics/shm_auto/disabled_on_android", test_kitty_graphics_shm_is_disabled_on_android);
    g_test_add_func("/kitty_graphics/frame/rejects_rowstride_overflow", test_kitty_graphics_frame_rejects_rowstride_overflow);
    g_test_add_func("/kitty_graphics/frame/rejects_large_payload", test_kitty_graphics_frame_rejects_large_payload);
}
