"""Host-only tests for the PDP-11 reference runner boundaries."""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import io
import os
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

import run

IMAGE_CONTENTS = b"pinned V7 image fixture\n"
PROFILE_CONTENTS = b"attach rl0 v7.dsk\n"
NORMALIZED_TRANSCRIPT = (
    "PDP11_ORACLE_BEGIN\n"
    "LINKS\n"
    "<same-inode> /oracle.dir/a\n"
    "<same-inode> /oracle.dir/b\n"
    "PDP11_ORACLE_END\n"
)
RAW_TRANSCRIPT = (
    b"PDP11_ORACLE_BEGIN\n"
    b"LINKS\n"
    b"1949 /oracle.dir/a\n"
    b"1949 /oracle.dir/b\n"
    b"PDP11_ORACLE_END\n"
)
FAKE_SIMULATOR_SOURCE = f"""#!/bin/sh
set -eu
: "${{PDP11_FAKE_TRACE:?}}"
if [ "$#" -eq 0 ]; then
    printf 'version\n' >> "${{PDP11_FAKE_TRACE}}"
    if [ "${{PDP11_FAKE_REPLACE_PROFILE:-0}}" -eq 1 ]; then
        printf '! unreviewed profile command\n' > "${{PDP11_FAKE_ORIGINAL_PROFILE}}"
    fi
    if [ "${{PDP11_FAKE_REPLACE_SIMULATOR:-0}}" -eq 1 ]; then
        printf '#!/bin/sh\nexit 99\n' > "${{PDP11_FAKE_ORIGINAL_SIMULATOR}}"
        chmod 700 "${{PDP11_FAKE_ORIGINAL_SIMULATOR}}"
    fi
    printf '%s\n' "${{PDP11_FAKE_BANNER:-{run.EXPECTED_SIMULATOR_VERSION}}}"
    exit "${{PDP11_FAKE_VERSION_STATUS:-0}}"
fi
printf 'guest\nargc=%s\nprofile=%s\ncwd=%s\n' \
    "$#" "$1" "$PWD" >> "${{PDP11_FAKE_TRACE}}"
# The profile must sit in the working directory. Both sides are
# compared as physical paths: macOS hands the temporary directory out
# under /var, which is a symlink to /private/var, and the shell's PWD
# carries the resolved form.
if [ "$#" -ne 1 ] || [ "$(basename -- "$1")" != v7_rl02.simh ] ||
    [ "$(cd "$(dirname -- "$1")" && pwd -P)" != "$(pwd -P)" ]; then
    exit 91
fi
if ! cmp -s "$1" "${{PDP11_FAKE_EXPECTED_PROFILE}}"; then
    exit 94
fi
if ! cmp -s v7.dsk "${{PDP11_FAKE_SOURCE_IMAGE}}"; then
    exit 92
fi
printf 'image=matched\n' >> "${{PDP11_FAKE_TRACE}}"
printf 'private mutation\n' >> v7.dsk
case "${{PDP11_FAKE_MODE:-success}}" in
nonzero)
    exit 23
    ;;
timeout)
    exec sleep 30
    ;;
mutate-source-success)
    printf 'source mutation\n' >> "${{PDP11_FAKE_SOURCE_IMAGE}}"
    ;;
mutate-source-nonzero)
    printf 'source mutation\n' >> "${{PDP11_FAKE_SOURCE_IMAGE}}"
    exit 23
    ;;
mutate-source-timeout)
    printf 'source mutation\n' >> "${{PDP11_FAKE_SOURCE_IMAGE}}"
    exec sleep 30
    ;;
success)
    ;;
*)
    exit 93
    ;;
esac
printf '%s\n' \
    PDP11_ORACLE_BEGIN \
    LINKS \
    '1949 /oracle.dir/a' \
    '1949 /oracle.dir/b' \
    PDP11_ORACLE_END
"""


class ReferenceRunnerTests(unittest.TestCase):
    def create_fixture(
        self, fixture_directory: pathlib.Path
    ) -> dict[str, pathlib.Path]:
        image_path = fixture_directory / "v7.dsk.source"
        profile_path = fixture_directory / "v7.simh"
        expected_profile_path = fixture_directory / "v7.simh.reference"
        expected_path = fixture_directory / "v7.expected"
        simulator_path = fixture_directory / "fake-simh-pdp11"
        trace_path = fixture_directory / "simulator.trace"
        image_path.write_bytes(IMAGE_CONTENTS)
        profile_path.write_bytes(PROFILE_CONTENTS)
        expected_profile_path.write_bytes(PROFILE_CONTENTS)
        expected_path.write_text(NORMALIZED_TRANSCRIPT, encoding="ascii")
        simulator_path.write_text(FAKE_SIMULATOR_SOURCE, encoding="ascii")
        simulator_path.chmod(0o700)
        return {
            "image": image_path,
            "profile": profile_path,
            "profile_reference": expected_profile_path,
            "expected": expected_path,
            "simulator": simulator_path,
            "trace": trace_path,
        }

    def admitted_constants(self) -> contextlib.ExitStack:
        patch_stack = contextlib.ExitStack()
        patch_stack.enter_context(
            mock.patch.object(run, "EXPECTED_IMAGE_SIZE", len(IMAGE_CONTENTS))
        )
        patch_stack.enter_context(
            mock.patch.object(
                run,
                "EXPECTED_IMAGE_SHA256",
                hashlib.sha256(IMAGE_CONTENTS).hexdigest(),
            )
        )
        patch_stack.enter_context(
            mock.patch.object(
                run,
                "EXPECTED_PROFILE_SHA256",
                hashlib.sha256(PROFILE_CONTENTS).hexdigest(),
            )
        )
        patch_stack.enter_context(
            mock.patch.object(
                run,
                "EXPECTED_TRANSCRIPT_SHA256",
                hashlib.sha256(NORMALIZED_TRANSCRIPT.encode("ascii")).hexdigest(),
            )
        )
        return patch_stack

    def arguments_for(
        self,
        fixture_paths: dict[str, pathlib.Path],
        *,
        evidence_directory: pathlib.Path | None = None,
        timeout_seconds: int = 5,
    ) -> argparse.Namespace:
        return argparse.Namespace(
            simulator=str(fixture_paths["simulator"]),
            image=fixture_paths["image"],
            profile=fixture_paths["profile"],
            expected=fixture_paths["expected"],
            evidence_dir=evidence_directory,
            timeout=timeout_seconds,
        )

    def fake_environment(
        self,
        fixture_paths: dict[str, pathlib.Path],
        **overrides: str,
    ) -> dict[str, str]:
        environment = {
            "PDP11_FAKE_EXPECTED_PROFILE": str(
                fixture_paths["profile_reference"].resolve()
            ),
            "PDP11_FAKE_ORIGINAL_PROFILE": str(fixture_paths["profile"].resolve()),
            "PDP11_FAKE_ORIGINAL_SIMULATOR": str(fixture_paths["simulator"].resolve()),
            "PDP11_FAKE_SOURCE_IMAGE": str(fixture_paths["image"].resolve()),
            "PDP11_FAKE_TRACE": str(fixture_paths["trace"]),
        }
        environment.update(overrides)
        return environment

    def invoke_main(self, arguments: argparse.Namespace) -> tuple[int, str, str]:
        standard_output = io.StringIO()
        standard_error = io.StringIO()
        with (
            mock.patch.object(run, "parse_arguments", return_value=arguments),
            mock.patch.object(
                run, "package_identity", return_value="test-package-owner"
            ),
            contextlib.redirect_stdout(standard_output),
            contextlib.redirect_stderr(standard_error),
        ):
            return_code = run.main()
        return return_code, standard_output.getvalue(), standard_error.getvalue()

    def assert_no_staging_directory(self, evidence_directory: pathlib.Path) -> None:
        staging_pattern = f".{evidence_directory.name}.stage-*"
        self.assertEqual(list(evidence_directory.parent.glob(staging_pattern)), [])

    def test_checked_in_profile_matches_runner(self) -> None:
        profile_path = pathlib.Path(run.__file__).with_name("v7_rl02.simh")
        profile_digest, profile_contents = run.validate_profile(profile_path)
        self.assertEqual(profile_digest, run.EXPECTED_PROFILE_SHA256)
        self.assertEqual(profile_contents, profile_path.read_bytes())

    def test_checked_in_transcript_matches_runner(self) -> None:
        expected_path = pathlib.Path(run.__file__).with_name("v7_rl02.expected")
        expected_digest, expected_output = run.validate_expected(expected_path)
        self.assertEqual(expected_digest, run.EXPECTED_TRANSCRIPT_SHA256)
        self.assertEqual(expected_output, expected_path.read_text(encoding="ascii"))

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
        self.assertEqual(run.normalize_transcript(raw_output), NORMALIZED_TRANSCRIPT)

    def test_normalize_transcript_rejects_different_inodes(self) -> None:
        raw_output = (
            b"PDP11_ORACLE_BEGIN\n1 /oracle.dir/a\n2 /oracle.dir/b\nPDP11_ORACLE_END\n"
        )
        with self.assertRaisesRegex(ValueError, "shared inode"):
            run.normalize_transcript(raw_output)

    def test_normalize_transcript_rejects_ambiguous_markers(self) -> None:
        malformed_outputs = {
            "missing": b"partial output\n",
            "duplicate begin": (
                b"PDP11_ORACLE_BEGIN\nPDP11_ORACLE_BEGIN\nPDP11_ORACLE_END\n"
            ),
            "duplicate end": (
                b"PDP11_ORACLE_BEGIN\nPDP11_ORACLE_END\nPDP11_ORACLE_END\n"
            ),
            "nested": (
                b"PDP11_ORACLE_BEGIN\nPDP11_ORACLE_BEGIN\n"
                b"PDP11_ORACLE_END\nPDP11_ORACLE_END\n"
            ),
        }
        for case_name, raw_output in malformed_outputs.items():
            with (
                self.subTest(case=case_name),
                self.assertRaisesRegex(ValueError, "exactly one"),
            ):
                run.normalize_transcript(raw_output)

        reversed_output = b"PDP11_ORACLE_END\nPDP11_ORACLE_BEGIN\n"
        with self.assertRaisesRegex(ValueError, "out of order"):
            run.normalize_transcript(reversed_output)

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

    def test_validate_expected_rejects_transcript_drift(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_path = pathlib.Path(directory_name) / "expected"
            fixture_path.write_text("drift\n", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "transcript SHA-256"):
                run.validate_expected(fixture_path)

    def test_fake_simulator_covers_successful_process_boundary(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_directory = pathlib.Path(directory_name)
            fixture_paths = self.create_fixture(fixture_directory)
            evidence_directory = fixture_directory / "retained-evidence"
            arguments = self.arguments_for(
                fixture_paths, evidence_directory=evidence_directory
            )
            environment = self.fake_environment(fixture_paths)
            with (
                self.admitted_constants(),
                mock.patch.dict(os.environ, environment, clear=False),
            ):
                return_code, standard_output, standard_error = self.invoke_main(
                    arguments
                )

            self.assertEqual(return_code, 0, standard_error)
            self.assertIn("PDP-11 V7 reference behavior passed", standard_output)
            trace_lines = (
                fixture_paths["trace"].read_text(encoding="ascii").splitlines()
            )
            self.assertEqual(trace_lines[0:3], ["version", "guest", "argc=1"])
            working_directory = pathlib.Path(trace_lines[4].removeprefix("cwd="))
            self.assertEqual(
                trace_lines[3], f"profile={working_directory / 'v7_rl02.simh'}"
            )
            self.assertTrue(
                working_directory.name.startswith("discobsd-pdp11-reference-")
            )
            self.assertNotEqual(working_directory, fixture_directory)
            self.assertEqual(trace_lines[5], "image=matched")
            self.assertEqual(fixture_paths["image"].read_bytes(), IMAGE_CONTENTS)
            self.assertFalse(working_directory.exists())
            self.assertEqual(
                (evidence_directory / "simulator-output.bin").read_bytes(),
                RAW_TRANSCRIPT,
            )
            self.assertEqual(
                (evidence_directory / "normalized-transcript.txt").read_text(
                    encoding="ascii"
                ),
                NORMALIZED_TRANSCRIPT,
            )
            provenance_text = (evidence_directory / "provenance.json").read_text(
                encoding="ascii"
            )
            self.assertIn(run.EXPECTED_SIMULATOR_VERSION, provenance_text)
            self.assertIn("expected_sha256", provenance_text)
            self.assert_no_staging_directory(evidence_directory)

    def test_fake_simulator_nonzero_exit_preserves_source_image(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_paths = self.create_fixture(pathlib.Path(directory_name))
            arguments = self.arguments_for(fixture_paths)
            environment = self.fake_environment(
                fixture_paths, PDP11_FAKE_MODE="nonzero"
            )
            with (
                self.admitted_constants(),
                mock.patch.dict(os.environ, environment, clear=False),
            ):
                return_code, _, standard_error = self.invoke_main(arguments)
            self.assertEqual(return_code, 1)
            self.assertIn("simulator exited with status 23", standard_error)
            self.assertEqual(fixture_paths["image"].read_bytes(), IMAGE_CONTENTS)

    def test_fake_simulator_timeout_preserves_source_image(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_paths = self.create_fixture(pathlib.Path(directory_name))
            arguments = self.arguments_for(fixture_paths, timeout_seconds=1)
            environment = self.fake_environment(
                fixture_paths, PDP11_FAKE_MODE="timeout"
            )
            with (
                self.admitted_constants(),
                mock.patch.dict(os.environ, environment, clear=False),
            ):
                return_code, _, standard_error = self.invoke_main(arguments)
            self.assertEqual(return_code, 1)
            self.assertIn("timed out", standard_error)
            self.assertEqual(fixture_paths["image"].read_bytes(), IMAGE_CONTENTS)

    def test_profile_replacement_after_admission_uses_private_copy(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_paths = self.create_fixture(pathlib.Path(directory_name))
            arguments = self.arguments_for(fixture_paths)
            environment = self.fake_environment(
                fixture_paths, PDP11_FAKE_REPLACE_PROFILE="1"
            )
            with (
                self.admitted_constants(),
                mock.patch.dict(os.environ, environment, clear=False),
            ):
                return_code, _, standard_error = self.invoke_main(arguments)
            self.assertEqual(return_code, 0, standard_error)
            self.assertNotEqual(fixture_paths["profile"].read_bytes(), PROFILE_CONTENTS)
            trace_text = fixture_paths["trace"].read_text(encoding="ascii")
            self.assertIn("version\nguest\n", trace_text)

    def test_simulator_replacement_after_probe_uses_private_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_paths = self.create_fixture(pathlib.Path(directory_name))
            original_simulator_digest = run.file_sha256(fixture_paths["simulator"])
            arguments = self.arguments_for(fixture_paths)
            environment = self.fake_environment(
                fixture_paths, PDP11_FAKE_REPLACE_SIMULATOR="1"
            )
            with (
                self.admitted_constants(),
                mock.patch.dict(os.environ, environment, clear=False),
            ):
                return_code, standard_output, standard_error = self.invoke_main(
                    arguments
                )
            self.assertEqual(return_code, 0, standard_error)
            self.assertNotEqual(
                run.file_sha256(fixture_paths["simulator"]),
                original_simulator_digest,
            )
            self.assertIn(original_simulator_digest, standard_output)
            trace_text = fixture_paths["trace"].read_text(encoding="ascii")
            self.assertIn("version\nguest\n", trace_text)

    def test_source_image_mutation_is_rejected_on_every_exit_path(self) -> None:
        mutation_modes = (
            "mutate-source-success",
            "mutate-source-nonzero",
            "mutate-source-timeout",
        )
        for mutation_mode in mutation_modes:
            with (
                self.subTest(mode=mutation_mode),
                tempfile.TemporaryDirectory() as directory_name,
            ):
                fixture_paths = self.create_fixture(pathlib.Path(directory_name))
                timeout_seconds = 1 if mutation_mode.endswith("timeout") else 5
                arguments = self.arguments_for(
                    fixture_paths, timeout_seconds=timeout_seconds
                )
                environment = self.fake_environment(
                    fixture_paths, PDP11_FAKE_MODE=mutation_mode
                )
                with (
                    self.admitted_constants(),
                    mock.patch.dict(os.environ, environment, clear=False),
                ):
                    return_code, _, standard_error = self.invoke_main(arguments)
                self.assertEqual(return_code, 1)
                self.assertIn("supplied image changed", standard_error)

    def test_input_drift_prevents_guest_execution(self) -> None:
        mutation_cases = ("image", "profile", "expected")
        for mutation_name in mutation_cases:
            with (
                self.subTest(input=mutation_name),
                tempfile.TemporaryDirectory() as directory_name,
            ):
                fixture_paths = self.create_fixture(pathlib.Path(directory_name))
                fixture_paths[mutation_name].write_bytes(
                    fixture_paths[mutation_name].read_bytes() + b"drift"
                )
                arguments = self.arguments_for(fixture_paths)
                environment = self.fake_environment(fixture_paths)
                with (
                    self.admitted_constants(),
                    mock.patch.dict(os.environ, environment, clear=False),
                ):
                    return_code, _, _ = self.invoke_main(arguments)
                self.assertEqual(return_code, 1)
                self.assertFalse(fixture_paths["trace"].exists())

    def test_simulator_version_drift_prevents_guest_execution(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_paths = self.create_fixture(pathlib.Path(directory_name))
            arguments = self.arguments_for(fixture_paths)
            environment = self.fake_environment(
                fixture_paths,
                PDP11_FAKE_BANNER="PDP-11 simulator Open SIMH V9.9-9",
            )
            with (
                self.admitted_constants(),
                mock.patch.dict(os.environ, environment, clear=False),
            ):
                return_code, _, standard_error = self.invoke_main(arguments)
            self.assertEqual(return_code, 1)
            self.assertIn("differs from expected", standard_error)
            self.assertEqual(
                fixture_paths["trace"].read_text(encoding="ascii"), "version\n"
            )

    def test_ambiguous_simulator_banner_prevents_guest_execution(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_paths = self.create_fixture(pathlib.Path(directory_name))
            arguments = self.arguments_for(fixture_paths)
            duplicate_banner = (
                f"{run.EXPECTED_SIMULATOR_VERSION}\n{run.EXPECTED_SIMULATOR_VERSION}"
            )
            environment = self.fake_environment(
                fixture_paths,
                PDP11_FAKE_BANNER=duplicate_banner,
            )
            with (
                self.admitted_constants(),
                mock.patch.dict(os.environ, environment, clear=False),
            ):
                return_code, _, standard_error = self.invoke_main(arguments)
            self.assertEqual(return_code, 1)
            self.assertIn("exactly one banner", standard_error)
            self.assertEqual(
                fixture_paths["trace"].read_text(encoding="ascii"), "version\n"
            )

    def test_evidence_preflight_prevents_simulator_execution(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            fixture_directory = pathlib.Path(directory_name)
            fixture_paths = self.create_fixture(fixture_directory)
            evidence_directory = fixture_directory / "retained-evidence"
            evidence_directory.mkdir()
            unrelated_path = evidence_directory / "unrelated"
            unrelated_path.write_bytes(b"retained")
            arguments = self.arguments_for(
                fixture_paths, evidence_directory=evidence_directory
            )
            environment = self.fake_environment(fixture_paths)
            with (
                self.admitted_constants(),
                mock.patch.dict(os.environ, environment, clear=False),
            ):
                return_code, _, standard_error = self.invoke_main(arguments)
            self.assertEqual(return_code, 1)
            self.assertIn("destination already exists", standard_error)
            self.assertFalse(fixture_paths["trace"].exists())
            self.assertEqual(unrelated_path.read_bytes(), b"retained")

    @unittest.skipUnless(

        sys.platform == "linux", "evidence publication uses Linux renameat2"

    )

    def test_write_evidence_publishes_complete_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            evidence_directory = pathlib.Path(directory_name) / "evidence"
            run.write_evidence(
                evidence_directory,
                {"source": "test"},
                b"raw",
                "normalized\n",
            )
            self.assertEqual(
                sorted(path.name for path in evidence_directory.iterdir()),
                [
                    "normalized-transcript.txt",
                    "provenance.json",
                    "simulator-output.bin",
                ],
            )
            self.assert_no_staging_directory(evidence_directory)

    def test_write_evidence_preserves_every_existing_destination(self) -> None:
        for has_contents in (False, True):
            with (
                self.subTest(has_contents=has_contents),
                tempfile.TemporaryDirectory() as directory_name,
            ):
                evidence_directory = pathlib.Path(directory_name) / "evidence"
                evidence_directory.mkdir()
                if has_contents:
                    (evidence_directory / "unrelated").write_bytes(b"retained")
                before_entries = {
                    path.name: path.read_bytes()
                    for path in evidence_directory.iterdir()
                    if path.is_file()
                }
                with self.assertRaisesRegex(FileExistsError, "already exists"):
                    run.write_evidence(evidence_directory, {}, b"new", "new\n")
                after_entries = {
                    path.name: path.read_bytes()
                    for path in evidence_directory.iterdir()
                    if path.is_file()
                }
                self.assertEqual(after_entries, before_entries)
                self.assert_no_staging_directory(evidence_directory)

    def test_write_evidence_cleans_staging_after_each_write_failure(self) -> None:
        original_writer = run.write_exclusive_file
        for failing_write in (1, 2, 3):
            with (
                self.subTest(write=failing_write),
                tempfile.TemporaryDirectory() as directory_name,
            ):
                evidence_directory = pathlib.Path(directory_name) / "evidence"
                write_count = 0

                def injected_writer(
                    output_path: pathlib.Path,
                    contents: bytes,
                    failing_write_number: int = failing_write,
                ) -> None:
                    nonlocal write_count
                    write_count += 1
                    if write_count == failing_write_number:
                        raise OSError(f"injected write failure {failing_write_number}")
                    original_writer(output_path, contents)

                with (
                    mock.patch.object(
                        run, "write_exclusive_file", side_effect=injected_writer
                    ),
                    self.assertRaisesRegex(OSError, "injected write failure"),
                ):
                    run.write_evidence(evidence_directory, {}, b"raw", "normalized\n")
                self.assertFalse(evidence_directory.exists())
                self.assert_no_staging_directory(evidence_directory)

    def test_write_evidence_cleans_staging_after_rename_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            evidence_directory = pathlib.Path(directory_name) / "evidence"
            with (
                mock.patch.object(
                    run,
                    "publish_directory_noreplace",
                    side_effect=OSError("injected rename failure"),
                ),
                self.assertRaisesRegex(OSError, "injected rename failure"),
            ):
                run.write_evidence(evidence_directory, {}, b"raw", "normalized\n")
            self.assertFalse(evidence_directory.exists())
            self.assert_no_staging_directory(evidence_directory)

    @unittest.skipUnless(

        sys.platform == "linux", "evidence publication uses Linux renameat2"

    )

    def test_write_evidence_preserves_destination_created_during_publish(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            evidence_directory = pathlib.Path(directory_name) / "evidence"
            destination_inode = None
            original_publisher = run.publish_directory_noreplace

            def race_publisher(
                staging_directory: pathlib.Path,
                destination_directory: pathlib.Path,
            ) -> None:
                nonlocal destination_inode
                destination_directory.mkdir()
                destination_inode = destination_directory.stat().st_ino
                original_publisher(staging_directory, destination_directory)

            with (
                mock.patch.object(
                    run,
                    "publish_directory_noreplace",
                    side_effect=race_publisher,
                ),
                self.assertRaises(FileExistsError),
            ):
                run.write_evidence(evidence_directory, {}, b"raw", "normalized\n")
            self.assertTrue(evidence_directory.is_dir())
            self.assertEqual(evidence_directory.stat().st_ino, destination_inode)
            self.assertEqual(list(evidence_directory.iterdir()), [])
            self.assert_no_staging_directory(evidence_directory)


if __name__ == "__main__":
    unittest.main()
