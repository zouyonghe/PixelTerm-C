#include "browser.h"
#include "renderer.h"

// Create a new file browser
FileBrowser* browser_create(void) {
    FileBrowser *browser = g_new0(FileBrowser, 1);
    if (!browser) {
        return NULL;
    }

    browser->directory_path = NULL;
    browser->image_files = NULL;
    browser->current = NULL;
    browser->total_files = 0;

    return browser;
}

// Destroy file browser and free resources
void browser_destroy(FileBrowser *browser) {
    if (!browser) {
        return;
    }

    g_free(browser->directory_path);
    
    if (browser->image_files) {
        g_list_free_full(browser->image_files, (GDestroyNotify)g_free);
    }

    g_free(browser);
}

// Scan directory for image files
ErrorCode browser_scan_directory(FileBrowser *browser, const char *directory) {
    if (!browser || !directory) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Check if directory exists
    if (!g_file_test(directory, G_FILE_TEST_IS_DIR)) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Cleanup existing data
    g_free(browser->directory_path);
    if (browser->image_files) {
        g_list_free_full(browser->image_files, (GDestroyNotify)g_free);
        browser->image_files = NULL;
    }

    browser->directory_path = g_strdup(directory);
    browser->current = NULL;
    browser->total_files = 0;

    // Open directory
    GDir *dir = g_dir_open(directory, 0, NULL);
    if (!dir) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Collect all files
    GList *all_files = NULL;
    const gchar *filename;

    while ((filename = g_dir_read_name(dir)) != NULL) {
        gchar *full_path = g_build_filename(directory, filename, NULL);
        
        // Check if it's a regular file and an image
        if (g_file_test(full_path, G_FILE_TEST_IS_REGULAR) && 
            is_image_file(filename)) {
            all_files = g_list_prepend(all_files, full_path);
        } else {
            g_free(full_path);
        }
    }

    g_dir_close(dir);

    // Sort files alphabetically
    all_files = g_list_sort(all_files, (GCompareFunc)g_strcmp0);

    // Store the filtered and sorted list
    browser->image_files = all_files;
    browser->current = browser->image_files;
    browser->total_files = g_list_length(browser->image_files);

    return ERROR_NONE;
}

// Refresh current directory
ErrorCode browser_refresh_directory(FileBrowser *browser) {
    if (!browser || !browser->directory_path) {
        return ERROR_FILE_NOT_FOUND;
    }

    return browser_scan_directory(browser, browser->directory_path);
}

// Check if file is an image based on extension


// Filter image files from a list of all files
GList* browser_filter_image_files(GList *all_files) {
    GList *image_files = NULL;
    GList *current = all_files;

    while (current) {
        gchar *filename = (gchar*)current->data;
        if (is_image_file(filename)) {
            image_files = g_list_prepend(image_files, g_strdup(filename));
        }
        current = g_list_next(current);
    }

    return g_list_sort(image_files, (GCompareFunc)g_strcmp0);
}

// Sort files alphabetically
void browser_sort_files(GList **files) {
    if (files && *files) {
        *files = g_list_sort(*files, (GCompareFunc)g_strcmp0);
    }
}

// Navigate to next file
ErrorCode browser_next_file(FileBrowser *browser) {
    if (!browser || !browser->current) {
        return ERROR_INVALID_IMAGE;
    }

    if (browser->current->next) {
        browser->current = browser->current->next;
        return ERROR_NONE;
    }

    return ERROR_INVALID_IMAGE;
}

// Navigate to previous file
ErrorCode browser_previous_file(FileBrowser *browser) {
    if (!browser || !browser->current) {
        return ERROR_INVALID_IMAGE;
    }

    if (browser->current->prev) {
        browser->current = browser->current->prev;
        return ERROR_NONE;
    }

    return ERROR_INVALID_IMAGE;
}

// Go to file by index
ErrorCode browser_goto_index(FileBrowser *browser, gint index) {
    if (!browser || !browser->image_files) {
        return ERROR_INVALID_IMAGE;
    }

    if (index < 0 || index >= browser->total_files) {
        return ERROR_INVALID_IMAGE;
    }

    browser->current = g_list_nth(browser->image_files, index);
    return browser->current ? ERROR_NONE : ERROR_INVALID_IMAGE;
}

// Go to file by filename
ErrorCode browser_goto_filename(FileBrowser *browser, const char *filename) {
    if (!browser || !browser->image_files || !filename) {
        return ERROR_INVALID_IMAGE;
    }

    GList *current = browser->image_files;
    gchar *target_basename = g_path_get_basename(filename);

    while (current) {
        gchar *current_file = (gchar*)current->data;
        gchar *current_basename = g_path_get_basename(current_file);
        
        gboolean match = (g_strcmp0(current_basename, target_basename) == 0);
        g_free(current_basename);
        
        if (match) {
            browser->current = current;
            g_free(target_basename);
            return ERROR_NONE;
        }
        
        current = g_list_next(current);
    }

    g_free(target_basename);
    return ERROR_INVALID_IMAGE;
}

// Get current file path
const gchar* browser_get_current_file(const FileBrowser *browser) {
    if (!browser || !browser->current) {
        return NULL;
    }

    return (const gchar*)browser->current->data;
}

// Get directory path
const gchar* browser_get_directory(const FileBrowser *browser) {
    return browser ? browser->directory_path : NULL;
}

// Get current index
gint browser_get_current_index(const FileBrowser *browser) {
    if (!browser || !browser->image_files || !browser->current) {
        return -1;
    }

    return g_list_position(browser->image_files, browser->current);
}

// Get total files count
gint browser_get_total_files(const FileBrowser *browser) {
    return browser ? browser->total_files : 0;
}

// Check if browser has files
gboolean browser_has_files(const FileBrowser *browser) {
    return browser && browser->image_files && browser->total_files > 0;
}

// Delete current file
ErrorCode browser_delete_current_file(FileBrowser *browser) {
    if (!browser || !browser->current) {
        return ERROR_INVALID_IMAGE;
    }

    const gchar *filepath = browser_get_current_file(browser);
    if (!filepath) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Remove file from filesystem
    if (unlink(filepath) != 0) {
        return ERROR_FILE_NOT_FOUND;
    }

    // Remove from list
    GList *to_remove = browser->current;
    gchar *file_data = (gchar*)to_remove->data;
    
    // Update current pointer
    if (to_remove->prev) {
        browser->current = to_remove->prev;
    } else if (to_remove->next) {
        browser->current = to_remove->next;
    } else {
        browser->current = NULL;
    }

    // Remove from list and free memory
    browser->image_files = g_list_delete_link(browser->image_files, to_remove);
    browser->total_files--;
    g_free(file_data);

    return ERROR_NONE;
}

// Get file information for current file
ImageInfo* browser_get_file_info(const FileBrowser *browser) {
    if (!browser || !browser_has_files(browser)) {
        return NULL;
    }

    const gchar *filepath = browser_get_current_file(browser);
    if (!filepath) {
        return NULL;
    }

    ImageInfo *info = g_new0(ImageInfo, 1);
    if (!info) {
        return NULL;
    }

    // Basic file information
    info->filepath = g_strdup(filepath);
    info->filename = g_path_get_basename(filepath);
    info->file_size = get_file_size(filepath);
    info->modification_time = get_file_mtime(filepath);
    info->format = g_strdup(get_file_extension(filepath));

    // Get image dimensions
    renderer_get_image_dimensions(filepath, &info->width, &info->height);

    return info;
}

// Get all files
GList* browser_get_all_files(const FileBrowser *browser) {
    return browser ? browser->image_files : NULL;
}

// Check if at first file
gboolean browser_is_at_first(const FileBrowser *browser) {
    if (!browser || !browser->current) {
        return TRUE;
    }

    return browser->current->prev == NULL;
}

// Check if at last file
gboolean browser_is_at_last(const FileBrowser *browser) {
    if (!browser || !browser->current) {
        return TRUE;
    }

    return browser->current->next == NULL;
}

// Reset browser state
void browser_reset(FileBrowser *browser) {
    if (!browser) {
        return;
    }

    browser->current = browser->image_files;
}