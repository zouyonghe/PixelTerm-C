#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping


SECTION_ORDER = (
    "Highlights",
    "Improvements",
    "Fixes",
    "Docs & Maintenance",
)
RELEASE_TAG_PATTERN = re.compile(r"^v\d+\.\d+\.\d+(?:[-+][A-Za-z0-9._-]+)?$")
CONVENTIONAL_PREFIX_PATTERN = re.compile(
    r"^(?P<type>feat|fix|docs|chore|ci|build|perf|refactor|test|tests|style)(?:\([^)]+\))?!?:\s*",
    re.IGNORECASE,
)
PR_SUFFIX_PATTERN = re.compile(r"\s*\(#\d+\)$")
MERGE_SUBJECT_PATTERN = re.compile(r"^(Merge|Merged)\b", re.IGNORECASE)
TRUTHY_VALUES = {"1", "true", "yes", "on"}
ASSOCIATED_PR_QUERY_LIMIT = 100

DOC_TYPES = {"docs", "chore", "ci", "build", "test", "tests", "style", "refactor"}
FIX_TYPES = {"fix"}
HIGHLIGHT_TYPES = {"feat"}
IMPROVEMENT_TYPES = {"perf"}

DOC_LABELS = {
    "build",
    "chore",
    "ci",
    "dependencies",
    "dependency",
    "docs",
    "documentation",
    "maintenance",
    "refactor",
    "release",
    "test",
    "tests",
}
FIX_LABELS = {"bug", "bugfix", "fix", "hotfix", "regression"}
HIGHLIGHT_LABELS = {"breaking", "feature", "highlight", "major"}
IMPROVEMENT_LABELS = {"enhancement", "improvement", "perf", "performance"}


class ReleaseNotesError(RuntimeError):
    pass


class GitHubAPIRequestError(ReleaseNotesError):
    def __init__(
        self,
        path: str,
        *,
        status_code: int | None = None,
        reason: str | None = None,
        details: str | None = None,
    ) -> None:
        self.path = path
        self.status_code = status_code
        self.reason = reason
        self.details = details

        if status_code is None:
            status_text = reason or "request failed"
        else:
            status_text = f"{status_code} {reason or 'request failed'}"
        suffix = f": {details}" if details else ""
        super().__init__(f"GitHub API request failed for {path}: {status_text}{suffix}")


@dataclass(frozen=True)
class CommitInfo:
    sha: str
    parents: tuple[str, ...]
    subject: str


@dataclass(frozen=True)
class PullRequestInfo:
    number: int
    title: str
    labels: tuple[str, ...]
    url: str | None = None


@dataclass(frozen=True)
class GitHubAPIConfig:
    enabled: bool
    repository: str | None = None
    token: str | None = None
    api_url: str = "https://api.github.com"


@dataclass(frozen=True)
class ReleaseEntry:
    category: str
    bullet: str
    source: str
    identifier: str


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate release notes markdown for a tagged release."
    )
    parser.add_argument("--current-tag", required=True, help="Tag being released.")
    parser.add_argument(
        "--previous-tag",
        help="Previous release tag. If omitted, the script will resolve it automatically.",
    )
    parser.add_argument("--output", required=True, help="Output markdown file path.")
    parser.add_argument(
        "--repo",
        default=".",
        help="Repository path to inspect. Defaults to the current directory.",
    )
    return parser


def run_git(repo_path: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=repo_path,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip() or "git command failed"
        raise ReleaseNotesError(f"git {' '.join(args)}: {message}")
    return result.stdout.strip()


def ensure_git_repo(repo_path: Path) -> None:
    result = subprocess.run(
        ["git", "rev-parse", "--is-inside-work-tree"],
        cwd=repo_path,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0 or result.stdout.strip() != "true":
        raise ReleaseNotesError(f"Not a git repository: {repo_path}")


def validate_release_tag(tag_name: str, description: str) -> None:
    if not RELEASE_TAG_PATTERN.match(tag_name):
        raise ReleaseNotesError(
            f"{description} does not match the release tag pattern: {tag_name}"
        )


def tag_exists(repo_path: Path, tag_name: str) -> bool:
    result = subprocess.run(
        ["git", "rev-parse", "-q", "--verify", f"refs/tags/{tag_name}"],
        cwd=repo_path,
        text=True,
        capture_output=True,
    )
    return result.returncode == 0


def tag_is_ancestor(repo_path: Path, ancestor_tag: str, current_tag: str) -> bool:
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", ancestor_tag, current_tag],
        cwd=repo_path,
        text=True,
        capture_output=True,
    )
    return result.returncode == 0


def previous_reachable_tag(repo_path: Path, current_tag: str) -> str | None:
    current_commit = run_git(repo_path, "rev-list", "-n", "1", current_tag)
    traversed = run_git(repo_path, "rev-list", "--first-parent", current_commit)
    commits = [line for line in traversed.splitlines() if line]

    for commit in commits[1:]:
        tags_output = run_git(repo_path, "tag", "--points-at", commit)
        for tag in tags_output.splitlines():
            if RELEASE_TAG_PATTERN.match(tag):
                return tag
    return None


def resolve_previous_tag(
    repo_path: Path, current_tag: str, previous_tag: str | None
) -> tuple[str | None, str | None]:
    if previous_tag is not None:
        if previous_tag == current_tag:
            raise ReleaseNotesError(
                f"Previous tag {previous_tag!r} must differ from current tag {current_tag!r}."
            )
        if not tag_is_ancestor(repo_path, previous_tag, current_tag):
            raise ReleaseNotesError(
                f"Previous tag {previous_tag!r} is not an ancestor of current tag {current_tag!r}."
            )
        return previous_tag, None

    resolved_tag = previous_reachable_tag(repo_path, current_tag)
    if resolved_tag is None:
        return None, "No previous tag found; using first-release fallback."

    return resolved_tag, f"Resolved previous tag automatically: {resolved_tag}"


def read_commits(
    repo_path: Path, current_tag: str, previous_tag: str | None
) -> list[CommitInfo]:
    revision = current_tag if previous_tag is None else f"{previous_tag}..{current_tag}"
    output = run_git(repo_path, "log", "--reverse", "--format=%H%x1f%P%x1f%s", revision)

    commits: list[CommitInfo] = []
    for line in output.splitlines():
        if not line:
            continue
        sha, parents, subject = line.split("\x1f", 2)
        commits.append(
            CommitInfo(
                sha=sha,
                parents=tuple(part for part in parents.split() if part),
                subject=subject,
            )
        )
    return commits


def env_value(env: Mapping[str, str], *names: str) -> str | None:
    for name in names:
        value = env.get(name)
        if value is not None and value != "":
            return value
    return None


def resolve_github_api_config(
    env: Mapping[str, str] | None = None,
) -> GitHubAPIConfig:
    if env is None:
        env = os.environ

    enabled_value = env.get("RELEASE_NOTES_USE_GITHUB_API", "")
    enabled = enabled_value.strip().lower() in TRUTHY_VALUES
    if not enabled:
        return GitHubAPIConfig(enabled=False)

    repository = env_value(
        env,
        "RELEASE_NOTES_GITHUB_REPOSITORY",
        "GITHUB_REPOSITORY",
    )
    token = env_value(
        env,
        "RELEASE_NOTES_GITHUB_TOKEN",
        "github_token",
        "GITHUB_TOKEN",
    )
    api_url = (
        env_value(env, "RELEASE_NOTES_GITHUB_API_URL", "GITHUB_API_URL")
        or "https://api.github.com"
    )

    if repository is None:
        raise ReleaseNotesError(
            "GitHub API is enabled but repository is missing; set GITHUB_REPOSITORY or RELEASE_NOTES_GITHUB_REPOSITORY."
        )
    if token is None:
        raise ReleaseNotesError(
            "GitHub API is enabled but token is missing; set github_token, GITHUB_TOKEN, or RELEASE_NOTES_GITHUB_TOKEN."
        )

    return GitHubAPIConfig(
        enabled=True,
        repository=repository,
        token=token,
        api_url=api_url.rstrip("/"),
    )


def github_api_get_json(config: GitHubAPIConfig, path: str) -> object:
    request = urllib.request.Request(
        f"{config.api_url}{path}",
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {config.token}",
            "User-Agent": "pixelterm-release-notes-generator",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            payload = response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        details = error.read().decode("utf-8", errors="replace").strip()
        raise GitHubAPIRequestError(
            path,
            status_code=error.code,
            reason=error.reason,
            details=details or None,
        ) from error
    except urllib.error.URLError as error:
        raise GitHubAPIRequestError(
            path,
            reason=str(error.reason),
        ) from error

    try:
        return json.loads(payload)
    except json.JSONDecodeError as error:
        raise ReleaseNotesError(
            f"GitHub API returned invalid JSON for {path}"
        ) from error


def fetch_associated_pull_requests(
    config: GitHubAPIConfig, commits: list[CommitInfo]
) -> dict[str, list[PullRequestInfo]]:
    if not config.enabled or not commits:
        return {}

    repository = urllib.parse.quote(config.repository or "", safe="/")
    associations: dict[str, list[PullRequestInfo]] = {}
    query_limit = max(0, ASSOCIATED_PR_QUERY_LIMIT)
    commits_to_query = commits[:query_limit]
    skipped_commits = len(commits) - len(commits_to_query)
    if skipped_commits > 0:
        print(
            "GitHub associated-PR lookup capped at "
            f"{query_limit} commits; using commit fallback for {skipped_commits} later commits.",
            file=sys.stderr,
        )

    for commit in commits_to_query:
        path = f"/repos/{repository}/commits/{urllib.parse.quote(commit.sha)}/pulls"
        try:
            payload = github_api_get_json(config, path)
        except GitHubAPIRequestError as error:
            if should_fallback_to_commit_only(error):
                print(
                    fallback_message_for_api_error(commit.sha, error),
                    file=sys.stderr,
                )
                continue
            raise
        if not isinstance(payload, list):
            raise ReleaseNotesError(
                f"GitHub API returned unexpected data for commit {commit.sha}"
            )

        seen_numbers: set[int] = set()
        pull_requests: list[PullRequestInfo] = []
        for item in payload:
            if not isinstance(item, dict):
                continue
            number = item.get("number")
            title = item.get("title")
            if not isinstance(number, int) or not isinstance(title, str):
                continue
            merged_at = item.get("merged_at")
            if merged_at is None:
                continue
            if number in seen_numbers:
                continue
            seen_numbers.add(number)
            labels = item.get("labels") or []
            label_names_list: list[str] = []
            for label in labels:
                if not isinstance(label, dict):
                    continue
                name = label.get("name")
                if isinstance(name, str):
                    label_names_list.append(name)
            pull_requests.append(
                PullRequestInfo(
                    number=number,
                    title=title,
                    labels=tuple(label_names_list),
                    url=item.get("html_url")
                    if isinstance(item.get("html_url"), str)
                    else None,
                )
            )
        if pull_requests:
            associations[commit.sha] = pull_requests

    return associations


def is_benign_missing_commit_error(error: GitHubAPIRequestError) -> bool:
    if error.status_code not in {404, 422}:
        return False
    details = (error.details or "").casefold()
    return "no commit found for sha" in details


def should_fallback_to_commit_only(error: GitHubAPIRequestError) -> bool:
    if is_benign_missing_commit_error(error):
        return True
    if error.status_code in {401, 403, 429}:
        return True
    if error.status_code is None:
        return True
    return False


def fallback_message_for_api_error(
    commit_sha: str, error: GitHubAPIRequestError
) -> str:
    if is_benign_missing_commit_error(error):
        return (
            "GitHub associated-PR lookup returned no commit metadata for "
            f"{commit_sha}; using commit fallback."
        )
    if error.status_code in {401, 403, 429}:
        return (
            "GitHub associated-PR lookup is unavailable "
            f"({error.status_code} {error.reason or 'request failed'}) for {commit_sha}; using commit fallback."
        )
    return (
        "GitHub associated-PR lookup is unavailable for "
        f"{commit_sha}; using commit fallback."
    )


def conventional_type(text: str) -> str | None:
    match = CONVENTIONAL_PREFIX_PATTERN.match(text.strip())
    if match is None:
        return None
    return match.group("type").lower()


def strip_title_noise(text: str) -> str:
    cleaned = PR_SUFFIX_PATTERN.sub("", text.strip())
    cleaned = CONVENTIONAL_PREFIX_PATTERN.sub("", cleaned)
    cleaned = re.sub(r"\s+", " ", cleaned)
    return cleaned.strip(" -:\t")


def starts_with_keyword(text: str, keywords: tuple[str, ...]) -> bool:
    return any(text.startswith(keyword) for keyword in keywords)


def classify_change(title: str, labels: tuple[str, ...]) -> str:
    normalized_labels = {label.lower() for label in labels}
    if normalized_labels & DOC_LABELS:
        return "Docs & Maintenance"
    if normalized_labels & FIX_LABELS:
        return "Fixes"
    if normalized_labels & HIGHLIGHT_LABELS:
        return "Highlights"
    if normalized_labels & IMPROVEMENT_LABELS:
        return "Improvements"

    kind = conventional_type(title)
    if kind in DOC_TYPES:
        return "Docs & Maintenance"
    if kind in FIX_TYPES:
        return "Fixes"
    if kind in HIGHLIGHT_TYPES:
        return "Highlights"
    if kind in IMPROVEMENT_TYPES:
        return "Improvements"

    normalized = strip_title_noise(title).lower()
    if starts_with_keyword(
        normalized,
        (
            "add .",
            "add dotfile",
            "docs",
            "document",
            "chore",
            "ci",
            "build",
            "test",
            "refactor",
            "release",
            "dependency",
            "dependencies",
            "maint",
            "update release",
        ),
    ):
        return "Docs & Maintenance"
    if starts_with_keyword(
        normalized,
        (
            "fix",
            "resolve",
            "prevent",
            "correct",
            "handle",
            "avoid",
        ),
    ) or any(
        keyword in normalized for keyword in ("bug", "crash", "regression", "error")
    ):
        return "Fixes"
    if starts_with_keyword(
        normalized,
        ("add", "introduce", "enable", "allow", "support", "new"),
    ):
        return "Highlights"
    if starts_with_keyword(
        normalized,
        ("improve", "enhance", "optimize", "reduce", "speed", "update"),
    ):
        return "Improvements"
    return "Improvements"


def rewrite_leading_verb(text: str) -> str:
    rewritten = re.sub(
        r"^([^\n]+?)\s+to\s+fix\s+(.+)$",
        r"\1 to improve \2",
        text,
        count=1,
        flags=re.IGNORECASE,
    )
    if rewritten != text:
        text = rewritten

    mappings = (
        (r"^(add|adds|added)\b", "Adds"),
        (r"^(allow|allows|allowed)\b", "Allows"),
        (r"^(clean up|cleans up|cleaned up)\b", "Cleans up"),
        (r"^(document|documents|documented)\b", "Documents"),
        (r"^(enable|enables|enabled)\b", "Enables"),
        (r"^(fix|fixes|fixed)\b", "Fixes"),
        (r"^(improve|improves|improved)\b", "Improves"),
        (r"^(introduce|introduces|introduced)\b", "Introduces"),
        (r"^(optimize|optimizes|optimized)\b", "Improves"),
        (r"^(prevent|prevents|prevented)\b", "Prevents"),
        (r"^(reduce|reduces|reduced)\b", "Reduces"),
        (r"^(remove|removes|removed)\b", "Removes"),
        (r"^(rename|renames|renamed)\b", "Renames"),
        (r"^(resolve|resolves|resolved)\b", "Resolves"),
        (r"^(ship|ships|shipped)\b", "Ships"),
        (r"^(simplify|simplifies|simplified)\b", "Simplifies"),
        (r"^(speed up|speeds up|sped up)\b", "Speeds up"),
        (r"^(support|supports|supported)\b", "Supports"),
        (r"^(update|updates|updated)\b", "Updates"),
    )
    for pattern, replacement in mappings:
        rewritten = re.sub(pattern, replacement, text, count=1, flags=re.IGNORECASE)
        if rewritten != text:
            return rewritten
    if not text:
        return text
    return text[0].upper() + text[1:]


def rewrite_user_facing(text: str) -> str:
    cleaned = strip_title_noise(text)
    if not cleaned:
        return ""
    cleaned = cleaned.rstrip(". ")
    cleaned = rewrite_leading_verb(cleaned)
    cleaned = re.sub(r"\s+", " ", cleaned)
    return cleaned.strip()


def is_merge_noise(commit: CommitInfo) -> bool:
    return (
        len(commit.parents) > 1
        or MERGE_SUBJECT_PATTERN.match(commit.subject) is not None
    )


def build_release_entries(
    commits: list[CommitInfo],
    associated_prs: Mapping[str, list[PullRequestInfo]],
) -> list[ReleaseEntry]:
    entries: list[ReleaseEntry] = []
    seen_pr_numbers: set[int] = set()
    seen_bullets: set[tuple[str, str]] = set()
    pr_backed_commits = {
        commit.sha for commit in commits if associated_prs.get(commit.sha)
    }

    for commit in commits:
        pull_requests = associated_prs.get(commit.sha, [])
        for pull_request in pull_requests:
            if pull_request.number in seen_pr_numbers:
                continue
            bullet = rewrite_user_facing(pull_request.title)
            if not bullet:
                seen_pr_numbers.add(pull_request.number)
                continue
            category = classify_change(pull_request.title, pull_request.labels)
            dedupe_key = (category, bullet.casefold())
            seen_pr_numbers.add(pull_request.number)
            if dedupe_key in seen_bullets:
                continue
            entries.append(
                ReleaseEntry(
                    category=category,
                    bullet=bullet,
                    source="pr",
                    identifier=str(pull_request.number),
                )
            )
            seen_bullets.add(dedupe_key)

        if commit.sha in pr_backed_commits:
            continue
        if is_merge_noise(commit):
            continue

        bullet = rewrite_user_facing(commit.subject)
        if not bullet:
            continue
        category = classify_change(commit.subject, ())
        dedupe_key = (category, bullet.casefold())
        if dedupe_key in seen_bullets:
            continue
        entries.append(
            ReleaseEntry(
                category=category,
                bullet=bullet,
                source="commit",
                identifier=commit.sha,
            )
        )
        seen_bullets.add(dedupe_key)

    return entries


def group_release_entries(entries: list[ReleaseEntry]) -> dict[str, list[str]]:
    grouped = {section: [] for section in SECTION_ORDER}
    for entry in entries:
        grouped.setdefault(entry.category, []).append(entry.bullet)
    return grouped


def render_release_notes(grouped_entries: Mapping[str, list[str]]) -> str:
    sections: list[str] = []
    for section in SECTION_ORDER:
        bullets = grouped_entries.get(section, [])
        body = "\n".join(f"- {bullet}" for bullet in bullets)
        if body:
            sections.append(f"## {section}\n{body}")
        else:
            sections.append(f"## {section}")
    return "\n\n".join(sections) + "\n"


def write_release_notes(output_path: Path, markdown: str) -> None:
    try:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(markdown, encoding="utf-8")
    except OSError as error:
        raise ReleaseNotesError(
            f"Unable to write release notes to {output_path}: {error}"
        ) from error


def count_sources(entries: list[ReleaseEntry]) -> tuple[int, int]:
    pr_count = sum(1 for entry in entries if entry.source == "pr")
    commit_count = sum(1 for entry in entries if entry.source == "commit")
    return pr_count, commit_count


def main() -> int:
    args = build_parser().parse_args()
    repo_path = Path(args.repo).resolve()
    if not repo_path.is_dir():
        print(f"Repository path does not exist: {repo_path}", file=sys.stderr)
        return 1

    try:
        ensure_git_repo(repo_path)
        validate_release_tag(args.current_tag, "Current tag")
        if args.previous_tag is not None:
            validate_release_tag(args.previous_tag, "Previous tag")
        if not tag_exists(repo_path, args.current_tag):
            raise ReleaseNotesError(f"Current tag does not exist: {args.current_tag}")
        if args.previous_tag is not None and not tag_exists(
            repo_path, args.previous_tag
        ):
            raise ReleaseNotesError(f"Previous tag does not exist: {args.previous_tag}")

        previous_tag, status_message = resolve_previous_tag(
            repo_path, args.current_tag, args.previous_tag
        )
        commits = read_commits(repo_path, args.current_tag, previous_tag)
        github_api = resolve_github_api_config()
        associated_prs = fetch_associated_pull_requests(github_api, commits)
        entries = build_release_entries(commits, associated_prs)
        grouped_entries = group_release_entries(entries)
        markdown = render_release_notes(grouped_entries)
        write_release_notes(Path(args.output), markdown)
    except ReleaseNotesError as error:
        print(str(error), file=sys.stderr)
        return 1

    if status_message is not None:
        print(status_message)

    range_label = (
        f"{previous_tag}..{args.current_tag}"
        if previous_tag is not None
        else f"first-release..{args.current_tag}"
    )
    pr_count, commit_count = count_sources(entries)
    print(f"Commit range: {range_label}")
    print(f"Read {len(commits)} commits")
    print(f"Grouped {pr_count} PRs and {commit_count} commit fallback entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
