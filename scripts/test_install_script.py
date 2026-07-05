from __future__ import annotations

import os
import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "install.sh"
README_PATH = REPO_ROOT / "README.md"
INSTALL_COMMAND = (
    "curl -fsSL "
    "https://raw.githubusercontent.com/zouyonghe/PixelTerm-C/main/scripts/install.sh"
    " | bash"
)


def run_script(
    *args: str, env: dict[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    full_env = os.environ.copy()
    if env is not None:
        full_env.update(env)

    return subprocess.run(
        ["bash", str(SCRIPT_PATH), *args],
        text=True,
        capture_output=True,
        env=full_env,
    )


def create_fake_curl(fake_bin_dir: Path) -> None:
    fake_binary = b"#!/usr/bin/env bash\nexit 0\n"
    fake_binary_sha256 = hashlib.sha256(fake_binary).hexdigest()
    fake_curl = fake_bin_dir / "curl"
    fake_curl.write_text(
        "#!/usr/bin/env bash\n"
        "set -eu\n"
        "out=''\n"
        "url=''\n"
        'while [ "$#" -gt 0 ]; do\n'
        '  case "$1" in\n'
        '    -o) out="$2"; shift 2 ;;\n'
        '    -*) shift ;;\n'
        '    *) url="$1"; shift ;;\n'
        "  esac\n"
        "done\n"
        "if printf '%s' \"$url\" | grep -q 'SHA256SUMS$'; then\n"
        "  if [ \"${CURL_FAIL_FOR_CHECKSUMS:-0}\" = \"1\" ]; then\n"
        "    printf 'simulated curl failure for checksums\\n' >&2\n"
        "    exit 22\n"
        "  fi\n"
        "  if [ \"${CURL_EMPTY_SHA256SUMS:-0}\" = \"1\" ]; then\n"
        "    : > \"$out\"\n"
        "    exit 0\n"
        "  fi\n"
        "  if [ \"${CURL_BAD_SHA256SUMS:-0}\" = \"1\" ]; then\n"
        "    printf '%s  %s\\n' "
        "'0000000000000000000000000000000000000000000000000000000000000000' "
        "pixelterm-amd64-linux > \"$out\"\n"
        "    exit 0\n"
        "  fi\n"
        f"  printf '%s  %s\\n' '{fake_binary_sha256}' pixelterm-amd64-linux > \"$out\"\n"
        "else\n"
        "  if [ \"${CURL_FAIL_FOR_BINARY:-0}\" = \"1\" ]; then\n"
        "    printf 'simulated curl failure for binary\\n' >&2\n"
        "    exit 22\n"
        "  fi\n"
        "  printf '#!/usr/bin/env bash\\nexit 0\\n' > \"$out\"\n"
        "  chmod 0755 \"$out\"\n"
        "fi\n",
        encoding="utf-8",
    )
    fake_curl.chmod(0o755)


def fake_install_env(fake_bin_dir: Path, extra: dict[str, str] | None = None) -> dict[str, str]:
    env = {
        "PATH": f"{fake_bin_dir}:{os.environ['PATH']}",
        "PIXELTERM_INSTALL_UNAME_S": "Linux",
        "PIXELTERM_INSTALL_UNAME_M": "x86_64",
    }
    if extra:
        env.update(extra)
    return env


class InstallScriptCLITest(unittest.TestCase):
    def test_selects_linux_amd64_release_asset(self) -> None:
        result = run_script(
            "--print-asset-name",
            env={
                "PIXELTERM_INSTALL_UNAME_S": "Linux",
                "PIXELTERM_INSTALL_UNAME_M": "x86_64",
            },
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(result.stdout.strip(), "pixelterm-amd64-linux")

    def test_selects_linux_arm64_release_asset(self) -> None:
        result = run_script(
            "--print-asset-name",
            env={
                "PIXELTERM_INSTALL_UNAME_S": "Linux",
                "PIXELTERM_INSTALL_UNAME_M": "aarch64",
            },
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(result.stdout.strip(), "pixelterm-arm64-linux")

    def test_selects_macos_amd64_release_asset(self) -> None:
        result = run_script(
            "--print-asset-name",
            env={
                "PIXELTERM_INSTALL_UNAME_S": "Darwin",
                "PIXELTERM_INSTALL_UNAME_M": "x86_64",
            },
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(result.stdout.strip(), "pixelterm-amd64-macos")

    def test_selects_macos_arm64_release_asset(self) -> None:
        result = run_script(
            "--print-asset-name",
            env={
                "PIXELTERM_INSTALL_UNAME_S": "Darwin",
                "PIXELTERM_INSTALL_UNAME_M": "arm64",
            },
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(result.stdout.strip(), "pixelterm-arm64-macos")

    def test_rejects_unsupported_platforms(self) -> None:
        result = run_script(
            "--print-asset-name",
            env={
                "PIXELTERM_INSTALL_UNAME_S": "Linux",
                "PIXELTERM_INSTALL_UNAME_M": "s390x",
            },
        )

        self.assertEqual(result.returncode, 1, msg=result.stdout)
        self.assertIn("Unsupported architecture", result.stderr)

    def test_reports_default_install_path(self) -> None:
        result = run_script("--print-install-path")

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertEqual(result.stdout.strip(), "/usr/local/bin/pixelterm")

    def test_unknown_argument_points_to_help(self) -> None:
        result = run_script("--prefix", "/tmp/pixelterm")

        self.assertNotEqual(result.returncode, 0, msg=result.stdout)
        self.assertIn("Unknown argument: --prefix", result.stderr)
        self.assertIn("Run: bash install.sh --help", result.stderr)
        self.assertIn("Use --bin-dir to choose the install directory", result.stderr)

    def test_dry_run_reports_download_url_and_destination(self) -> None:
        result = run_script(
            "--dry-run",
            env={
                "PIXELTERM_INSTALL_UNAME_S": "Linux",
                "PIXELTERM_INSTALL_UNAME_M": "x86_64",
            },
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn(
            "https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/"
            "pixelterm-amd64-linux",
            result.stdout,
        )
        self.assertIn("/usr/local/bin/pixelterm", result.stdout)

    def test_dry_run_supports_pinned_release_version(self) -> None:
        result = run_script(
            "--dry-run",
            "--version",
            "v1.7.26",
            env={
                "PIXELTERM_INSTALL_UNAME_S": "Linux",
                "PIXELTERM_INSTALL_UNAME_M": "x86_64",
            },
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Release version: v1.7.26", result.stdout)
        self.assertIn(
            "https://github.com/zouyonghe/PixelTerm-C/releases/download/"
            "v1.7.26/pixelterm-amd64-linux",
            result.stdout,
        )

    def test_dry_run_treats_latest_version_as_latest_selector(self) -> None:
        result = run_script(
            "--dry-run",
            "--version",
            "latest",
            env={
                "PIXELTERM_INSTALL_UNAME_S": "Linux",
                "PIXELTERM_INSTALL_UNAME_M": "x86_64",
            },
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("Release version: latest", result.stdout)
        self.assertIn(
            "https://github.com/zouyonghe/PixelTerm-C/releases/latest/download/"
            "pixelterm-amd64-linux",
            result.stdout,
        )
        self.assertNotIn("/releases/download/latest/", result.stdout)

    def test_install_completes_without_unbound_tmp_dir_error(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            fake_bin_dir = temp_path / "fake-bin"
            install_dir = temp_path / "install-bin"
            fake_bin_dir.mkdir()
            create_fake_curl(fake_bin_dir)

            result = run_script(
                "--bin-dir",
                str(install_dir),
                "--version",
                "v1.7.26",
                env=fake_install_env(fake_bin_dir),
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertTrue((install_dir / "pixelterm").exists())

    def test_install_aborts_on_mismatched_sha256(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            fake_bin_dir = temp_path / "fake-bin"
            install_dir = temp_path / "install-bin"
            fake_bin_dir.mkdir()
            create_fake_curl(fake_bin_dir)

            result = run_script(
                "--bin-dir",
                str(install_dir),
                "--version",
                "v1.7.26",
                env=fake_install_env(fake_bin_dir, {"CURL_BAD_SHA256SUMS": "1"}),
            )

            self.assertNotEqual(result.returncode, 0, msg=result.stdout)
            self.assertIn("Checksum verification failed", result.stderr)
            self.assertFalse((install_dir / "pixelterm").exists())

    def test_install_aborts_on_empty_sha256sums(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            fake_bin_dir = temp_path / "fake-bin"
            install_dir = temp_path / "install-bin"
            fake_bin_dir.mkdir()
            create_fake_curl(fake_bin_dir)

            result = run_script(
                "--bin-dir",
                str(install_dir),
                "--version",
                "v1.7.26",
                env=fake_install_env(fake_bin_dir, {"CURL_EMPTY_SHA256SUMS": "1"}),
            )

            self.assertNotEqual(result.returncode, 0, msg=result.stdout)
            self.assertIn("Missing checksum", result.stderr)
            self.assertFalse((install_dir / "pixelterm").exists())

    def test_install_aborts_when_binary_download_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            fake_bin_dir = temp_path / "fake-bin"
            install_dir = temp_path / "install-bin"
            fake_bin_dir.mkdir()
            create_fake_curl(fake_bin_dir)

            result = run_script(
                "--bin-dir",
                str(install_dir),
                "--version",
                "v1.7.26",
                env=fake_install_env(fake_bin_dir, {"CURL_FAIL_FOR_BINARY": "1"}),
            )

            self.assertNotEqual(result.returncode, 0, msg=result.stdout)
            self.assertIn("simulated curl failure for binary", result.stderr)
            self.assertFalse((install_dir / "pixelterm").exists())

    def test_install_aborts_when_checksum_download_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            fake_bin_dir = temp_path / "fake-bin"
            install_dir = temp_path / "install-bin"
            fake_bin_dir.mkdir()
            create_fake_curl(fake_bin_dir)

            result = run_script(
                "--bin-dir",
                str(install_dir),
                "--version",
                "v1.7.26",
                env=fake_install_env(fake_bin_dir, {"CURL_FAIL_FOR_CHECKSUMS": "1"}),
            )

            self.assertNotEqual(result.returncode, 0, msg=result.stdout)
            self.assertIn("simulated curl failure for checksums", result.stderr)
            self.assertFalse((install_dir / "pixelterm").exists())


class InstallReadmeTest(unittest.TestCase):
    def test_make_test_runs_install_script_checks(self) -> None:
        result = subprocess.run(
            ["make", "-n", "test"],
            text=True,
            capture_output=True,
            cwd=REPO_ROOT,
            check=False,
        )

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("python3 scripts/test_install_script.py", result.stdout)

    def test_readme_promotes_one_command_install(self) -> None:
        self.assertIn(INSTALL_COMMAND, README_PATH.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
