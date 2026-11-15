"""Calibrate PIC replay with real ARM captures and rejected ownership changes."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import libc_pic_replay as replay


class ReplayTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="libc-pic-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.owner = self.root / "lib/libc/gen"
        self.owner.mkdir(parents=True)
        self.source = self.owner / "probe.c"
        self.source.write_text("int state; int api(int value) { state += value; return state; }\n")
        (self.owner / "assembly.h").write_text(
            ".syntax unified\n.thumb\n.text\n.globl api\n.thumb_func\napi: bx lr\n")
        compiler = shutil.which(os.environ.get("LIBC_PIC_CC", "arm-none-eabi-gcc"))
        self.ar = shutil.which(os.environ.get("LIBC_PIC_AR", "arm-none-eabi-ar"))
        self.readelf = shutil.which(os.environ.get("LIBC_PIC_READELF", "arm-none-eabi-readelf"))
        replay.require(compiler and self.ar and self.readelf, "ARM GCC/binutils are required")
        self.compiler = Path(compiler)
        self.run_command(["git", "init", "-q"])
        self.run_command(["git", "add", "lib/libc/gen/probe.c", "lib/libc/gen/assembly.h"])
        self.run_command(["git", "-c", "user.name=fixture", "-c",
                          "user.email=fixture@example.invalid", "commit", "-qm", "fixture"])
        self.evidence = self.root / ".build-logs"
        self.evidence.mkdir()
        self.snapshot = self.evidence / "source.json"
        self.records = self.evidence / "records"
        self.output = self.evidence / "pic"
        from shipped_closure import source_snapshot

        self.snapshot.write_text(json.dumps(source_snapshot(self.root)) + "\n")
        recorder = Path(replay.__file__).parent / "c17/record_tool.py"
        argv = [sys.executable, str(recorder), "--root", str(self.root),
                "--records", str(self.records), "--", str(self.compiler),
                "-std=gnu17", "-fno-common", "-ffunction-sections", "-mcpu=cortex-m0plus",
                "-mthumb", "-Os", "-c", "probe.c", "-o", "probe.o"]
        result = subprocess.run(argv, cwd=self.owner, capture_output=True, text=True,
                                env=dict(os.environ, C17_SOURCE_SNAPSHOT=str(self.snapshot)))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.archive = self.root / "lib/libc.a"
        self.run_command([self.ar, "cr", str(self.archive), str(self.owner / "probe.o")])
        self.capture_path, = self.records.glob("*.json")

    def run_command(self, argv):
        return subprocess.check_output(argv, cwd=self.root, stderr=subprocess.STDOUT)

    def prepare(self):
        return replay.prepare(self.root, self.records, self.snapshot, self.archive,
                              self.compiler, self.ar)

    def mutate(self, change):
        row = json.loads(self.capture_path.read_text())
        change(row)
        self.capture_path.write_text(json.dumps(row) + "\n")

    def generated_capture(self):
        self.records = self.evidence / "generated-records"
        recorder = Path(replay.__file__).parent / "c17/record_tool.py"
        argv = [sys.executable, str(recorder), "--root", str(self.root),
                "--records", str(self.records), "--", str(self.compiler),
                "-mcpu=cortex-m0plus", "-mthumb", "-x", "assembler-with-cpp",
                "-c", "-", "-c", "-o", "generated.o"]
        result = subprocess.run(argv, cwd=self.owner, input='#include "assembly.h"\n',
                                capture_output=True, text=True,
                                env=dict(os.environ, C17_SOURCE_SNAPSHOT=str(self.snapshot)))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.archive = self.root / "lib/generated.a"
        self.run_command([self.ar, "cr", str(self.archive), str(self.owner / "generated.o")])
        self.capture_path, = self.records.glob("*.json")

    def test_generated_assembly_preserves_stdin_and_duplicate_compile_option(self):
        self.generated_capture()
        result = replay.replay(self.root, self.records, self.snapshot, self.archive,
                               self.output, self.compiler, self.ar)
        self.assertEqual(result["members"][0]["member"], "generated.o")

    def test_changed_generated_input_rejects(self):
        self.generated_capture()
        row = json.loads(self.capture_path.read_text())
        (self.records / row["stdin_retained"]).write_text("invalid assembly\n")
        with self.assertRaisesRegex(ValueError, "retained assembly input differs"):
            self.prepare()

    def test_real_pic_uses_got_and_preserves_static_archive(self):
        original = self.archive.read_bytes()
        result = replay.replay(self.root, self.records, self.snapshot, self.archive,
                               self.output, self.compiler, self.ar)
        self.assertEqual(len(result["members"]), 1)
        self.assertEqual(self.archive.read_bytes(), original)
        relocations = subprocess.check_output([self.readelf, "-r",
                                               str(self.output / "probe.o")], text=True)
        self.assertRegex(relocations, r"R_ARM_GOT(?:32|_BREL)")
        self.assertNotEqual(result["members"][0]["static_sha256"],
                            result["members"][0]["pic_sha256"])

    def test_missing_capture_rejects(self):
        self.capture_path.rename(self.capture_path.with_suffix(".withheld"))
        with self.assertRaisesRegex(ValueError, "missing compilation witness"):
            self.prepare()

    def test_source_drift_rejects(self):
        self.source.write_text("int changed;\n")
        with self.assertRaisesRegex(ValueError, "source snapshot changed"):
            self.prepare()

    def test_failed_compilation_capture_rejects(self):
        self.mutate(lambda row: row.update(exit_code=1))
        with self.assertRaisesRegex(ValueError, "unsuccessful compilation"):
            self.prepare()

    def test_snapshot_identity_rejects(self):
        self.mutate(lambda row: row.update(source_snapshot_sha256="0" * 64))
        with self.assertRaisesRegex(ValueError, "snapshot receipt differs"):
            self.prepare()

    def test_unknown_capture_schema_rejects(self):
        self.mutate(lambda row: row.update(schema_version=2))
        with self.assertRaisesRegex(ValueError, "unsupported compilation capture version"):
            self.prepare()

    def test_compiler_identity_rejects(self):
        self.mutate(lambda row: row["tool"].update(sha256="0" * 64))
        with self.assertRaisesRegex(ValueError, "compiler identity differs"):
            self.prepare()

    def test_dependency_identity_rejects(self):
        self.mutate(lambda row: row["dependencies"][0].update(sha256="0" * 64))
        with self.assertRaisesRegex(ValueError, "compilation input differs"):
            self.prepare()

    def test_missing_dependencies_rejects(self):
        self.mutate(lambda row: row.update(dependencies=[]))
        with self.assertRaisesRegex(ValueError, "no dependency witnesses"):
            self.prepare()

    def test_dependency_escape_rejects(self):
        self.mutate(lambda row: row["dependencies"][0].update(path="../outside.c"))
        with self.assertRaisesRegex(ValueError, "nonrelative capture path"):
            self.prepare()

    def test_unreviewed_option_rejects(self):
        self.mutate(lambda row: row["argv"].insert(1, "-fplugin=outside.so"))
        with self.assertRaisesRegex(ValueError, "unreviewed compiler option"):
            self.prepare()

    def test_response_operand_rejects_before_execution(self):
        self.mutate(lambda row: row["argv"].insert(1, "@outside.rsp"))
        with self.assertRaisesRegex(ValueError, "unreviewed compiler operand"):
            self.prepare()

    def test_source_operand_escape_rejects_before_execution(self):
        self.mutate(lambda row: row["argv"].__setitem__(row["argv"].index("probe.c"),
                                                       "../../../outside.c"))
        with self.assertRaisesRegex(ValueError, "source operand"):
            self.prepare()

    def test_compile_owner_rejects(self):
        self.mutate(lambda row: row.update(directory="lib"))
        with self.assertRaisesRegex(ValueError, "compile owner is outside libc"):
            self.prepare()

    def test_existing_output_rejects(self):
        self.output.mkdir()
        with self.assertRaisesRegex(ValueError, "output already exists"):
            replay.replay(self.root, self.records, self.snapshot, self.archive,
                          self.output, self.compiler, self.ar)

    def test_output_escape_rejects(self):
        with self.assertRaisesRegex(ValueError, "below the evidence directory"):
            replay.replay(self.root, self.records, self.snapshot, self.archive,
                          self.root / "lib/pic", self.compiler, self.ar)

    def test_ambiguous_witness_rejects(self):
        row = json.loads(self.capture_path.read_text())
        row["argv"].insert(1, "-Wall")
        (self.records / "duplicate.json").write_text(json.dumps(row) + "\n")
        with self.assertRaisesRegex(ValueError, "ambiguous compilation witnesses"):
            self.prepare()

    def replay_with_mutation(self, mutate):
        original = subprocess.run

        def observed(argv, *args, **kwargs):
            result = original(argv, *args, **kwargs)
            if replay.PIC_FLAGS[0] in argv:
                mutate()
            return result

        with patch.object(replay.subprocess, "run", side_effect=observed):
            return replay.replay(self.root, self.records, self.snapshot, self.archive,
                                 self.output, self.compiler, self.ar)

    def test_capture_change_during_replay_rejects(self):
        with self.assertRaisesRegex(ValueError, "compilation receipt changes"):
            self.replay_with_mutation(lambda: self.mutate(lambda row: row.update(note="drift")))

    def test_snapshot_change_during_replay_rejects(self):
        def mutate():
            snapshot = json.loads(self.snapshot.read_text())
            snapshot["created_epoch_seconds"] += 1
            self.snapshot.write_text(json.dumps(snapshot) + "\n")

        with self.assertRaisesRegex(ValueError, "snapshot receipt changes"):
            self.replay_with_mutation(mutate)

    def test_generated_input_change_during_replay_rejects(self):
        self.generated_capture()
        row = json.loads(self.capture_path.read_text())
        with self.assertRaisesRegex(ValueError, "retained assembly input changes"):
            self.replay_with_mutation(lambda: (self.records / row["stdin_retained"]).write_text(
                "changed retained input\n"))

    def test_static_archive_change_during_replay_rejects(self):
        with self.assertRaisesRegex(ValueError, "static archive changes"):
            self.replay_with_mutation(lambda: self.archive.write_bytes(b"withheld archive"))

    def test_failed_compiler_does_not_publish_receipt(self):
        original = subprocess.run

        def observed(argv, *args, **kwargs):
            if replay.PIC_FLAGS[0] in argv:
                return subprocess.CompletedProcess(argv, 1, b"", b"compile control")
            return original(argv, *args, **kwargs)

        with patch.object(replay.subprocess, "run", side_effect=observed):
            with self.assertRaisesRegex(ValueError, "PIC compilation fails"):
                replay.replay(self.root, self.records, self.snapshot, self.archive,
                              self.output, self.compiler, self.ar)
        self.assertFalse((self.output / "receipt.json").exists())


if __name__ == "__main__":
    unittest.main()
