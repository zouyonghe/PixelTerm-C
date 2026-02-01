#ifndef MEDIA_UTILS_H
#define MEDIA_UTILS_H

#include <glib.h>

/**
 * @brief Media file type classification
 * 
 * Enumeration of supported media types for classification
 * and appropriate handler selection.
 */
typedef enum {
    MEDIA_KIND_UNKNOWN = 0,        /**< Unknown or unsupported media type */
    MEDIA_KIND_IMAGE,              /**< Static image (JPG, PNG, BMP, etc.) */
    MEDIA_KIND_ANIMATED_IMAGE,     /**< Animated image (GIF, animated WebP) */
    MEDIA_KIND_VIDEO               /**< Video file (MP4, MKV, AVI, etc.) */
} MediaKind;

/**
 * @brief Classifies a media file by its type
 * 
 * Analyzes the file extension and content to determine the media type.
 * This is used to select the appropriate handler (image viewer, GIF player,
 * or video player).
 * 
 * @param path Path to the media file
 * @return MediaKind classification of the file
 * 
 * @note Returns MEDIA_KIND_UNKNOWN if file doesn't exist or type cannot be determined
 * @see media_is_image(), media_is_animated_image(), media_is_video()
 */
MediaKind media_classify(const char *path);

/**
 * @brief Checks if media kind is a static image
 * 
 * @param kind Media kind to check
 * @return TRUE if kind is MEDIA_KIND_IMAGE, FALSE otherwise
 */
gboolean media_is_image(MediaKind kind);

/**
 * @brief Checks if media kind is an animated image
 * 
 * @param kind Media kind to check
 * @return TRUE if kind is MEDIA_KIND_ANIMATED_IMAGE, FALSE otherwise
 */
gboolean media_is_animated_image(MediaKind kind);

/**
 * @brief Checks if media kind is a video
 * 
 * @param kind Media kind to check
 * @return TRUE if kind is MEDIA_KIND_VIDEO, FALSE otherwise
 */
gboolean media_is_video(MediaKind kind);

#endif
