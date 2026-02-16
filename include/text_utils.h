#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H

#include <glib.h>

/**
 * @brief Sanitizes text for safe terminal display
 *
 * Removes or replaces control characters and non-printable characters
 * that could interfere with terminal display or cause security issues.
 *
 * @param text Input text to sanitize (UTF-8 encoded)
 * @return Sanitized text string. Caller must free with g_free()
 *
 * @note Returns an empty string if input is NULL
 */
gchar* sanitize_for_terminal(const gchar *text);

/**
 * @brief Calculates the display width of UTF-8 text
 *
 * Computes the visual width of the text when displayed in a terminal,
 * taking into account wide characters (e.g., CJK characters) and
 * combining characters.
 *
 * @param text UTF-8 encoded text string
 * @return Display width in terminal columns
 *
 * @note Wide characters (e.g., Chinese, Japanese) count as 2 columns
 */
gint utf8_display_width(const gchar *text);

/**
 * @brief Extracts a UTF-8 prefix with specified display width
 *
 * Returns a prefix of the input text that fits within the specified
 * maximum display width, properly handling multi-byte UTF-8 characters.
 *
 * @param text Input UTF-8 text string
 * @param max_width Maximum display width in columns
 * @return Prefix string with display width <= max_width. Caller must free with g_free()
 *
 * @note Will not break multi-byte characters
 */
gchar* utf8_prefix_by_width(const gchar *text, gint max_width);

/**
 * @brief Extracts a UTF-8 suffix with specified display width
 *
 * Returns a suffix of the input text that fits within the specified
 * maximum display width, properly handling multi-byte UTF-8 characters.
 *
 * @param text Input UTF-8 text string
 * @param max_width Maximum display width in columns
 * @return Suffix string with display width <= max_width. Caller must free with g_free()
 *
 * @note Will not break multi-byte characters
 */
gchar* utf8_suffix_by_width(const gchar *text, gint max_width);

/**
 * @brief Truncates UTF-8 text to fit within display width
 *
 * Truncates the text to fit within the specified maximum width,
 * adding an ellipsis (...) if truncation occurs.
 *
 * @param text Input UTF-8 text string
 * @param max_width Maximum display width in columns
 * @return Truncated string with ellipsis if needed. Caller must free with g_free()
 *
 * @note Preserves UTF-8 character boundaries
 */
gchar* truncate_utf8_for_display(const gchar *text, gint max_width);

/**
 * @brief Truncates UTF-8 text in the middle, keeping the suffix
 *
 * Truncates text by removing characters from the middle, preserving
 * the beginning and end portions. Useful for displaying file paths
 * where the end is more important than the middle.
 *
 * @param text Input UTF-8 text string
 * @param max_width Maximum display width in columns
 * @return Truncated string with middle replaced by ellipsis. Caller must free with g_free()
 *
 * @note Example: "/very/long/path/to/file.txt" -> "/very/...file.txt"
 */
gchar* truncate_utf8_middle_keep_suffix(const gchar *text, gint max_width);

#endif
