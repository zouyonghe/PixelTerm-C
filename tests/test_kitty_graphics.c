#include <glib.h>
#include <string.h>

#include "kitty_graphics.h"

static void test_kitty_graphics_shm_command_contains_expected_controls(void) {
    GString *command = kitty_graphics_build_shm_command_for_test("/pixelterm-test", 16, 8, 4, 2, 512);
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
    g_assert_null(kitty_graphics_build_shm_command_for_test(NULL, 16, 8, 4, 2, 512));
    g_assert_null(kitty_graphics_build_shm_command_for_test("/x", 0, 8, 4, 2, 512));
    g_assert_null(kitty_graphics_build_shm_command_for_test("/x", 16, 8, 0, 2, 512));
    g_assert_null(kitty_graphics_build_shm_command_for_test("/x", 16, 8, 4, 2, 0));
}

static void test_kitty_graphics_shm_auto_enabled_rejects_remote_context(void) {
    const gchar *old_override = g_getenv("PIXELTERM_KITTY_SHM");
    const gchar *old_ssh = g_getenv("SSH_CONNECTION");
    const gchar *old_term = g_getenv("TERM");

    g_unsetenv("PIXELTERM_KITTY_SHM");
    g_setenv("SSH_CONNECTION", "host 1 host 2", TRUE);
    g_setenv("TERM", "xterm-kitty", TRUE);

    g_assert_false(kitty_graphics_shm_auto_enabled());

    if (old_override) g_setenv("PIXELTERM_KITTY_SHM", old_override, TRUE); else g_unsetenv("PIXELTERM_KITTY_SHM");
    if (old_ssh) g_setenv("SSH_CONNECTION", old_ssh, TRUE); else g_unsetenv("SSH_CONNECTION");
    if (old_term) g_setenv("TERM", old_term, TRUE); else g_unsetenv("TERM");
}

static void test_kitty_graphics_shm_auto_enabled_allows_explicit_override(void) {
    const gchar *old_override = g_getenv("PIXELTERM_KITTY_SHM");
    const gchar *old_ssh = g_getenv("SSH_CONNECTION");
    const gchar *old_term = g_getenv("TERM");

    g_setenv("PIXELTERM_KITTY_SHM", "1", TRUE);
    g_setenv("SSH_CONNECTION", "host 1 host 2", TRUE);
    g_setenv("TERM", "xterm-256color", TRUE);

    g_assert_true(kitty_graphics_shm_auto_enabled());

    if (old_override) g_setenv("PIXELTERM_KITTY_SHM", old_override, TRUE); else g_unsetenv("PIXELTERM_KITTY_SHM");
    if (old_ssh) g_setenv("SSH_CONNECTION", old_ssh, TRUE); else g_unsetenv("SSH_CONNECTION");
    if (old_term) g_setenv("TERM", old_term, TRUE); else g_unsetenv("TERM");
}

void register_kitty_graphics_tests(void) {
    g_test_add_func("/kitty_graphics/shm_command/controls", test_kitty_graphics_shm_command_contains_expected_controls);
    g_test_add_func("/kitty_graphics/shm_command/rejects_invalid_input", test_kitty_graphics_shm_command_rejects_invalid_input);
    g_test_add_func("/kitty_graphics/shm_auto/rejects_remote_context", test_kitty_graphics_shm_auto_enabled_rejects_remote_context);
    g_test_add_func("/kitty_graphics/shm_auto/allows_explicit_override", test_kitty_graphics_shm_auto_enabled_allows_explicit_override);
}
