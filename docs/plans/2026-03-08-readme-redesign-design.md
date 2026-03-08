# 2026-03-08 README Redesign and Multilingual Docs Design

## Goal

Redesign the repository README set so the project presents itself like a polished, user-facing open source tool rather than a developer note dump. The new documentation should help first-time users understand what PixelTerm-C does, how it looks, how to install it, and where to go next, while keeping developer-specific detail available but secondary.

## Current Documentation Problems

- `README.md` is informative but reads more like a feature inventory than a product-facing landing page.
- The current top section leads with implementation history and performance comparisons before clearly explaining the user value.
- The README contains a manually curated “recommended terminals” list that is subjective and likely to drift over time.
- `README_zh.md` is structurally aligned with the English version, but some phrasing still feels translation-shaped rather than native.
- There is no Japanese README yet.
- Terminal graphics protocol compatibility is important for users, but it is currently mixed into README prose instead of living in a factual compatibility document.

## Approaches Considered

### 1. Minimal Cleanup Only

Tighten wording, refresh badges, and add a Japanese translation without changing the README structure.

- Pros: low effort, low risk.
- Cons: misses the opportunity to make the project feel significantly more approachable and mature.

### 2. User-Facing README Redesign With Shared Multilingual Structure (Chosen)

Redesign the English README around user needs, then mirror the same information architecture in Chinese and Japanese while allowing each language to read naturally rather than as a line-by-line translation.

- Pros: better first impression, cleaner navigation, easier long-term maintenance, and clearer separation between user-facing messaging and technical reference material.
- Cons: requires careful editing across multiple files to keep content aligned without sounding mechanically translated.

### 3. Split README + Website-Style Docs Home

Shrink the README dramatically and move most content into multiple standalone docs pages.

- Pros: cleaner homepage, easier topical maintenance.
- Cons: too much navigation friction for first-time GitHub visitors; overkill for the current repository scale.

## Chosen Design

### Primary Audience

The redesigned README set is primarily for users and evaluators, not contributors. The top half of each README should answer:

1. What is PixelTerm-C?
2. Why would I use it?
3. What does it look like?
4. How do I install and run it quickly?
5. Where do I go for deeper usage and compatibility details?

Contributor-oriented build details remain present, but lower on the page.

### README Information Architecture

All three README files should use the same overall structure:

1. Title + concise subtitle
2. Badge row
3. Language switch row
4. Short value-oriented intro paragraph
5. Screenshot / visual preview
6. “Why PixelTerm-C” or highlights section
7. Install section
8. Quick start / first run
9. Supported content and compatibility overview
10. Configuration / docs navigation
11. Build from source
12. License

This keeps the user path clear while still giving advanced users enough detail.

### Content Direction

- De-emphasize “this is the C port of the Python version” as the main narrative.
- Emphasize concrete user value: terminal-native browsing for images, video, and books; fast navigation; multi-format support; keyboard and mouse interaction; configurable rendering.
- Keep performance claims, but only where they support user expectations rather than dominate the page.
- Replace long bullet inventory with clearer grouped highlights.

### Terminal Recommendations Removal

The “recommended terminals” list will be removed from README files.

Reasoning:

- It is subjective.
- It is likely to age poorly.
- It mixes benchmark-like opinion into the landing page.

Instead, the README should link to a dedicated compatibility document that records terminal graphics protocol support in a more factual format.

### New Compatibility Document

Create a new doc:

- `docs/TERMINAL_PROTOCOL_SUPPORT.md`

This document should provide a table-oriented compatibility reference for users, including:

- terminal name
- protocol or render path notes (kitty graphics, sixel, iTerm2 inline image behavior, ANSI/TrueColor fallback, etc.)
- current support status such as tested / partially tested / unverified
- practical notes about image fidelity, color behavior, or known limitations where appropriate

The goal is not to overstate certainty, but to provide a factual compatibility reference that the README can safely link to.

### Multilingual Strategy

Files to maintain:

- `README.md`
- `README_zh.md`
- `README_ja.md` (new)

Rules:

- Keep the same information architecture across all languages.
- Do not translate line by line when that makes the text feel unnatural.
- Prefer native technical writing tone for each language.
- Keep terminology aligned where it matters (formats, options, doc links), but adapt phrasing to local reading habits.

#### English

- Concise, product-facing, confident without sounding promotional.

#### Chinese

- Natural Simplified Chinese technical style.
- Avoid direct English sentence order where it feels stiff.

#### Japanese

- Natural OSS/technical documentation tone.
- Clear and polite, but not overly formal marketing language.

### Visual Style in GitHub Markdown

The redesign should remain GitHub-native rather than HTML-heavy.

- Keep layout clean and scannable.
- Use short sections with strong headings.
- Use tables only where they clearly help, such as install matrix or compatibility summary.
- Use one screenshot near the top to establish the product visually.
- Keep badge usage modest and purposeful.

### Scope Boundaries

This pass should:

- redesign README presentation
- improve multilingual phrasing
- add Japanese README
- add terminal protocol support documentation

This pass should not:

- redesign the project website (none exists here)
- add speculative compatibility claims without support notes
- turn README into contributor-only architecture documentation

## Validation Plan

- Read each README top-to-bottom as a first-time user flow.
- Ensure all three README files link to each other correctly.
- Ensure README links to `USAGE*`, `CONTROLS*`, and the new protocol support doc correctly.
- Confirm the Chinese and Japanese versions read naturally rather than as literal mirrors.
- Confirm obsolete “recommended terminals” prose is removed.

## Deliverables

- updated `README.md`
- updated `README_zh.md`
- new `README_ja.md`
- new `docs/TERMINAL_PROTOCOL_SUPPORT.md`
- updated cross-links between README files and supporting docs
