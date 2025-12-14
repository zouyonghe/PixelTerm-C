# Changelog

- v1.0.16: File manager now sorts entries in AaBb order (uppercase before lowercase within each letter) and skips `$…` system items, matching `ls` ordering while keeping directories grouped ahead of files.
- v1.0.15: Fix image loading for extremely long file paths by streaming via GIO instead of direct file opens, resolving “Filename too long” errors on deep/UTF-8 paths.
- v1.0.14: File manager header now truncates and centers long/UTF-8 paths correctly, and long file/folder names use smarter centering-friendly truncation.
- v1.0.13: Preview grid UX polish (single render on entry, predictable zoom with min 2 columns, better paging/wrap), preloader now truly pauses/disables without burning CPU and fixes cache ownership, and CLI error messages are clearer for help/version/argument cases.
