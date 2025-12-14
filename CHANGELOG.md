# Changelog

- v1.1.0: 修复文件浏览器中中文文件名居中显示问题，使用正确的UTF-8显示宽度计算替代字节数计算，改进文件名截断逻辑确保多字节字符完整性。
- v1.0.20: Fix preview grid pagination to allow sparse last pages, improve input handling with timeout for escape sequences, fix image file detection using full paths, and add proper error handling for renderer initialization in preloader.
- v1.0.19: Add support for image files without extensions by implementing magic number detection for JPEG, PNG, GIF, WebP, BMP, and TIFF formats, automatically detecting image files without file extensions by reading file headers while maintaining backward compatibility with extension-based detection.
- v1.0.18: Add wrap-around navigation in file manager (up from top jumps to bottom, down from bottom jumps to top), fix preview zoom jumping issues by using floating-point precision and proper rounding, improve terminal geometry fallback and Konsole handling, clamp terminal cell aspect ratio to sane range, and prefer local chafa at runtime.
- v1.0.16: File manager now sorts entries in AaBb order (uppercase before lowercase within each letter) and skips `$…` system items, matching `ls` ordering while keeping directories grouped ahead of files.
- v1.0.15: Fix image loading for extremely long file paths by streaming via GIO instead of direct file opens, resolving “Filename too long” errors on deep/UTF-8 paths.
- v1.0.14: File manager header now truncates and centers long/UTF-8 paths correctly, and long file/folder names use smarter centering-friendly truncation.
- v1.0.13: Preview grid UX polish (single render on entry, predictable zoom with min 2 columns, better paging/wrap), preloader now truly pauses/disables without burning CPU and fixes cache ownership, and CLI error messages are clearer for help/version/argument cases.
