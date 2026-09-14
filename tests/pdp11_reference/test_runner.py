"""Host-only tests for the PDP-11 reference runner boundaries."""

from __future__ import annotations

import hashlib
import pathlib
import tempfile
import unittest
from unittest import mock

import run


class ReferenceRunnerTests(unittest.TestCase):
    def test_checked_in_profile_matches_runner(self) -> None:
        profile_path = pathlib.Path(run.__file__).with_name("v7_rl02.simh")
        self.assertEqual(
            run.validate_profile(profile_path), run.EXPECTED_PROFILE_SHA256
        )

    def test_file_sha256(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_path = pathlib.Path(directory_name) / "fixture"
            fixture_path.write_bytes(b"abc")
            self.assertEqual(
                run.file_sha256(fixture_path),
                hashlib.sha256(b"abc").hexdigest(),
            )

    def test_normalize_transcript(self) -> None:
        raw_output = (
            b"banner\r\nPDP11_PROMPTPDP11_ORACLE_BEGIN\r\n"
            b"PDP11_PROMPTLINKS\r\n"
            b" 1949 /oracle.dir/a\r\n 1949 /oracle.dir/b\r\n"
            b"PDP11_PROMPTPDP11_ORACLE_END\r\ntrailer\r\n"
        )
        self.assertEqual(
            run.normalize_transcript(raw_output),
            "PDP11_ORACLE_BEGIN\n"
            "LINKS\n"
            "<same-inode> /oracle.dir/a\n"
            "<same-inode> /oracle.dir/b\n"
            "PDP11_ORACLE_END\n",
        )

    def test_normalize_transcript_rejects_different_inodes(self) -> None:
        raw_output = (
            b"PDP11_ORACLE_BEGIN\n1 /oracle.dir/a\n2 /oracle.dir/b\nPDP11_ORACLE_END\n"
        )
        with self.assertRaisesRegex(ValueError, "shared inode"):
            run.normalize_transcript(raw_output)

    def test_normalize_transcript_requires_markers(self) -> None:
        with self.assertRaisesRegex(ValueError, "complete oracle markers"):
            run.normalize_transcript(b"partial output\n")

    def test_validate_image_rejects_wrong_size(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_path = pathlib.Path(directory_name) / "disk"
            fixture_path.write_bytes(b"wrong")
            with self.assertRaisesRegex(ValueError, "image size"):
                run.validate_image(fixture_path)

    def test_validate_image_rejects_wrong_digest(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_path = pathlib.Path(directory_name) / "disk"
            fixture_path.write_bytes(b"wrong")
            with (
                mock.patch.object(run, "EXPECTED_IMAGE_SIZE", 5),
                self.assertRaisesRegex(ValueError, "image SHA-256"),
            ):
                run.validate_image(fixture_path)

    def test_validate_profile_rejects_unreviewed_commands(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_path = pathlib.Path(directory_name) / "profile"
            fixture_path.write_text("! host-command\n", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "profile SHA-256"):
                run.validate_profile(fixture_path)

    def test_write_evidence_refuses_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            evidence_directory = pathlib.Path(directory_name)
            (evidence_directory / "simulator-output.bin").write_bytes(b"retained")
            with self.assertRaises(FileExistsError):
                run.write_evidence(evidence_directory, {}, b"new", "new\n")
            self.assertFalse(
                (evidence_directory / "normalized-transcript.txt").exists()
            )
            self.assertFalse((evidence_directory / "provenance.json").exists())


if __name__ == "__main__":
    unittest.main()
