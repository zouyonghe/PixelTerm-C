import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


MAKEFILE = Path(__file__).resolve().parent.parent / 'Makefile'
VALIDATOR = MAKEFILE.parent / 'scripts' / 'validate_release_version.sh'


class BuildVersionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pixelterm-version-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def git(self, *args):
        return subprocess.run(['git', *args], cwd=self.root, check=True,
                              capture_output=True, text=True).stdout.strip()

    def init_repo(self):
        if shutil.which('git') is None:
            self.skipTest('Git executable unavailable')
        self.git('init')
        self.git('-c', 'user.name=Test', '-c', 'user.email=test@example.com',
                 '-c', 'commit.gpgsign=false',
                 'commit', '--allow-empty', '-m', 'Initial commit')

    def version(self, *args):
        recipe = 'print-version:' + chr(10) + chr(9) + '@echo "$(VERSION)"' + chr(10)
        env = os.environ.copy()
        for key in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES'):
            env.pop(key, None)
        env['GIT_CEILING_DIRECTORIES'] = str(self.root.parent)
        result = subprocess.run(
            ['make', '--no-print-directory', '-s', '-f', str(MAKEFILE), '-f', '-',
             'print-version', 'PKG_CONFIG=true', 'MUPDF_PREFIX=', *args],
            cwd=self.root, input=recipe, env=env,
            capture_output=True, text=True, check=True)
        return result.stdout.strip()

    def test_source_archive_has_nonempty_fallback(self):
        self.assertEqual(self.version(), 'unknown')

    def test_exact_tag_is_preserved(self):
        self.init_repo()
        self.git('tag', 'v1.2.3')
        self.assertEqual(self.version(), 'v1.2.3')

    def test_untagged_repository_uses_commit_id(self):
        self.init_repo()
        self.assertEqual(self.version(), self.git('rev-parse', '--short', 'HEAD'))

    def test_explicit_release_version_works_without_git_metadata(self):
        self.assertEqual(self.version('VERSION=v1.2.4'), 'v1.2.4')

    def test_release_version_validation(self):
        marker = self.root / 'executed'
        for version in ('v1.2.3', 'v1.2.4-rc.1', 'v1.2.4+build.2'):
            with self.subTest(version=version):
                result = subprocess.run(['bash', str(VALIDATOR), version],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
        for version in ('', 'main', 'v1.2', 'v1.2.3' + chr(10),
                        f'$(shell touch {marker})',
                        f'v1.2.3;touch {marker}',
                        f'v1.2.3$(touch {marker})',
                        f'v1.2.3`touch {marker}`', 'v1.2.3"'):
            with self.subTest(version=version):
                result = subprocess.run(['bash', str(VALIDATOR), version],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(marker.exists())


if __name__ == '__main__':
    unittest.main()
