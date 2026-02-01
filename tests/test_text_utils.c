#include <glib.h>
#include <string.h>
#include "text_utils.h"

// Test sanitize_for_terminal
static void test_sanitize_for_terminal_null(void) {
    gchar *result = sanitize_for_terminal(NULL);
    g_assert_null(result);
}

static void test_sanitize_for_terminal_simple(void) {
    const gchar *input = "Hello World";
    gchar *result = sanitize_for_terminal(input);
    g_assert_nonnull(result);
    g_assert_cmpstr(result, ==, "Hello World");
    g_free(result);
}

static void test_sanitize_for_terminal_empty(void) {
    const gchar *input = "";
    gchar *result = sanitize_for_terminal(input);
    g_assert_nonnull(result);
    g_assert_cmpstr(result, ==, "");
    g_free(result);
}

// Test utf8_display_width
static void test_utf8_display_width_ascii(void) {
    gint width = utf8_display_width("Hello");
    g_assert_cmpint(width, ==, 5);
}

static void test_utf8_display_width_empty(void) {
    gint width = utf8_display_width("");
    g_assert_cmpint(width, ==, 0);
}

static void test_utf8_display_width_null(void) {
    gint width = utf8_display_width(NULL);
    g_assert_cmpint(width, ==, 0);
}

// Test utf8_prefix_by_width
static void test_utf8_prefix_by_width_full(void) {
    gchar *result = utf8_prefix_by_width("Hello", 10);
    g_assert_nonnull(result);
    g_assert_cmpstr(result, ==, "Hello");
    g_free(result);
}

static void test_utf8_prefix_by_width_truncate(void) {
    gchar *result = utf8_prefix_by_width("Hello World", 5);
    g_assert_nonnull(result);
    // Should return first 5 characters worth of width
    g_assert_cmpint(utf8_display_width(result), <=, 5);
    // Verify it's a proper prefix
    g_assert_true(g_str_has_prefix("Hello World", result));
    g_free(result);
}

static void test_utf8_prefix_by_width_zero(void) {
    gchar *result = utf8_prefix_by_width("Hello", 0);
    g_assert_nonnull(result);
    g_assert_cmpstr(result, ==, "");
    g_free(result);
}

// Test utf8_suffix_by_width
static void test_utf8_suffix_by_width_full(void) {
    gchar *result = utf8_suffix_by_width("Hello", 10);
    g_assert_nonnull(result);
    g_assert_cmpstr(result, ==, "Hello");
    g_free(result);
}

static void test_utf8_suffix_by_width_truncate(void) {
    gchar *result = utf8_suffix_by_width("Hello World", 5);
    g_assert_nonnull(result);
    // Should return last 5 characters worth of width
    g_assert_cmpint(utf8_display_width(result), <=, 5);
    // Verify it's a proper suffix
    g_assert_true(g_str_has_suffix("Hello World", result));
    g_free(result);
}

// Test truncate_utf8_for_display
static void test_truncate_utf8_for_display_no_truncate(void) {
    gchar *result = truncate_utf8_for_display("Hello", 10);
    g_assert_nonnull(result);
    g_assert_cmpstr(result, ==, "Hello");
    g_free(result);
}

static void test_truncate_utf8_for_display_truncate(void) {
    gchar *result = truncate_utf8_for_display("Hello World", 8);
    g_assert_nonnull(result);
    // Should be truncated with ellipsis
    g_assert_cmpint(utf8_display_width(result), <=, 8);
    // Should contain ellipsis when truncated
    g_assert_true(strstr(result, "...") != NULL || g_utf8_strlen(result, -1) < 11);
    g_free(result);
}

static void test_truncate_utf8_for_display_null(void) {
    gchar *result = truncate_utf8_for_display(NULL, 10);
    g_assert_null(result);
}

// Test truncate_utf8_middle_keep_suffix
static void test_truncate_utf8_middle_no_truncate(void) {
    gchar *result = truncate_utf8_middle_keep_suffix("Hello", 10);
    g_assert_nonnull(result);
    g_assert_cmpstr(result, ==, "Hello");
    g_free(result);
}

static void test_truncate_utf8_middle_truncate(void) {
    gchar *result = truncate_utf8_middle_keep_suffix("/very/long/path/file.txt", 15);
    g_assert_nonnull(result);
    // Should be truncated in the middle with ellipsis
    g_assert_cmpint(utf8_display_width(result), <=, 15);
    // Should contain the suffix (end of path)
    g_assert_true(g_str_has_suffix(result, "file.txt"));
    // Should start with the prefix
    g_assert_true(g_str_has_prefix(result, "/very"));
    g_free(result);
}

static void test_truncate_utf8_middle_null(void) {
    gchar *result = truncate_utf8_middle_keep_suffix(NULL, 10);
    g_assert_null(result);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);

    // sanitize_for_terminal tests
    g_test_add_func("/text_utils/sanitize/null", test_sanitize_for_terminal_null);
    g_test_add_func("/text_utils/sanitize/simple", test_sanitize_for_terminal_simple);
    g_test_add_func("/text_utils/sanitize/empty", test_sanitize_for_terminal_empty);

    // utf8_display_width tests
    g_test_add_func("/text_utils/display_width/ascii", test_utf8_display_width_ascii);
    g_test_add_func("/text_utils/display_width/empty", test_utf8_display_width_empty);
    g_test_add_func("/text_utils/display_width/null", test_utf8_display_width_null);

    // utf8_prefix_by_width tests
    g_test_add_func("/text_utils/prefix/full", test_utf8_prefix_by_width_full);
    g_test_add_func("/text_utils/prefix/truncate", test_utf8_prefix_by_width_truncate);
    g_test_add_func("/text_utils/prefix/zero", test_utf8_prefix_by_width_zero);

    // utf8_suffix_by_width tests
    g_test_add_func("/text_utils/suffix/full", test_utf8_suffix_by_width_full);
    g_test_add_func("/text_utils/suffix/truncate", test_utf8_suffix_by_width_truncate);

    // truncate_utf8_for_display tests
    g_test_add_func("/text_utils/truncate/no_truncate", test_truncate_utf8_for_display_no_truncate);
    g_test_add_func("/text_utils/truncate/truncate", test_truncate_utf8_for_display_truncate);
    g_test_add_func("/text_utils/truncate/null", test_truncate_utf8_for_display_null);

    // truncate_utf8_middle_keep_suffix tests
    g_test_add_func("/text_utils/truncate_middle/no_truncate", test_truncate_utf8_middle_no_truncate);
    g_test_add_func("/text_utils/truncate_middle/truncate", test_truncate_utf8_middle_truncate);
    g_test_add_func("/text_utils/truncate_middle/null", test_truncate_utf8_middle_null);

    return g_test_run();
}
