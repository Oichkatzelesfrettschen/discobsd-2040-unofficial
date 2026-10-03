"""Calibrate the cache launcher's configuration boundary without inference."""

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class CacheLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.wrapper = self.directory / 'reader.sh'
        self.wrapper.write_text('#!/bin/sh\nset -eu\nprintf "%s\\n" "$PYTHON" "$@"\n')
        self.environment = dict(os.environ)
        self.environment['PYTHON'] = os.environ['PYTHON']
        self.environment['GRAFT_CACHE_READER'] = str(self.wrapper)

    def launch(self, *arguments):
        return subprocess.run(['/bin/sh', str(ROOT / 'tools/graft-cache-reader.sh'),
                               *arguments], env=self.environment, capture_output=True,
                              text=True, check=False)

    def test_delegate_preserves_interpreter_and_literal_arguments(self):
        result = self.launch('argument with spaces', '$(untrusted-command)')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(),
                         [self.environment['PYTHON'], 'argument with spaces',
                          '$(untrusted-command)'])

    def test_require_interpreter(self):
        self.environment.pop('PYTHON')
        self.assertNotEqual(self.launch().returncode, 0)

    def test_require_wrapper(self):
        self.environment.pop('GRAFT_CACHE_READER')
        self.assertNotEqual(self.launch().returncode, 0)

    def test_reject_relative_missing_directory_and_symlink(self):
        link = self.directory / 'link.sh'
        link.symlink_to(self.wrapper)
        for path in ('reader.sh', str(self.directory / 'absent.sh'),
                     str(self.directory), str(link)):
            with self.subTest(path=path):
                self.environment['GRAFT_CACHE_READER'] = path
                result = self.launch()
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, '')

    def test_project_mcp_selects_launcher_and_explicit_environment(self):
        registration = json.loads((ROOT / '.mcp.json').read_text())['mcpServers']['graft']
        self.assertEqual(registration['command'], '/bin/sh')
        self.assertEqual(registration['args'], ['tools/graft-cache-reader.sh'])
        self.assertEqual(registration['env'], {'PYTHON': '${PYTHON}',
                                               'GRAFT_CACHE_READER': '${GRAFT_CACHE_READER}'})


if __name__ == '__main__':
    unittest.main()
