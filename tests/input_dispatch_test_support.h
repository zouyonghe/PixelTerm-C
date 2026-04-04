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
    gint open_book_calls;
    gint close_book_calls;
    gint enter_file_manager_calls;
    gint exit_file_manager_calls;
    gint enter_preview_calls;
    gint enter_book_preview_calls;
    gint enter_book_page_calls;
    gint book_page_render_calls;
    gint book_toc_render_calls;
    gint book_jump_prompt_render_calls;
    gint book_jump_prompt_clear_calls;
    gint file_manager_enter_calls;
    gint file_manager_toggle_hidden_calls;
    gint file_manager_select_path_calls;
    gint load_directory_calls;
    gint enter_at_position_calls;
    gint last_mouse_x;
    gint last_mouse_y;
    gboolean current_is_video;
    gboolean file_manager_has_images;
    gint enter_at_position_mode;
    gint64 last_video_seek_delta_ms;
    gint64 video_seek_total_delta_ms;
    ErrorCode delete_result;
    ErrorCode open_book_result;
    ErrorCode enter_file_manager_result;
    ErrorCode exit_file_manager_result;
    ErrorCode enter_preview_result;
    ErrorCode enter_book_preview_result;
    ErrorCode enter_book_page_result;
    ErrorCode file_manager_enter_result;
    ErrorCode file_manager_toggle_hidden_result;
    ErrorCode load_directory_result;
    ErrorCode enter_at_position_result;
    ErrorCode video_seek_result;
    ErrorCode next_image_result;
    ErrorCode previous_image_result;
} InputDispatchTestStubState;

extern InputDispatchTestStubState g_input_dispatch_stub_state;

void input_dispatch_test_reset_stubs(void);
ErrorCode input_dispatch_test_video_seek(VideoPlayer *player, gint64 delta_ms);

#endif
