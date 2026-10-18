"""Calibrate retained measurement identity separately from live source identity."""

import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path

import check_dpucp_donor_ledger as donors


class MeasurementSourceIdentityTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'fixture.c'
        self.source.write_text('int fixture(void) { return 37; }\n')
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        subprocess.run(['git', 'add', 'fixture.c'], cwd=self.root, check=True)
        subprocess.run(['git', '-c', 'user.name=Fixture', '-c',
                        'user.email=fixture@example.invalid', '-c', 'commit.gpgsign=false',
                        'commit', '-qm', 'Retain measured source'], cwd=self.root, check=True)
        self.revision = subprocess.check_output(
            ['git', 'rev-parse', 'HEAD'], cwd=self.root, text=True).strip()
        self.hashes = {'fixture.c': hashlib.sha256(self.source.read_bytes()).hexdigest()}

    def test_retained_and_live_identity_match(self):
        self.assertEqual(donors.validate_measured_sources(
            self.root, self.hashes, self.revision), [])

    def test_live_change_retains_historical_identity_and_reports_drift(self):
        self.source.write_text('int fixture(void) { return 91; }\n')
        self.assertEqual(donors.validate_measured_sources(
            self.root, self.hashes, self.revision), ['fixture.c'])

    def test_changed_retained_digest_rejects_record(self):
        with self.assertRaisesRegex(donors.DonorError, 'retained source digest'):
            donors.validate_measured_sources(self.root, {'fixture.c': '0' * 64}, self.revision)

    def test_unavailable_retained_revision_rejects_record(self):
        with self.assertRaisesRegex(donors.DonorError, 'unavailable'):
            donors.validate_measured_sources(self.root, self.hashes, '1' * 40)

    def test_unsafe_source_path_rejects_record(self):
        with self.assertRaisesRegex(donors.DonorError, 'path'):
            donors.validate_measured_sources(self.root, {'../fixture.c': '0' * 64}, self.revision)

    def test_symbolic_revision_rejects_ambiguous_identity(self):
        with self.assertRaisesRegex(donors.DonorError, 'revision'):
            donors.validate_measured_sources(self.root, self.hashes, 'HEAD')

    def test_removed_live_file_keeps_historical_status(self):
        self.source.unlink()
        self.assertEqual(donors.validate_measured_sources(
            self.root, self.hashes, self.revision), ['fixture.c'])


if __name__ == '__main__':
    unittest.main()
