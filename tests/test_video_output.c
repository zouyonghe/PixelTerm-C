#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "video_player.h"
#include "terminal_output.h"
#include "test_output_stream.h"
#include <errno.h>
#include <string.h>

static ssize_t failed_area_write(void *data, const char *bytes, size_t length) {
    (void)data; (void)bytes; (void)length;
    errno = EIO;
    return -1;
}

static void test_failed_area_clear_preserves_cache(void) {
    VideoPlayer *player = video_player_new(1, TRUE, FALSE, FALSE, FALSE,
        TEXT_SYMBOL_MODE_AUTO, 1.0, KITTY_TRANSFER_DIRECT);
    g_assert_nonnull(player);
    player->last_frame_top_row = 4;
    player->last_frame_height = 1;
    player->last_frame_lines = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(player->last_frame_lines, g_strdup("cached row"));
    FILE *stream = test_output_stream(NULL, failed_area_write);
    FILE *recovery = tmpfile();
    g_assert_nonnull(stream);
    g_assert_nonnull(recovery);
    FILE *saved = stdout;
    fflush(saved);
    stdout = stream;
    ErrorCode result = video_player_clear_render_area(player);
    gboolean retained = player->last_frame_lines && player->last_frame_lines->len == 1 &&
        strcmp(g_ptr_array_index(player->last_frame_lines, 0), "cached row") == 0;
    stdout = recovery;
    gboolean recovered = terminal_output_flush();
    fclose(stream);
    stdout = saved;
    fclose(recovery);
    video_player_destroy(player);
    g_assert_true(retained);
    g_assert_cmpint(result, ==, ERROR_OUTPUT_IO);
    g_assert_true(recovered);
}

void register_video_output_tests(void) {
    g_test_add_func("/video_player/output/failed_clear_preserves_cache", test_failed_area_clear_preserves_cache);
}
