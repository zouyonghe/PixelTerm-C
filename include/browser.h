#ifndef BROWSER_H
#define BROWSER_H

#include "common.h"

// File browser structure
typedef struct {
    gchar *directory_path;
    GList *image_files;
    GList *current;
    gint total_files;
} FileBrowser;

// Browser lifecycle functions
FileBrowser* browser_create(void);
void browser_destroy(FileBrowser *browser);

// Directory scanning functions
ErrorCode browser_scan_directory(FileBrowser *browser, const char *directory);
ErrorCode browser_refresh_directory(FileBrowser *browser);

// File filtering and sorting

GList* browser_filter_image_files(GList *all_files);
void browser_sort_files(GList **files);

// Navigation functions
ErrorCode browser_next_file(FileBrowser *browser);
ErrorCode browser_previous_file(FileBrowser *browser);
ErrorCode browser_goto_index(FileBrowser *browser, gint index);
ErrorCode browser_goto_filename(FileBrowser *browser, const char *filename);

// File information functions
const gchar* browser_get_current_file(const FileBrowser *browser);
const gchar* browser_get_directory(const FileBrowser *browser);
gint browser_get_current_index(const FileBrowser *browser);
gint browser_get_total_files(const FileBrowser *browser);
gboolean browser_has_files(const FileBrowser *browser);

// File operations
ErrorCode browser_delete_current_file(FileBrowser *browser);
ImageInfo* browser_get_file_info(const FileBrowser *browser);
GList* browser_get_all_files(const FileBrowser *browser);

// Utility functions
gboolean browser_is_at_first(const FileBrowser *browser);
gboolean browser_is_at_last(const FileBrowser *browser);
void browser_reset(FileBrowser *browser);

#endif // BROWSER_H