from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT_PATH = REPO_ROOT / "scripts" / "install.sh"
README_PATH = REPO_ROOT / "README.md"
README_ZH_PATH = REPO_ROOT / "docs" / "i18n" / "README_zh.md"
README_JA_PATH = REPO_ROOT / "docs" / "i18n" / "README_ja.md"
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

    def test_install_completes_without_unbound_tmp_dir_error(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            fake_bin_dir = temp_path / "fake-bin"
            install_dir = temp_path / "install-bin"
            fake_bin_dir.mkdir()

            fake_curl = fake_bin_dir / "curl"
            fake_curl.write_text(
                "#!/usr/bin/env bash\n"
                "set -eu\n"
                "out=''\n"
                'while [ "$#" -gt 0 ]; do\n'
                '  case "$1" in\n'
                '    -o) out="$2"; shift 2 ;;\n'
                "    *) shift ;;\n"
                "  esac\n"
                "done\n"
                "printf '#!/usr/bin/env bash\\nexit 0\\n' > \"$out\"\n"
                'chmod 0755 "$out"\n',
                encoding="utf-8",
            )
            fake_curl.chmod(0o755)

            result = run_script(
                "--bin-dir",
                str(install_dir),
                env={
                    "PATH": f"{fake_bin_dir}:{os.environ['PATH']}",
                    "PIXELTERM_INSTALL_UNAME_S": "Linux",
                    "PIXELTERM_INSTALL_UNAME_M": "x86_64",
                },
            )

            self.assertEqual(result.returncode, 0, msg=result.stderr)
            self.assertTrue((install_dir / "pixelterm").exists())


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

    def test_chinese_readme_promotes_one_command_install(self) -> None:
        self.assertIn(INSTALL_COMMAND, README_ZH_PATH.read_text(encoding="utf-8"))

    def test_japanese_readme_promotes_one_command_install(self) -> None:
        self.assertIn(INSTALL_COMMAND, README_JA_PATH.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
