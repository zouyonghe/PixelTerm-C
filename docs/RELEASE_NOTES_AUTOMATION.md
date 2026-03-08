# Release Notes Automation

## When generation runs

Release note generation belongs in `.github/workflows/release.yml` inside the `create-release` job, after `Download artifacts` and before `Create Release` publishes the GitHub Release.

That placement keeps the existing build matrix and artifact uploads untouched while ensuring release-note markdown is ready before publishing.

The job now also performs a full repository checkout before generation so the script can inspect git tags and commit history.

## Required permissions and runner state

- `create-release` keeps `permissions.contents: write` so the final `softprops/action-gh-release` step can publish the GitHub Release.
- The generator reads local git history and tags from the checked-out repository, and CI enables GitHub API lookup so release notes can prefer merged PR metadata over raw commit titles.
- `actions/checkout@v4` must use `fetch-depth: 0` in `create-release` so previous-tag resolution can inspect full tag ancestry instead of a shallow clone.

## Workflow inputs passed to the generator

The workflow now invokes `scripts/generate_release_notes.py` with these inputs:

- `current_tag`: resolved from the pushed tag name, or from the required `workflow_dispatch` `version` input for manual runs
- `previous_tag`: currently an empty workflow placeholder; when omitted on the CLI, the script auto-resolves the previous reachable release tag from git history
- `output_file`: a temporary markdown path on the runner (`$RUNNER_TEMP/release-notes.md`)
- `repo`: `.` from the checked-out repository root
- `GITHUB_TOKEN` and `GITHUB_REPOSITORY`: provided in CI so the generator can query associated merged PRs when PR-aware mode is enabled

For `workflow_dispatch`, the manual `version` input is required. The workflow should fail fast instead of silently treating a branch name as a release tag. If the workflow is manually run against an actual tag ref, the `version` input must match that tag.

The current CI command shape is:

```bash
python3 scripts/generate_release_notes.py \
  --current-tag "$current_tag" \
  --output "$output_file" \
  --repo .
```

CI also enables PR-aware mode with:

```bash
RELEASE_NOTES_USE_GITHUB_API=1
GITHUB_TOKEN=$GITHUB_TOKEN
GITHUB_REPOSITORY=$GITHUB_REPOSITORY
```

If a future workflow step resolves `previous_tag` explicitly, CI can append:

```bash
--previous-tag "$previous_tag"
```

## Output consumption

The generator writes markdown to `output_file`.

The `Create Release` step now consumes that file with `body_path`, so `softprops/action-gh-release` publishes generated release notes together with the existing binary artifacts.

Because `body_path` is used on each run, rerunning the workflow for an existing release will replace the release body with the newly generated markdown rather than preserving any manual edits made on GitHub.

The current expected markdown shape is the script skeleton:

```markdown
## Highlights

## Improvements

## Fixes

## Docs & Maintenance
```

## Current publish behavior

- Tag pushes (`refs/tags/v*`) still create GitHub Releases.
- Manual `workflow_dispatch` runs now execute release-note generation, but they still do not publish a GitHub Release unless the workflow is running on an actual tag ref. When run on a tag ref, the manual `version` input must match that ref.

## Current behavior summary

- Build and artifact behavior remains unchanged.
- CI wires the generator into the release job and passes its markdown to the GitHub release action through `body_path`.
- The script auto-resolves `previous_tag` from reachable release tags when the workflow does not provide one explicitly.
- Local runs work without GitHub API access; CI enables PR-aware note generation so merged PR metadata can be preferred over commit fallback output.
