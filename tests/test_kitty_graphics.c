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
    g_assert_cmpint(width, >, 0);
    g_assert_cmpuint((gsize)width * height * 3, <=, KITTY_GRAPHICS_DIRECT_MAX_BYTES);
    kitty_graphics_get_direct_size(16, 8, 100, 100, &width, &height);
    g_assert_cmpint(width, ==, 16);
    g_assert_cmpint(height, ==, 8);
    kitty_graphics_get_direct_size(1, 16000000, 0, 0, &width, &height);
    g_assert_cmpint(width, ==, 1);
    g_assert_cmpuint((gsize)width * height * 3, <=, KITTY_GRAPHICS_DIRECT_MAX_BYTES);
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
