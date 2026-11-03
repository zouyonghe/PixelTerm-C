import os
import subprocess
import tempfile
import unittest
from pathlib import Path


MAKEFILE = Path(__file__).resolve().parent.parent / 'Makefile'


class BuildVersionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pixelterm-version-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def git(self, *args):
        return subprocess.run(['git', *args], cwd=self.root, check=True,
                              capture_output=True, text=True).stdout.strip()

    def init_repo(self):
        self.git('init')
        self.git('-c', 'user.name=Test', '-c', 'user.email=test@example.com',
                 'commit', '--allow-empty', '-m', 'Initial commit')

    def version(self, *args):
        recipe = 'print-version:' + chr(10) + chr(9) + '@echo "$(VERSION)"' + chr(10)
        env = os.environ.copy()
        for key in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES'):
            env.pop(key, None)
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


if __name__ == '__main__':
    unittest.main()
