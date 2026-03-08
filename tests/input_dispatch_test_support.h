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
    gint refresh_display_calls;
    gint book_change_page_calls;
    gint delete_calls;
    gint render_by_mode_calls;
    gint enter_file_manager_calls;
    gboolean current_is_video;
    ErrorCode delete_result;
    ErrorCode enter_file_manager_result;
} InputDispatchTestStubState;

extern InputDispatchTestStubState g_input_dispatch_stub_state;

void input_dispatch_test_reset_stubs(void);

#endif
