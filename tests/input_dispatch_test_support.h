#ifndef TESTS_INPUT_DISPATCH_TEST_SUPPORT_H
#define TESTS_INPUT_DISPATCH_TEST_SUPPORT_H

#include "app.h"

typedef struct {
    gint preview_click_calls;
    gint preview_render_calls;
    gint preview_selection_render_calls;
    gint book_preview_click_calls;
    gint book_preview_render_calls;
    gint book_preview_selection_render_calls;
    gint file_manager_click_calls;
    gint file_manager_render_calls;
    gint next_image_calls;
    gint previous_image_calls;
    gint video_seek_calls;
    gint refresh_display_calls;
    gint book_change_page_calls;
    gint delete_calls;
    gint render_by_mode_calls;
    gint enter_file_manager_calls;
    gint enter_preview_calls;
    gboolean current_is_video;
    gint64 last_video_seek_delta_ms;
    ErrorCode delete_result;
    ErrorCode enter_file_manager_result;
    ErrorCode enter_preview_result;
    ErrorCode video_seek_result;
    ErrorCode next_image_result;
    ErrorCode previous_image_result;
} InputDispatchTestStubState;

extern InputDispatchTestStubState g_input_dispatch_stub_state;

void input_dispatch_test_reset_stubs(void);
ErrorCode input_dispatch_test_video_seek(VideoPlayer *player, gint64 delta_ms);

#endif
