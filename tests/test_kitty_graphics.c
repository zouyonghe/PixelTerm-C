#include <glib.h>
#include <string.h>

#include "kitty_graphics.h"
#include "process_env.h"

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

    g_assert_true(kitty_graphics_shm_auto_enabled());

    pixelterm_env_reset_for_test();
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
    g_test_add_func("/kitty_graphics/shm_command/controls", test_kitty_graphics_shm_command_contains_expected_controls);
    g_test_add_func("/kitty_graphics/shm_command/rejects_invalid_input", test_kitty_graphics_shm_command_rejects_invalid_input);
    g_test_add_func("/kitty_graphics/shm_auto/rejects_remote_context", test_kitty_graphics_shm_auto_enabled_rejects_remote_context);
    g_test_add_func("/kitty_graphics/shm_auto/allows_explicit_override", test_kitty_graphics_shm_auto_enabled_allows_explicit_override);
    g_test_add_func("/kitty_graphics/frame/rejects_rowstride_overflow", test_kitty_graphics_frame_rejects_rowstride_overflow);
    g_test_add_func("/kitty_graphics/frame/rejects_large_payload", test_kitty_graphics_frame_rejects_large_payload);
}
