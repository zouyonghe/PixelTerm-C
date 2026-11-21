#ifndef TESTS_INPUT_DISPATCH_TEST_SUPPORT_H
#define TESTS_INPUT_DISPATCH_TEST_SUPPORT_H

#include "app.h"

typedef struct {
    gint preview_click_calls;
    gint preview_render_calls;
    gboolean preview_render_needs_screen_clear;
    gboolean preview_render_suppress_full_clear;
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
    gint display_image_info_calls;
    gint display_help_calls;
    gint book_change_page_calls;
    gint delete_calls;
    gint render_by_mode_calls;
    gboolean render_by_mode_preserved_pause;
    gint open_book_calls;
    gint close_book_calls;
    gint enter_file_manager_calls;
    gint last_enter_file_manager_index;
    gint exit_file_manager_calls;
    gint clear_listing_calls;
    gint enter_preview_calls;
    gint preview_zoom_calls;
    gint last_preview_zoom_delta;
    gint exit_preview_calls;
    gboolean last_exit_preview_open_selected;
    gint enter_book_preview_calls;
    gint enter_book_page_calls;
    gint book_page_render_calls;
    gint book_toc_render_calls;
    gint book_jump_prompt_render_calls;
    gint book_jump_prompt_clear_calls;
    ErrorCode book_jump_prompt_result;
    gint file_manager_enter_calls;
    gint file_manager_toggle_hidden_calls;
    gint file_manager_select_path_calls;
    gint file_manager_jump_to_letter_calls;
    char last_file_manager_jump_letter;
    gint load_directory_calls;
    gboolean require_isolated_directory_load;
    gint preloader_reset_calls;
    gint preloader_enable_calls;
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
    gboolean enter_file_manager_renders_before_error;
    ErrorCode exit_file_manager_result;
    ErrorCode enter_preview_result;
    ErrorCode enter_book_preview_result;
    ErrorCode enter_book_page_result;
    ErrorCode file_manager_enter_result;
    gboolean file_manager_enter_renders_error;
    ErrorCode file_manager_toggle_hidden_result;
    ErrorCode load_directory_result;
    gboolean load_directory_replaces_media;
    ErrorCode book_change_page_result;
    ErrorCode enter_at_position_result;
    ErrorCode video_seek_result;
    ErrorCode next_image_result;
    ErrorCode previous_image_result;
    ErrorCode refresh_display_result;
    ErrorCode display_image_info_result;
    ErrorCode display_help_result;
    ErrorCode render_by_mode_result;
    ErrorCode preview_render_result;
    ErrorCode book_preview_render_result;
    ErrorCode book_page_render_result;
    ErrorCode file_manager_render_result;
    gboolean render_by_mode_outputs_help;
    const gchar *render_by_mode_payload;
    ErrorCode preview_selection_render_result;
    ErrorCode exit_preview_result;
    ErrorCode preview_zoom_result;
    gboolean preview_move_scroll;
    gboolean preview_click_changes_selection;
} InputDispatchTestStubState;

extern InputDispatchTestStubState g_input_dispatch_stub_state;

void input_dispatch_test_reset_stubs(void);
ErrorCode input_dispatch_test_video_seek(VideoPlayer *player, gint64 delta_ms);

#endif
