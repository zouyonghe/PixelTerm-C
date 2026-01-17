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
/**
 * @brief Creates a new `FileBrowser` instance.
 * 
 * Allocates memory for a new `FileBrowser` structure and initializes its
 * members to default values (e.g., NULL pointers, zero counts).
 * 
 * @return A pointer to the newly created `FileBrowser` instance on success,
 *         or NULL if memory allocation fails.
 */
FileBrowser* browser_create(void);
/**
 * @brief Destroys a `FileBrowser` instance and frees all associated resources.
 * 
 * This function cleans up memory allocated for the directory path and the
 * list of image files managed by the browser.
 * 
 * @param browser A pointer to the `FileBrowser` instance to destroy.
 */
void browser_destroy(FileBrowser *browser);

// Directory scanning functions
/**
 * @brief Scans a specified directory for image files and populates the browser's list.
 * 
 * Clears any previously loaded files, sets the new directory path, and then
 * iterates through the directory, adding all supported image files to an
 * internal list. The list is sorted alphabetically.
 * 
 * @param browser A pointer to the `FileBrowser` instance.
 * @param directory The path to the directory to scan.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         directory cannot be opened or accessed.
 */
ErrorCode browser_scan_directory(FileBrowser *browser, const char *directory);
/**
 * @brief Refreshes the current directory's file list.
 * 
 * This function re-scans the directory previously set in the browser.
 * It's equivalent to calling `browser_scan_directory` with the current
 * `directory_path`.
 * 
 * @param browser A pointer to the `FileBrowser` instance.
 * @return `ERROR_NONE` on success, or an appropriate `ErrorCode` if the
 *         directory cannot be opened or accessed.
 */
ErrorCode browser_refresh_directory(FileBrowser *browser);



// Navigation functions
/**
 * @brief Moves the current file pointer to the next file in the list.
 * 
 * If currently at the last file, the current selection stays unchanged.
 * 
 * @param browser A pointer to the `FileBrowser` instance.
 * @return `ERROR_NONE` on success, or `ERROR_INVALID_IMAGE` if no next file exists.
 */
ErrorCode browser_next_file(FileBrowser *browser);
/**
 * @brief Moves the current file pointer to the previous file in the list.
 * 
 * If currently at the first file, the current selection stays unchanged.
 * 
 * @param browser A pointer to the `FileBrowser` instance.
 * @return `ERROR_NONE` on success, or `ERROR_INVALID_IMAGE` if no previous file exists.
 */
ErrorCode browser_previous_file(FileBrowser *browser);
/**
 * @brief Sets the current file pointer to a specific index in the file list.
 * 
 * @param browser A pointer to the `FileBrowser` instance.
 * @param index The 0-based index of the file to navigate to.
 * @return `ERROR_NONE` on success, or `ERROR_FILE_NOT_FOUND` if no files are
 *         loaded or the index is out of bounds.
 */
ErrorCode browser_goto_index(FileBrowser *browser, gint index);
/**
 * @brief Sets the current file pointer to a specific file by its filename.
 * 
 * Searches the browser's file list for a matching filename and updates the
 * current file pointer if found.
 * 
 * @param browser A pointer to the `FileBrowser` instance.
 * @param filename The name of the file to navigate to.
 * @return `ERROR_NONE` on success, or `ERROR_FILE_NOT_FOUND` if the file
 *         is not found in the current list.
 */
ErrorCode browser_goto_filename(FileBrowser *browser, const char *filename);

// File information functions
/**
 * @brief Retrieves the path of the currently selected file.
 * 
 * @param browser A pointer to the constant `FileBrowser` instance.
 * @return A constant pointer to the string representing the current file's
 *         path, or NULL if no file is currently selected or the browser is invalid.
 *         The returned pointer points to internal data and should not be freed.
 */
const gchar* browser_get_current_file(const FileBrowser *browser);
/**
 * @brief Retrieves the path of the directory currently being browsed.
 * 
 * @param browser A pointer to the constant `FileBrowser` instance.
 * @return A constant pointer to the string representing the current directory's
 *         path, or NULL if the browser is invalid or no directory is set.
 *         The returned pointer points to internal data and should not be freed.
 */
const gchar* browser_get_directory(const FileBrowser *browser);
/**
 * @brief Retrieves the 0-based index of the currently selected file in the browser's list.
 * 
 * @param browser A pointer to the constant `FileBrowser` instance.
 * @return The index of the current file, or -1 if the browser is invalid or no file is selected.
 */
gint browser_get_current_index(const FileBrowser *browser);
/**
 * @brief Retrieves the total number of files in the browser's current list.
 * 
 * @param browser A pointer to the constant `FileBrowser` instance.
 * @return The total count of files, or 0 if the browser is invalid or empty.
 */
gint browser_get_total_files(const FileBrowser *browser);
/**
 * @brief Checks if the browser's current list contains any files.
 * 
 * @param browser A pointer to the constant `FileBrowser` instance.
 * @return `TRUE` if the list is not empty, `FALSE` otherwise.
 */
gboolean browser_has_files(const FileBrowser *browser);

// File operations
/**
 * @brief Deletes the currently selected file from the filesystem and the browser's list.
 * 
 * @param browser A pointer to the `FileBrowser` instance.
 * @return `ERROR_NONE` on successful deletion, or an appropriate `ErrorCode` if
 *         the file cannot be deleted, is not found, or no file is selected.
 */
ErrorCode browser_delete_current_file(FileBrowser *browser);
/**
 * @brief Retrieves detailed information about the currently selected file.
 * 
 * @param browser A pointer to the constant `FileBrowser` instance.
 * @return A pointer to an `ImageInfo` structure containing details about
 *         the current file, or NULL if no file is selected or information
 *         cannot be retrieved. The returned structure is dynamically allocated
 *         and should be freed by the caller.
 */
ImageInfo* browser_get_file_info(const FileBrowser *browser);
/**
 * @brief Retrieves a list of all file paths currently managed by the browser.
 * 
 * @param browser A pointer to the constant `FileBrowser` instance.
 * @return A `GList` containing `gchar*` file paths. The returned list and
 *         its string elements are internal copies and should not be modified
 *         or freed by the caller.
 */
GList* browser_get_all_files(const FileBrowser *browser);

// Utility functions
/**
 * @brief Checks if the current file pointer is at the first file in the list.
 * 
 * @param browser A pointer to the constant `FileBrowser` instance.
 * @return `TRUE` if the current file is the first, `FALSE` otherwise.
 */
gboolean browser_is_at_first(const FileBrowser *browser);
/**
 * @brief Checks if the current file pointer is at the last file in the list.
 * 
 * @param browser A pointer to the constant `FileBrowser` instance.
 * @return `TRUE` if the current file is the last, `FALSE` otherwise.
 */
gboolean browser_is_at_last(const FileBrowser *browser);
/**
 * @brief Resets the browser's state, clearing the file list and directory path.
 * 
 * After calling this function, the browser will be in its initial empty state.
 * 
 * @param browser A pointer to the `FileBrowser` instance.
 */
void browser_reset(FileBrowser *browser);

#endif // BROWSER_H
