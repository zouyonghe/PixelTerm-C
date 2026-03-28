import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve().parent / "sync_version_refs.py"
WORKTREE_ROOT = SCRIPT_PATH.resolve().parent.parent
MODULE_SPEC = importlib.util.spec_from_file_location("sync_version_refs", SCRIPT_PATH)
assert MODULE_SPEC is not None and MODULE_SPEC.loader is not None
sync_version_refs = importlib.util.module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(sync_version_refs)

APPROVED_TARGET_PATHS = (
    "README.md",
    "docs/i18n/README_zh.md",
    "docs/i18n/README_ja.md",
    "docs/project/PROJECT_STATUS.md",
    "docs/project/ROADMAP.md",
    "docs/development/DEVELOPMENT.md",
)


def run_script(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT_PATH), *args],
        text=True,
        capture_output=True,
    )


def target_for(relative_path: str) -> object:
    for target in sync_version_refs.TARGETS:
        if target.relative_path == relative_path:
            return target
    raise AssertionError(f"missing tracked target for {relative_path}")


def tracked_version(root: Path, relative_path: str) -> str:
    content = (root / relative_path).read_text(encoding="utf-8")
    match = sync_version_refs.expected_match(content, target_for(relative_path))
    return match.group("version")


def current_repo_version() -> str:
    versions = {
        tracked_version(WORKTREE_ROOT, relative_path)
        for relative_path in APPROVED_TARGET_PATHS
    }
    if len(versions) != 1:
        raise AssertionError(f"expected one repo version, got {sorted(versions)}")
    return next(iter(versions))


def replace_version(root: Path, relative_path: str, version: str) -> None:
    path = root / relative_path
    original = path.read_text(encoding="utf-8")
    updated = original.replace(tracked_version(root, relative_path), version, 1)
    if updated == original:
        raise AssertionError(f"expected tracked version in {path}")
    path.write_text(updated, encoding="utf-8")


def break_tracked_reference(root: Path, relative_path: str) -> None:
    replace_version(root, relative_path, current_repo_version().removeprefix("v"))


def fixture_files() -> dict[str, str]:
    files = {
        relative_path: (WORKTREE_ROOT / relative_path).read_text(encoding="utf-8")
        for relative_path in APPROVED_TARGET_PATHS
    }
    files["docs/notes.md"] = "Untracked reference: v1.7.13\n"
    return files


class SyncVersionRefsCLITest(unittest.TestCase):
    def create_fixture_root(self) -> Path:
        root = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: shutil.rmtree(root, ignore_errors=True))

        for relative_path, content in fixture_files().items():
            file_path = root / relative_path
            file_path.parent.mkdir(parents=True, exist_ok=True)
            file_path.write_text(content, encoding="utf-8")

        return root

    def test_patterns_match_real_repo_targets(self) -> None:
        expected_version = current_repo_version()

        self.assertEqual(
            tuple(target.relative_path for target in sync_version_refs.TARGETS),
            APPROVED_TARGET_PATHS,
        )

        for relative_path in APPROVED_TARGET_PATHS:
            content = (WORKTREE_ROOT / relative_path).read_text(encoding="utf-8")
            match = sync_version_refs.expected_match(content, target_for(relative_path))

            self.assertEqual(match.group("version"), expected_version, relative_path)

    def test_check_reports_outdated_tracked_files(self) -> None:
        root = self.create_fixture_root()
        expected_version = current_repo_version()
        replace_version(root, "docs/project/ROADMAP.md", "v0.0.0")

        result = run_script(expected_version, "--check", "--root", str(root))

        self.assertEqual(result.returncode, 2, msg=result.stderr)
        self.assertIn("docs/project/ROADMAP.md", result.stdout)
        self.assertIn(
            "v0.0.0", (root / "docs/project/ROADMAP.md").read_text(encoding="utf-8")
        )

    def test_check_reports_hard_failure_separately_from_safe_drift(self) -> None:
        root = self.create_fixture_root()
        break_tracked_reference(root, "docs/project/ROADMAP.md")

        result = run_script(current_repo_version(), "--check", "--root", str(root))

        self.assertEqual(result.returncode, 1, msg=result.stdout)
        self.assertIn(
            "Expected exactly one tracked version reference in docs/project/ROADMAP.md",
            result.stderr,
        )

    def test_sync_updates_only_fixed_target_files(self) -> None:
        root = self.create_fixture_root()
        expected_version = current_repo_version()
        replace_version(root, "README.md", "v0.0.0")
        replace_version(root, "docs/project/PROJECT_STATUS.md", "v0.0.0")

        result = run_script(expected_version, "--root", str(root))

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Updated 2 files", result.stdout)
        self.assertIn(
            expected_version, (root / "README.md").read_text(encoding="utf-8")
        )
        self.assertIn(
            expected_version,
            (root / "docs/project/PROJECT_STATUS.md").read_text(encoding="utf-8"),
        )
        self.assertIn("v1.7.13", (root / "docs/notes.md").read_text(encoding="utf-8"))

    def test_check_succeeds_when_tracked_files_are_already_in_sync(self) -> None:
        root = self.create_fixture_root()

        result = run_script(current_repo_version(), "--check", "--root", str(root))

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Version references already match", result.stdout)

    def test_current_version_cli_reports_tracked_docs_version(self) -> None:
        root = self.create_fixture_root()

        result = run_script("--current-version", "--root", str(root))

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(result.stdout.strip(), current_repo_version())

    def test_compare_versions_orders_release_tags_numerically(self) -> None:
        self.assertLess(
            sync_version_refs.compare_versions("v1.7.13", "v1.7.14"),
            0,
        )
        self.assertEqual(
            sync_version_refs.compare_versions("v1.7.14", "v1.7.14"),
            0,
        )
        self.assertGreater(
            sync_version_refs.compare_versions("v1.8.0", "v1.7.99"),
            0,
        )

    def test_rejects_non_simple_release_tags(self) -> None:
        for invalid_version in ("1.7.14", "v1.7", "v1.7.14-rc1", "v1.7.14+meta"):
            with self.subTest(version=invalid_version):
                result = run_script(
                    invalid_version, "--check", "--root", str(WORKTREE_ROOT)
                )

                self.assertEqual(result.returncode, 1, msg=result.stdout)
                self.assertIn(
                    "Version must use the simple release tag format vX.Y.Z",
                    result.stderr,
                )


if __name__ == "__main__":
    unittest.main()
