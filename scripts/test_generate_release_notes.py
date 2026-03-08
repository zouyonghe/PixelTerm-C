import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve().parent / "generate_release_notes.py"
MODULE_SPEC = importlib.util.spec_from_file_location(
    "generate_release_notes", SCRIPT_PATH
)
assert MODULE_SPEC is not None and MODULE_SPEC.loader is not None
release_notes = importlib.util.module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(release_notes)


def git(repo_path: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        cwd=repo_path,
        text=True,
        capture_output=True,
        check=True,
    )


def run_script(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT_PATH), *args],
        text=True,
        capture_output=True,
    )


class GenerateReleaseNotesCLITest(unittest.TestCase):
    def create_repo(self) -> Path:
        repo_path = Path(tempfile.mkdtemp())
        self.addCleanup(
            lambda: subprocess.run(["rm", "-rf", str(repo_path)], check=False)
        )
        git(repo_path, "init")
        git(repo_path, "config", "user.name", "Test User")
        git(repo_path, "config", "user.email", "test@example.com")
        return repo_path

    def commit_file(
        self,
        repo_path: Path,
        tracked_file: Path,
        message: str,
        content: str,
        tag: str | None = None,
    ) -> None:
        tracked_file.write_text(content, encoding="utf-8")
        git(repo_path, "add", tracked_file.name)
        git(repo_path, "commit", "-m", message)
        if tag is not None:
            git(repo_path, "tag", tag)

    def test_fails_clearly_when_required_args_are_missing(self) -> None:
        result = run_script()

        self.assertEqual(result.returncode, 2)
        self.assertIn("--current-tag", result.stderr)
        self.assertIn("--output", result.stderr)

    def test_writes_grouped_markdown_for_explicit_tag_range(self) -> None:
        repo_path = self.create_repo()
        output_path = repo_path / "release_notes.md"
        tracked_file = repo_path / "notes.txt"

        self.commit_file(
            repo_path, tracked_file, "Initial release", "initial\n", tag="v1.0.0"
        )
        self.commit_file(
            repo_path, tracked_file, "Ship update", "initial\nupdate\n", tag="v1.0.1"
        )

        result = run_script(
            "--current-tag",
            "v1.0.1",
            "--previous-tag",
            "v1.0.0",
            "--output",
            str(output_path),
            "--repo",
            str(repo_path),
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertTrue(output_path.exists())
        self.assertIn("Commit range: v1.0.0..v1.0.1", result.stdout)
        self.assertIn("Read 1 commits", result.stdout)
        self.assertIn("Grouped 0 PRs and 1 commit fallback entries", result.stdout)
        self.assertEqual(
            output_path.read_text(encoding="utf-8"),
            "## Highlights\n\n"
            "## Improvements\n"
            "- Ships update\n\n"
            "## Fixes\n\n"
            "## Docs & Maintenance\n",
        )

    def test_generates_grouped_markdown_from_commit_fallback(self) -> None:
        repo_path = self.create_repo()
        output_path = repo_path / "release_notes.md"
        tracked_file = repo_path / "notes.txt"

        self.commit_file(
            repo_path, tracked_file, "Initial release", "initial\n", tag="v1.0.0"
        )
        self.commit_file(
            repo_path,
            tracked_file,
            "feat: add kitty image protocol support",
            "initial\nfeature\n",
        )
        self.commit_file(
            repo_path,
            tracked_file,
            "perf: improve preview redraw performance",
            "initial\nfeature\nperf\n",
        )
        self.commit_file(
            repo_path,
            tracked_file,
            "fix: resolve crash when preview metadata is missing",
            "initial\nfeature\nperf\nfix\n",
        )
        self.commit_file(
            repo_path,
            tracked_file,
            "docs: update release workflow notes",
            "initial\nfeature\nperf\nfix\ndocs\n",
            tag="v1.0.1",
        )

        result = run_script(
            "--current-tag",
            "v1.0.1",
            "--previous-tag",
            "v1.0.0",
            "--output",
            str(output_path),
            "--repo",
            str(repo_path),
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Grouped 0 PRs and 4 commit fallback entries", result.stdout)
        self.assertEqual(
            output_path.read_text(encoding="utf-8"),
            "## Highlights\n"
            "- Adds kitty image protocol support\n\n"
            "## Improvements\n"
            "- Improves preview redraw performance\n\n"
            "## Fixes\n"
            "- Resolves crash when preview metadata is missing\n\n"
            "## Docs & Maintenance\n"
            "- Updates release workflow notes\n",
        )

    def test_resolves_previous_tag_automatically_when_omitted(self) -> None:
        repo_path = self.create_repo()
        output_path = repo_path / "release_notes.md"
        tracked_file = repo_path / "notes.txt"

        self.commit_file(
            repo_path, tracked_file, "First release", "one\n", tag="v1.0.0"
        )
        self.commit_file(
            repo_path, tracked_file, "Second release", "one\ntwo\n", tag="v1.0.1"
        )
        self.commit_file(
            repo_path, tracked_file, "Third release", "one\ntwo\nthree\n", tag="v1.0.2"
        )

        result = run_script(
            "--current-tag",
            "v1.0.2",
            "--output",
            str(output_path),
            "--repo",
            str(repo_path),
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Resolved previous tag automatically: v1.0.1", result.stdout)
        self.assertIn("Commit range: v1.0.1..v1.0.2", result.stdout)
        self.assertIn("Read 1 commits", result.stdout)

    def test_uses_first_release_fallback_when_no_previous_tag_exists(self) -> None:
        repo_path = self.create_repo()
        output_path = repo_path / "release_notes.md"
        tracked_file = repo_path / "notes.txt"

        self.commit_file(repo_path, tracked_file, "Initial setup", "one\n")
        self.commit_file(
            repo_path, tracked_file, "Prepare first release", "one\ntwo\n", tag="v1.0.0"
        )

        result = run_script(
            "--current-tag",
            "v1.0.0",
            "--output",
            str(output_path),
            "--repo",
            str(repo_path),
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn(
            "No previous tag found; using first-release fallback.", result.stdout
        )
        self.assertIn("Commit range: first-release..v1.0.0", result.stdout)
        self.assertIn("Read 2 commits", result.stdout)

    def test_rejects_explicit_previous_tag_that_is_not_an_ancestor(self) -> None:
        repo_path = self.create_repo()
        output_path = repo_path / "release_notes.md"
        tracked_file = repo_path / "notes.txt"

        self.commit_file(
            repo_path, tracked_file, "Initial release", "one\n", tag="v1.0.0"
        )
        self.commit_file(
            repo_path, tracked_file, "Work in progress", "one\ntwo\n", tag="v1.1.0"
        )
        git(repo_path, "checkout", "-b", "hotfix", "v1.0.0")
        self.commit_file(
            repo_path, tracked_file, "Hotfix release", "one\nhotfix\n", tag="v1.0.1"
        )

        result = run_script(
            "--current-tag",
            "v1.1.0",
            "--previous-tag",
            "v1.0.1",
            "--output",
            str(output_path),
            "--repo",
            str(repo_path),
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("is not an ancestor of current tag", result.stderr)

    def test_prefers_ancestry_for_previous_tag_resolution(self) -> None:
        repo_path = self.create_repo()
        output_path = repo_path / "release_notes.md"
        tracked_file = repo_path / "notes.txt"

        self.commit_file(
            repo_path, tracked_file, "Initial release", "one\n", tag="v1.0.0"
        )
        self.commit_file(
            repo_path, tracked_file, "Release candidate", "one\nrc\n", tag="v1.0.1-rc1"
        )
        self.commit_file(
            repo_path, tracked_file, "Final release", "one\nrc\nfinal\n", tag="v1.0.1"
        )

        result = run_script(
            "--current-tag",
            "v1.0.1",
            "--output",
            str(output_path),
            "--repo",
            str(repo_path),
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Resolved previous tag automatically: v1.0.1-rc1", result.stdout)
        self.assertIn("Commit range: v1.0.1-rc1..v1.0.1", result.stdout)

    def test_ignores_non_release_tags_when_resolving_previous_tag(self) -> None:
        repo_path = self.create_repo()
        output_path = repo_path / "release_notes.md"
        tracked_file = repo_path / "notes.txt"

        self.commit_file(
            repo_path, tracked_file, "Initial release", "one\n", tag="v1.0.0"
        )
        self.commit_file(
            repo_path, tracked_file, "Internal milestone", "one\ninternal\n"
        )
        git(repo_path, "tag", "checkpoint")
        self.commit_file(
            repo_path,
            tracked_file,
            "Patch release",
            "one\ninternal\npatch\n",
            tag="v1.0.1",
        )

        result = run_script(
            "--current-tag",
            "v1.0.1",
            "--output",
            str(output_path),
            "--repo",
            str(repo_path),
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Resolved previous tag automatically: v1.0.0", result.stdout)
        self.assertNotIn("checkpoint", result.stdout)

    def test_rejects_identical_previous_and_current_tags(self) -> None:
        repo_path = self.create_repo()
        output_path = repo_path / "release_notes.md"
        tracked_file = repo_path / "notes.txt"

        self.commit_file(
            repo_path, tracked_file, "Initial release", "one\n", tag="v1.0.0"
        )

        result = run_script(
            "--current-tag",
            "v1.0.0",
            "--previous-tag",
            "v1.0.0",
            "--output",
            str(output_path),
            "--repo",
            str(repo_path),
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must differ from current tag", result.stderr)

    def test_fails_clearly_when_repo_path_is_not_a_git_repo(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_path = Path(temp_dir)
            output_path = repo_path / "release_notes.md"

            result = run_script(
                "--current-tag",
                "v1.0.0",
                "--output",
                str(output_path),
                "--repo",
                str(repo_path),
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Not a git repository", result.stderr)

    def test_fails_clearly_when_output_path_is_not_writable_file(self) -> None:
        repo_path = self.create_repo()
        tracked_file = repo_path / "notes.txt"
        self.commit_file(
            repo_path, tracked_file, "Initial release", "one\n", tag="v1.0.0"
        )
        self.commit_file(
            repo_path, tracked_file, "Patch release", "one\ntwo\n", tag="v1.0.1"
        )

        result = run_script(
            "--current-tag",
            "v1.0.1",
            "--previous-tag",
            "v1.0.0",
            "--output",
            str(repo_path),
            "--repo",
            str(repo_path),
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unable to write release notes", result.stderr)


if __name__ == "__main__":
    unittest.main()


class ReleaseNotesBehaviorTest(unittest.TestCase):
    def test_requires_explicit_opt_in_before_enabling_github_api(self) -> None:
        disabled = release_notes.resolve_github_api_config(
            {
                "GITHUB_REPOSITORY": "owner/repo",
                "github_token": "token",
            }
        )

        self.assertFalse(disabled.enabled)

        enabled = release_notes.resolve_github_api_config(
            {
                "RELEASE_NOTES_USE_GITHUB_API": "1",
                "GITHUB_REPOSITORY": "owner/repo",
                "github_token": "token",
            }
        )

        self.assertTrue(enabled.enabled)
        self.assertEqual(enabled.repository, "owner/repo")
        self.assertEqual(enabled.token, "token")

        with self.assertRaises(release_notes.ReleaseNotesError):
            release_notes.resolve_github_api_config(
                {
                    "RELEASE_NOTES_USE_GITHUB_API": "1",
                    "GITHUB_REPOSITORY": "owner/repo",
                }
            )

    def test_prefers_pull_requests_over_merge_commits_and_rewrites_entries(
        self,
    ) -> None:
        commits = [
            release_notes.CommitInfo(
                sha="1111111",
                parents=("aaaaaaa",),
                subject="feat(render): add kitty image protocol support",
            ),
            release_notes.CommitInfo(
                sha="2222222",
                parents=("bbbbbbb", "ccccccc"),
                subject="Merge pull request #42 from feature/kitty-protocol",
            ),
            release_notes.CommitInfo(
                sha="3333333",
                parents=("ddddddd",),
                subject="perf: improve preview redraw performance",
            ),
            release_notes.CommitInfo(
                sha="4444444",
                parents=("eeeeeee",),
                subject="fix: resolve crash when preview metadata is missing",
            ),
            release_notes.CommitInfo(
                sha="5555555",
                parents=("fffffff",),
                subject="docs: update release workflow notes",
            ),
        ]
        associated_prs = {
            "1111111": [
                release_notes.PullRequestInfo(
                    number=42,
                    title="feat(render): add kitty image protocol support (#42)",
                    labels=("feature",),
                    url="https://github.com/owner/repo/pull/42",
                )
            ],
            "2222222": [
                release_notes.PullRequestInfo(
                    number=42,
                    title="feat(render): add kitty image protocol support (#42)",
                    labels=("feature",),
                    url="https://github.com/owner/repo/pull/42",
                )
            ],
        }

        entries = release_notes.build_release_entries(commits, associated_prs)
        grouped = release_notes.group_release_entries(entries)
        markdown = release_notes.render_release_notes(grouped)

        self.assertEqual(len(entries), 4)
        self.assertEqual(grouped["Highlights"], ["Adds kitty image protocol support"])
        self.assertEqual(
            grouped["Improvements"], ["Improves preview redraw performance"]
        )
        self.assertEqual(
            grouped["Fixes"], ["Resolves crash when preview metadata is missing"]
        )
        self.assertEqual(
            grouped["Docs & Maintenance"], ["Updates release workflow notes"]
        )
        self.assertNotIn("Merge pull request", markdown)

    def test_skips_commit_fallback_for_later_commits_backed_by_same_pr(self) -> None:
        commits = [
            release_notes.CommitInfo(
                sha="1111111",
                parents=("aaaaaaa",),
                subject="feat(render): add kitty image protocol support",
            ),
            release_notes.CommitInfo(
                sha="2222222",
                parents=("bbbbbbb",),
                subject="fix(render): resolve remaining kitty edge case",
            ),
        ]
        associated_prs = {
            "1111111": [
                release_notes.PullRequestInfo(
                    number=42,
                    title="feat(render): add kitty image protocol support (#42)",
                    labels=("feature",),
                )
            ],
            "2222222": [
                release_notes.PullRequestInfo(
                    number=42,
                    title="feat(render): add kitty image protocol support (#42)",
                    labels=("feature",),
                )
            ],
        }

        entries = release_notes.build_release_entries(commits, associated_prs)

        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0].source, "pr")
        self.assertEqual(entries[0].identifier, "42")

    def test_ignores_closed_but_unmerged_pr_candidates(self) -> None:
        config = release_notes.GitHubAPIConfig(
            enabled=True,
            repository="owner/repo",
            token="token",
        )
        commits = [
            release_notes.CommitInfo(
                sha="1111111",
                parents=("aaaaaaa",),
                subject="fix: resolve crash when preview metadata is missing",
            )
        ]

        def fake_get_json(_config: object, _path: str) -> object:
            return [
                {
                    "number": 99,
                    "title": "fix: abandoned approach",
                    "state": "closed",
                    "merged_at": None,
                    "labels": [],
                },
                {
                    "number": 100,
                    "title": "fix: shipped approach",
                    "state": "closed",
                    "merged_at": "2026-03-08T00:00:00Z",
                    "labels": [{"name": "fix"}],
                },
            ]

        original = getattr(release_notes, "github_api_get_json")
        try:
            setattr(release_notes, "github_api_get_json", fake_get_json)
            associated = release_notes.fetch_associated_pull_requests(config, commits)
        finally:
            setattr(release_notes, "github_api_get_json", original)

        self.assertEqual(len(associated["1111111"]), 1)
        self.assertEqual(associated["1111111"][0].number, 100)
