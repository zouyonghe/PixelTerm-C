# README Redesign and Multilingual Docs Implementation Plan

> Work through this plan task-by-task and verify each step before moving on.

**Goal:** Redesign the README set to be more polished and user-facing, naturalize the Chinese README, add a Japanese README, and move terminal compatibility details into a dedicated protocol support document.

**Architecture:** Keep one shared documentation structure across languages while allowing each README to read naturally in its own language. Move subjective terminal guidance out of the README and into a factual compatibility reference document.

**Tech Stack:** Markdown, GitHub-flavored Markdown, repository documentation files

---

### Task 1: Audit Current README Content and Link Targets

**Files:**
- Modify: `README.md`
- Modify: `README_zh.md`
- Create: `README_ja.md`
- Create: `docs/TERMINAL_PROTOCOL_SUPPORT.md`
- Test: `README.md`
- Test: `README_zh.md`

**Step 1: Identify current README content that needs moving or rewriting**

Check the current README files for terminal recommendation prose, old framing, and sections that should move lower in the page.

**Step 2: Verify target support docs exist before relinking**

Confirm that the existing usage, controls, and changelog documents are present before adding new README links.

**Step 3: Draft the shared README structure in-place as headings only**

Apply a minimal structural rewrite in the three README files using this section order:

```md
# Title
badges
language switch
short intro
## Why PixelTerm-C
## Screenshot
## Install
## Quick Start
## Formats and Compatibility
## Configuration
## Documentation
## Build from Source
## License
```

**Step 4: Verify the new section skeleton is present**

Confirm that the rewritten English and Chinese README files follow the new top-level section order.

### Task 2: Rewrite the English README for a User-Facing First Impression

**Files:**
- Modify: `README.md`

**Step 1: Identify old English README framing before rewriting**

Check the current English README for old recommendation wording and implementation-history framing.

**Step 2: Rewrite the top section with concise user-facing copy**

Replace the old intro and feature inventory with content shaped like:

```md
PixelTerm-C is a terminal-native browser for images, video, and books.
It is built for people who want fast media browsing without leaving the terminal.
```

and grouped highlights such as:

```md
- Images, animated GIFs, video, and books in one workflow
- Keyboard and mouse navigation across single-view and grid modes
- Fast rendering with configurable output and preload behavior
```

**Step 3: Remove the recommended-terminal list and replace it with a doc link**

Add a compatibility pointer like:

```md
See `docs/TERMINAL_PROTOCOL_SUPPORT.md` for tested terminal protocol notes.
```

**Step 4: Verify removed wording is gone**

Confirm that the old terminal recommendation wording is no longer present in `README.md`.

### Task 3: Naturalize the Chinese README

**Files:**
- Modify: `README_zh.md`

**Step 1: Identify translation-shaped wording in the Chinese README**

Check the current Chinese README for old overview wording, recommendation prose, and sections that should be rewritten more naturally.

**Step 2: Rewrite Chinese copy to sound native rather than mirrored**

Use naturally phrased Simplified Chinese for the intro, highlights, install, and docs sections. Keep the same section order as `README.md`, but adapt wording rather than translating literally.

Example direction:

```md
PixelTerm-C 是一个面向终端用户的图像、视频和电子书浏览工具。
如果你希望在终端里快速浏览媒体内容，它会比“查看器拼接脚本”更直接。
```

**Step 3: Replace the terminal list with the compatibility doc link**

Point to:

```md
`docs/TERMINAL_PROTOCOL_SUPPORT.md`
```

with natural Chinese explanatory text.

**Step 4: Verify removed recommendation wording is gone**

Confirm that the old terminal recommendation wording is no longer present in `README_zh.md`.

### Task 4: Add a Japanese README

**Files:**
- Create: `README_ja.md`
- Modify: `README.md`
- Modify: `README_zh.md`

**Step 1: Create the new README with the shared section structure**

Include:

```md
* [English](README.md) | [中文](README_zh.md) | 日本語 *
```

and Japanese sections matching the shared README architecture.

**Step 2: Write natural Japanese user-facing copy**

Keep tone technical and readable, for example:

```md
PixelTerm-C は、ターミナル内で画像・動画・電子書籍を閲覧するための高速ブラウザです。
```

Avoid direct machine-translation phrasing and keep install/build terminology consistent.

**Step 3: Update language switch rows in the existing README files**

Add `README_ja.md` to the language navigation in both `README.md` and `README_zh.md`.

**Step 4: Verify language cross-links**

Confirm that each README links to the other language variants correctly.

### Task 5: Add Terminal Protocol Support Documentation

**Files:**
- Create: `docs/TERMINAL_PROTOCOL_SUPPORT.md`
- Modify: `README.md`
- Modify: `README_zh.md`
- Modify: `README_ja.md`

**Step 1: Create the compatibility document skeleton**

Use a factual structure like:

```md
# Terminal Protocol Support

## How to read this page

| Terminal | Protocol / Path | Status | Notes |
|----------|------------------|--------|-------|
```

**Step 2: Fill in currently known terminal entries conservatively**

Include the terminals already referenced in the repository and describe them in factual terms without “best/recommended” language.

**Step 3: Add README links to the new compatibility document**

Use language-appropriate link text in all three README files.

**Step 4: Verify links and remove old terminal recommendation phrasing**

Confirm that the compatibility doc is linked from all README files and that old recommendation phrasing is absent.

### Task 6: Final README and Doc Consistency Verification

**Files:**
- Modify: `README.md` (only if verification finds a real issue)
- Modify: `README_zh.md` (only if verification finds a real issue)
- Modify: `README_ja.md` (only if verification finds a real issue)
- Modify: `docs/TERMINAL_PROTOCOL_SUPPORT.md` (only if verification finds a real issue)

**Step 1: Run a link and phrase sanity check**

Check the new README files and support docs for stale recommendation wording or assistant-targeted phrasing.

**Step 2: Read all three README files top-to-bottom for structure parity**

Confirm that all three language variants follow comparable section structure and information flow.

**Step 3: Review final diff scope**

Confirm that the final change set is limited to README files, the compatibility doc, and the design/plan docs for this work.
