#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "video_player.h"
#include "terminal_output.h"
#include "test_output_stream.h"
#include <errno.h>
#include <string.h>

static ssize_t failed_area_write(void *data, const char *bytes, size_t length) {
    if (data) g_string_append_len(data, bytes, length);
    errno = EIO;
    return -1;
}

static void test_failed_area_clear_invalidates_cache(gconstpointer delivered) {
    VideoPlayer *player = video_player_new(1, TRUE, FALSE, FALSE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    player->last_frame_top_row = 4;
    player->last_frame_height = 1;
    player->last_frame_lines = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(player->last_frame_lines, g_strdup("cached row"));
    GString *attempted = g_string_new(NULL);
    FILE *stream = test_output_stream(delivered ? attempted : NULL, failed_area_write);
    FILE *recovery = tmpfile();
    g_assert_nonnull(stream);
    g_assert_nonnull(recovery);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    ErrorCode result = video_player_clear_render_area(player);
    gchar *cached = video_player_dup_cached_line_at_row(player, 4);
    stdout = recovery;
    gboolean recovered = terminal_output_flush();
    rewind(recovery);
    char bytes[8];
    gsize length = fread(bytes, 1, sizeof(bytes), recovery);
    fclose(stream);
    stdout = saved;
    fclose(recovery);
    video_player_destroy(player);
    g_assert_null(cached);
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_true(recovered);
    g_assert_cmpmem(bytes, length, "\033\\", 2);
    g_assert_cmpstr(attempted->str, ==, delivered ? "\033[4;1H\033[2K\033\\" : "");
    g_string_free(attempted, TRUE);
}

void register_video_output_tests(void) {
    g_test_add_data_func("/video_player/output/failed_clear_invalidates_cache", NULL, test_failed_area_clear_invalidates_cache);
    g_test_add_data_func("/video_player/output/delivered_clear_failure_invalidates_cache", GINT_TO_POINTER(1), test_failed_area_clear_invalidates_cache);
}
