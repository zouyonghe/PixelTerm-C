#!/usr/bin/env python3

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import TextIO


EXIT_SUCCESS = 0
EXIT_FAILURE = 1
EXIT_OUT_OF_SYNC = 2
RELEASE_VERSION_PATTERN = r"v\d+\.\d+\.\d+"
RELEASE_TAG_PATTERN = re.compile(rf"^{RELEASE_VERSION_PATTERN}$")


@dataclass(frozen=True)
class VersionTarget:
    relative_path: str
    pattern: re.Pattern[str]


@dataclass(frozen=True)
class FileUpdate:
    relative_path: str
    current_version: str
    updated_content: str


TARGETS = (
    VersionTarget(
        "README.md",
        re.compile(
            rf"^(?P<prefix>!\[Version\]\(https://img\.shields\.io/badge/Version-)(?P<version>{RELEASE_VERSION_PATTERN})(?P<suffix>-blue\))$",
            re.MULTILINE,
        ),
    ),
    VersionTarget(
        "docs/i18n/README_zh.md",
        re.compile(
            rf"^(?P<prefix>!\[Version\]\(https://img\.shields\.io/badge/Version-)(?P<version>{RELEASE_VERSION_PATTERN})(?P<suffix>-blue\))$",
            re.MULTILINE,
        ),
    ),
    VersionTarget(
        "docs/i18n/README_ja.md",
        re.compile(
            rf"^(?P<prefix>!\[Version\]\(https://img\.shields\.io/badge/Version-)(?P<version>{RELEASE_VERSION_PATTERN})(?P<suffix>-blue\))$",
            re.MULTILINE,
        ),
    ),
    VersionTarget(
        "docs/project/PROJECT_STATUS.md",
        re.compile(
            rf"^(?P<prefix>- \*\*Current Version\*\*: )(?P<version>{RELEASE_VERSION_PATTERN})$",
            re.MULTILINE,
        ),
    ),
    VersionTarget(
        "docs/project/ROADMAP.md",
        re.compile(
            rf"^(?P<prefix>PixelTerm-C is production-ready as of )(?P<version>{RELEASE_VERSION_PATTERN})(?P<suffix>\. The current short-term baseline)$",
            re.MULTILINE,
        ),
    ),
    VersionTarget(
        "docs/development/DEVELOPMENT.md",
        re.compile(
            rf"^(?P<prefix>\*\*Current Status\*\*: ✅ \*\*PRODUCTION READY\*\* - )(?P<version>{RELEASE_VERSION_PATTERN})(?P<suffix> with .*)$",
            re.MULTILINE,
        ),
    ),
)


class SyncVersionRefsError(RuntimeError):
    pass


def default_root() -> Path:
    return Path(__file__).resolve().parent.parent


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Sync tracked version-display references."
    )
    parser.add_argument(
        "version",
        nargs="?",
        help="Release tag-style version, for example v1.7.14.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Report whether tracked files need updates without writing changes.",
    )
    parser.add_argument(
        "--compare-version",
        help="Compare version against another release tag and print -1, 0, or 1.",
    )
    parser.add_argument(
        "--current-version",
        action="store_true",
        help="Print the currently tracked docs version and exit.",
    )
    parser.add_argument(
        "--root",
        default=str(default_root()),
        help="Repository root containing the tracked files.",
    )
    return parser


def validate_version(version: str) -> None:
    if not RELEASE_TAG_PATTERN.fullmatch(version):
        raise SyncVersionRefsError(
            f"Version must use the simple release tag format vX.Y.Z: {version}"
        )


def version_key(version: str) -> tuple[int, int, int]:
    validate_version(version)
    major, minor, patch = version.removeprefix("v").split(".")
    return int(major), int(minor), int(patch)


def compare_versions(left: str, right: str) -> int:
    left_key = version_key(left)
    right_key = version_key(right)
    if left_key < right_key:
        return -1
    if left_key > right_key:
        return 1
    return 0


def read_text(path: Path, relative_path: str) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as error:
        raise SyncVersionRefsError(
            f"Unable to read {relative_path}: {error}"
        ) from error


def write_text(path: Path, relative_path: str, content: str) -> None:
    try:
        path.write_text(content, encoding="utf-8")
    except OSError as error:
        raise SyncVersionRefsError(
            f"Unable to write {relative_path}: {error}"
        ) from error


def expected_match(content: str, target: VersionTarget) -> re.Match[str]:
    matches = list(target.pattern.finditer(content))
    if len(matches) != 1:
        raise SyncVersionRefsError(
            f"Expected exactly one tracked version reference in {target.relative_path}"
        )
    return matches[0]


def current_tracked_version(root: Path) -> str:
    versions = []
    for target in TARGETS:
        content = read_text(root / target.relative_path, target.relative_path)
        match = expected_match(content, target)
        versions.append(match.group("version"))

    distinct_versions = sorted(set(versions))
    if len(distinct_versions) != 1:
        raise SyncVersionRefsError(
            "Tracked version references do not agree: " + ", ".join(distinct_versions)
        )

    return distinct_versions[0]


def build_updates(root: Path, version: str) -> list[FileUpdate]:
    updates: list[FileUpdate] = []
    for target in TARGETS:
        path = root / target.relative_path
        content = read_text(path, target.relative_path)
        match = expected_match(content, target)
        current_version = match.group("version")
        if current_version == version:
            continue
        updated_content = (
            content[: match.start("version")]
            + version
            + content[match.end("version") :]
        )
        updates.append(
            FileUpdate(
                relative_path=target.relative_path,
                current_version=current_version,
                updated_content=updated_content,
            )
        )
    return updates


def apply_updates(root: Path, updates: list[FileUpdate]) -> None:
    for update in updates:
        write_text(
            root / update.relative_path, update.relative_path, update.updated_content
        )


def report_updates(version: str, updates: list[FileUpdate], *, stream: TextIO) -> None:
    print(
        f"Tracked version references need updates for {version}:",
        file=stream,
    )
    for update in updates:
        print(
            f"- {update.relative_path}: {update.current_version} -> {version}",
            file=stream,
        )


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    root = Path(args.root).resolve()

    if not root.is_dir():
        print(f"Repository root does not exist: {root}", file=sys.stderr)
        return EXIT_FAILURE

    try:
        if args.current_version:
            if (
                args.version is not None
                or args.check
                or args.compare_version is not None
            ):
                raise SyncVersionRefsError(
                    "--current-version cannot be combined with version sync options"
                )
            print(current_tracked_version(root))
            return EXIT_SUCCESS

        if args.version is None:
            raise SyncVersionRefsError(
                "Version is required unless --current-version is used"
            )

        validate_version(args.version)
        if args.compare_version is not None:
            print(compare_versions(args.version, args.compare_version))
            return EXIT_SUCCESS

        updates = build_updates(root, args.version)
        if args.check:
            if updates:
                report_updates(args.version, updates, stream=sys.stdout)
                return EXIT_OUT_OF_SYNC
            print("Version references already match.")
            return EXIT_SUCCESS

        if not updates:
            print("Version references already match.")
            return EXIT_SUCCESS

        apply_updates(root, updates)
    except SyncVersionRefsError as error:
        print(str(error), file=sys.stderr)
        return EXIT_FAILURE

    print(f"Updated {len(updates)} files to {args.version}.")
    for update in updates:
        print(f"- {update.relative_path}: {update.current_version} -> {args.version}")
    return EXIT_SUCCESS


if __name__ == "__main__":
    raise SystemExit(main())
