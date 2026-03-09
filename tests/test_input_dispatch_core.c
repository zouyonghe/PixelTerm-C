#include <glib.h>

#include "app.h"
#include "input_dispatch_core.h"

static gboolean requeue_idle_source(gpointer user_data) {
    gint *call_count = (gint *)user_data;
    *call_count += 1;
    if (*call_count < 3) {
        g_idle_add(requeue_idle_source, user_data);
    }
    return G_SOURCE_REMOVE;
}

static void drain_default_main_context(void) {
    while (g_main_context_pending(NULL)) {
        g_main_context_iteration(NULL, FALSE);
    }
}

static void test_process_animations_only_runs_one_iteration_per_call(void) {
    PixelTermApp app = {0};
    GifPlayer gif = {0};
    gint call_count = 0;

    drain_default_main_context();

    gif.is_playing = TRUE;
    app.gif_player = &gif;
    g_idle_add(requeue_idle_source, &call_count);

    input_dispatch_core_process_animations(&app);

    g_assert_cmpint(call_count, ==, 1);

    drain_default_main_context();
}

static void test_process_animations_skips_when_nothing_is_playing(void) {
    PixelTermApp app = {0};
    GifPlayer gif = {0};
    gint call_count = 0;

    drain_default_main_context();

    gif.is_playing = FALSE;
    app.gif_player = &gif;
    g_idle_add(requeue_idle_source, &call_count);

    input_dispatch_core_process_animations(&app);

    g_assert_cmpint(call_count, ==, 0);

    drain_default_main_context();
}

void register_input_dispatch_core_tests(void) {
    g_test_add_func("/input_dispatch_core/process_animations/only_runs_one_iteration_per_call",
                    test_process_animations_only_runs_one_iteration_per_call);
    g_test_add_func("/input_dispatch_core/process_animations/skips_when_nothing_is_playing",
                    test_process_animations_skips_when_nothing_is_playing);
}
