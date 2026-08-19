# PixelTerm-C Multidimensional Code Review

Date: 2026-08-19  
Reviewed commit: `6943dbb5a442c633925d6974831a4bdcfce9b6d3` (`main`)  
Review scope: build and CI, tests, static analysis, memory and integer safety,
concurrency, architecture, performance, portability, release/install flow,
supply-chain security, and documentation consistency.

## Executive Summary

The reviewed commit has a healthy Linux verification baseline: the matching
GitHub Actions run passed warning-as-error compilation, the full test suite,
AddressSanitizer tests, and cppcheck. No confirmed critical vulnerability or
immediate release-blocking memory corruption was found during source review.
Several findings from the older `CODE_REVIEW_REPORT.md` have since been fixed,
including preloader locking/queue cleanup and checked MuPDF target dimensions.

The most important remaining risk is distribution portability. Linux release
binaries are built dynamically in rolling Arch Linux containers and then
presented as standalone Linux downloads, without a compatibility smoke-test on
Ubuntu or Debian. Concurrency assurance is also incomplete because the worker-
heavy video and preloader paths are covered by ASan but not ThreadSanitizer.
The v1.8.2 release additionally exposed a process defect: artifact creation
succeeded while the post-release documentation sync failed, leaving tracked
version references at v1.8.1.

Overall assessment: **B+ / good engineering baseline, with release portability
and assurance work required for a stronger production distribution claim.**

## Verification Evidence

- Repository: `zouyonghe/PixelTerm-C`
- GitHub Actions run: `30453551105`
- Run URL: <https://github.com/zouyonghe/PixelTerm-C/actions/runs/30453551105>
- Linux `-Werror` build: passed
- Full test suite: passed
- AddressSanitizer debug tests: passed
- cppcheck: passed
- macOS job: skipped for that push event by workflow condition
- Approximate review metrics:
  - 35,589 C/header/test lines
  - 304 GLib test registrations
  - 1,489 test assertions
  - 338 public declarations

The local review environment was Termux/Android and did not contain `make`, a C
compiler, Python, or cppcheck. Local tool absence was treated as an environment
limitation, not a project failure; build/test conclusions above come from the
matching immutable commit's GitHub Actions run.

## Findings

### F-01 — High — Linux release portability is not demonstrated

**Evidence**

- `.github/workflows/release.yml` builds Linux assets in `archlinux:latest` and
  `menci/archlinuxarm:base-devel`.
- The Makefile resolves Chafa, GLib, GDK-Pixbuf, FFmpeg, and optional MuPDF as
  normal dynamic dependencies.
- `scripts/install.sh` installs the resulting single binary but neither bundles
  nor diagnoses runtime libraries.
- No job downloads the final release artifact into Ubuntu/Debian and executes a
  smoke test.

**Risk**

A binary built against rolling Arch libraries may fail on common or older Linux
systems because of GLIBC, FFmpeg soname, Chafa, MuPDF, or other ABI differences.

**Remediation / acceptance criteria**

- [ ] Define and document a broader minimum Linux binary baseline. This is
      deferred because changing the existing Arch ABI would affect the current
      `pixelterm-c-bin` AUR packaging flow and requires a separately approved
      distribution design.
- [x] Retain the existing Arch Linux/Arch Linux ARM build environments rather
      than changing the release ABI as part of this remediation batch.
- [x] Download uploaded amd64/arm64 artifacts into matching Arch environments
      and test them with `ldd`, `pixelterm --version`, and `pixelterm --help`
      before release creation.
- [x] Make the installer report missing Linux runtime dependencies clearly.

### F-02 — Medium/High — No ThreadSanitizer concurrency baseline

**Evidence**

The video player uses decode/render workers, multiple queues, GLib timeouts,
three mutex domains, and condition variables. The preloader has another worker,
queue, cache, mutex, and condition variable. CI runs ASan, but not TSan or
UBSan. ASan does not detect ordinary C data races.

**Remediation / acceptance criteria**

- [x] Add a dedicated `-fsanitize=thread` target and Linux CI job.
- [ ] Add more explicit concurrent play/pause/stop/seek/resize/EOF stress tests
      beyond the current worker/locking regression suite. Preloader concurrent
      stop, paused-stop, repeated start/stop, and stop-time enqueue coverage is
      now included in the focused TSan suite.
- [x] Add a separate UndefinedBehaviorSanitizer target/job.
- [x] Document field ownership and lock ordering for video playback in
      `docs/development/VIDEO_PLAYER_THREADING.md`.

### F-03 — Medium — Release/document version synchronization failed

**Evidence**

- Latest release: v1.8.2, published 2026-07-20.
- Release run `29749025343` built all four platforms and created the release,
  but `sync-doc-versions` failed.
- The reviewed `main` still displayed v1.8.1 in README and tracked project docs.

**Remediation / acceptance criteria**

- [x] Synchronize all tracked references to v1.8.2.
- [x] Run the version-sync script tests in the standard test baseline.
- [x] Validate tracked version references before creating a formal release tag.
- [x] Reject out-of-sync formal tags before build instead of relying solely on
      the post-release direct push to `main`.

### F-04 — Medium — CI actions and build containers use mutable tags

**Evidence**

Actions use major tags such as `actions/checkout@v6` and
`softprops/action-gh-release@v2`; release containers use mutable image tags,
including `archlinux:latest`.

**Remediation / acceptance criteria**

- [x] Pin third-party actions to full commit SHAs, retaining version comments.
- [ ] Pin release container images by digest. The existing Arch build
      environments are intentionally retained to avoid changing the shipped ABI;
      digest pinning remains a build-hardening follow-up.
- [x] Add Dependabot configuration for controlled GitHub Actions updates.
- [ ] Record dependency versions and generate release provenance/SBOM.

### F-05 — Medium/Low — Checksums share the release trust domain

The installer correctly verifies SHA-256, which detects corruption and asset
mismatch. The binary and `SHA256SUMS`, however, come from the same GitHub
Release; compromise of release credentials could replace both.

**Remediation / acceptance criteria**

- [ ] Publish and verify signed assets or artifact attestations. GitHub build
      provenance generation is now configured for formal release assets;
      installer-side verification remains outstanding.
- [ ] Sign formal Git tags.
- [x] Keep write permission isolated to release creation/document-sync jobs;
      build and artifact verification jobs use default read-only permissions.
- [x] Show checksum verification in every manual install example, not only one
      architecture.

### F-06 — Medium/Low — Video internals bypass an explicit API boundary

`VideoPlayer` exposes FFmpeg objects, queues, workers, mutexes, renderer state,
and layout/statistics fields in a public header. Several app/input modules read
or mutate those fields directly, making thread ownership and locking contracts
implicit.

**Remediation / acceptance criteria**

- [ ] Make `VideoPlayer` opaque to ordinary consumers.
- [ ] Move its definition to an internal header.
- [x] Add synchronized APIs for current file state, stats, color enhancement,
      frame bounds/cache, and protocol cycling.
- [ ] Document thread context, locking, and ownership for each public API.
      The added VideoPlayer APIs are documented; remaining headers are pending.

### F-07 — Low — Public API documentation does not meet project policy

`CONTRIBUTING.md` requires Doxygen comments for all public functions, but many
public headers (including video, app, terminal, and UI APIs) have no Doxygen
blocks.

**Remediation / acceptance criteria**

- [ ] Distinguish public and internal headers.
- [ ] Document all supported public APIs and thread/ownership semantics.
      VideoPlayer public APIs are now documented; remaining app/terminal/UI
      headers still require coverage.
- [ ] Add a Doxygen warning check, or relax the written policy to match the
      intended scope.

### F-08 — Low — No coverage, fuzzing, or performance regression baseline

The test suite is broad, but CI does not publish code coverage, run fuzzers, or
track startup/navigation/video performance. Open issue #26 reports very low
video FPS, making a repeatable benchmark particularly useful.

**Remediation / acceptance criteria**

- [x] Establish gcov/gcovr reporting and upload the HTML/XML report from CI.
- [ ] Fuzz CLI/config, terminal input, file-type detection, and protocol parsing.
- [ ] Add large-directory and video playback benchmarks.
- [ ] Track render/decode FPS, queue waits, dropped frames, and peak memory.

## Positive Observations

- The reviewed Linux commit passes `-Werror`, tests, ASan, and cppcheck.
- No uses of `strcpy`, `strcat`, `sprintf`, `gets`, `system`, or `popen` were
  found in the C source scan.
- Pixel and rowstride handling uses checked multiplication and validation.
- Kitty SHM paths bound payload size and handle cleanup carefully; Android SHM
  transfer is currently disabled explicitly.
- MuPDF target dimensions now use `gint64` multiplication and a 4096-pixel cap.
- Preloader cache cleanup is lock-scoped and stop now clears queued work.
- Normal release builds enable stack protection, FORTIFY, RELRO, and immediate
  binding where supported.
- Tests are split into focused binaries and cover many CLI, media, preview,
  file-manager, and paging regressions.

## Remediation Order

1. **P0 / next release:** F-03 version consistency and manual checksum docs;
   then F-01 release artifact compatibility tests.
2. **P1 / near term:** F-02 TSan/UBSan, F-04 immutable CI dependencies, and
   F-05 provenance/signing.
3. **P2 / medium term:** F-06 API encapsulation, F-07 API documentation, and
   F-08 coverage/fuzz/performance infrastructure.

## Progress Log

- 2026-08-19: Review recorded at commit `6943dbb`; remediation branch
  `fix/review-remediation` created.
- 2026-08-19: Completed the first remediation batch: tracked docs now use
  v1.8.2, maintenance-script tests are part of `make test`, release tags are
  checked against tracked docs before build, manual checksum examples cover all
  four platform assets, GitHub Actions are pinned to commit SHAs, and a
  Dependabot GitHub Actions update configuration was added.
- 2026-08-19: After checking the existing `pixelterm-c` and `pixelterm-c-bin`
  AUR flows, the proposed Ubuntu 24.04 release ABI migration was reverted. Linux
  release builds retain their prior Arch Linux/Arch Linux ARM environments.
  Uploaded artifacts still gate release creation on runtime dependency,
  version, and help smoke tests in matching Arch environments, and the installer
  rejects missing Linux runtime libraries before installation.
- 2026-08-19: Added `video_player_is_loaded_file()` so rendering code no longer
  reads the internal filepath pointer or depends on its ownership; load-time
  path publication is now synchronized by the state mutex.
- 2026-08-19: Moved video protocol cycling and its renderer locking into a
  documented `video_player_cycle_protocol()` API, preserving the existing
  text/sixel/iTerm2/kitty order and removing the remaining production-code
  access to `VideoPlayer::renderer` and `render_mutex`.
- 2026-08-19: Serialized preloader start/stop lifecycle publication, fixed
  paused-worker stop wakeup and concurrent double-join risk, rejected new work
  during shutdown, and added four focused lifecycle regressions to the TSan
  suite. Local Clang `-Werror` tests increased to 315 main tests and passed; the
  focused preloader lifecycle set also passed 100 repeated local runs.
- 2026-08-19: Added separate ThreadSanitizer and UndefinedBehaviorSanitizer
  Makefile targets and Linux CI matrix jobs. The existing warning-clean full
  suite also passed locally on Termux/Android with Clang: 309 main tests, 14
  file-manager tests, 13 preview-grid tests, 8 book-preview/page tests, and 49
  Python maintenance tests.
- 2026-08-19: Added thread-safe VideoPlayer setters/snapshot accessors for
  statistics, color enhancement, and cached frame layout; app/input modules no
  longer access those fields directly. The worker stop flag is now atomic, and
  the VideoPlayer lock-order/ownership contract is documented.
- 2026-08-19: Static Analysis run `32203958031` passed warning-clean build and
  tests, ASan, cppcheck, focused TSan, UBSan, and coverage generation. The
  initial gcovr source baseline is 54.32% lines (5719/10528) and 39.78%
  branches (3134/7879); reports are uploaded as Cobertura XML and HTML details.
- 2026-08-19: Release workflow dispatch run `32206565028` passed the retained
  Arch Linux amd64 and Arch Linux ARM arm64 builds, macOS amd64/arm64 builds,
  uploaded-artifact smoke tests in matching Arch environments, checksum
  generation, and the non-tag release preparation path. The workflow now
  bootstraps Git before checkout because `docs/ export-ignore` makes the REST
  archive fallback unsuitable for version-reference validation.
- 2026-08-19: Static Analysis run `32203379170` passed warning-clean build and
  tests, ASan, cppcheck, the focused project concurrency TSan suite, and UBSan.
- 2026-08-19: Configured GitHub build provenance attestation for checksums and
  all formal release assets; consumer/installer verification remains open.
- 2026-08-19: Remaining unchecked items are intentionally not marked complete;
  sanitizer stress expansion, provenance verification/signing, full
  VideoPlayer opacity,
  API documentation, coverage, fuzzing, and performance baselines remain in
  progress.
