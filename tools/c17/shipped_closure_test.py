"""Calibrate shipped dependency selection with real compiler and archive output."""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import shipped_closure

RECORDER = Path(__file__).with_name("record_tool.py")
CHECKER = Path(__file__).with_name("shipped_closure.py")


class ShippedClosureTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.records = self.root / "records"
        self.compiler = shutil.which("arm-none-eabi-gcc")
        self.archive_tool = shutil.which("arm-none-eabi-ar")
        self.assertIsNotNone(self.compiler)
        self.assertIsNotNone(self.archive_tool)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        (self.root / "used.h").write_text("#define VALUE 37\n")
        (self.root / "unused.h").write_text("#define VALUE 91\n")
        (self.root / "selected.c").write_text(
            '#include "used.h"\nint selected(void) { return VALUE; }\n')
        (self.root / "unused.c").write_text(
            '#include "unused.h"\nint unused(void) { return VALUE; }\n')
        (self.root / "main.c").write_text(
            'int selected(void); int main(void) { return selected(); }\n')
        subprocess.run(["git", "add", "main.c", "selected.c", "unused.c",
                        "used.h", "unused.h"], cwd=self.root, check=True)
        subprocess.run(["git", "-c", "user.name=Fixture", "-c",
                        "user.email=fixture@example.invalid", "-c", "commit.gpgsign=false",
                        "commit", "-qm", "fixture sources"], cwd=self.root, check=True)
        denominator = {"schema_version": 2, "rows": [
            {"path": name, "language_state": "unreviewed",
             "owner": "fixture", "classification": {"risk_tier": "T1"}}
            for name in ["main.c", "selected.c", "unused.c", "unused.h", "used.h"]]}
        (self.root / "denominator.json").write_text(json.dumps(denominator))
        for source in ["main", "selected", "unused"]:
            self.record(self.compiler, "-c", source + ".c", "-o", source + ".o")
        self.record(self.archive_tool, "rcs", "libfixture.a", "selected.o", "unused.o")
        self.record(self.compiler, "-nostdlib", "-Wl,-e,main", "main.o",
                    "libfixture.a", "-o", "program")

    def record(self, *arguments):
        subprocess.run([sys.executable, str(RECORDER), "--root", str(self.root),
                        "--records", str(self.records), "--", *arguments],
                       cwd=self.root, check=True, capture_output=True,
                       env=dict(os.environ,
                                _DISCOBSD_EXECUTION_OWNERSHIP_INVOCATION="fixture-capture"))

    def check(self, *extra):
        return subprocess.run([
            sys.executable, str(CHECKER), "--root", str(self.root),
            "--records", str(self.records), "--denominator", "denominator.json",
            "--artifact", "fixture:program", "--output", str(self.root / "closure.json"),
            *extra], cwd=self.root, capture_output=True, text=True, check=False)

    def test_unused_member_and_header_stay_outside_executable_closure(self):
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads((self.root / "closure.json").read_text())
        self.assertEqual(data["configurations"]["fixture"]["tracked_sources"],
                         ["main.c", "selected.c", "used.h"])
        self.assertEqual(data["language_acceptance"], "open")

    def test_native_bsd_archive_includes_members_without_index_as_source(self):
        native_ar = Path(__file__).resolve().parents[1] / "bin/rp2040/ar"
        self.assertTrue(native_ar.is_file())
        native_tools = self.root / "tools/bin/rp2040"
        native_tools.mkdir(parents=True)
        shutil.copyfile(native_ar, native_tools / "ar")
        (native_tools / "ar").chmod(0o755)
        (self.root / "__.SYMDEF").write_bytes(b"index format control")
        self.record(str(native_ar), "rc", "native.a", "__.SYMDEF", "selected.o", "unused.o")
        result = self.check("--artifact", "native:native.a")
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads((self.root / "closure.json").read_text())
        self.assertEqual(data["configurations"]["native"]["tracked_sources"],
                         ["selected.c", "unused.c", "unused.h", "used.h"])

    def test_installed_archive_includes_every_member(self):
        result = self.check("--artifact", "development:libfixture.a")
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads((self.root / "closure.json").read_text())
        self.assertEqual(data["configurations"]["development"]["tracked_sources"],
                         ["selected.c", "unused.c", "unused.h", "used.h"])

    def test_removed_generated_input_retains_provider_and_source_identity(self):
        (self.root / "provider.in").write_text("int main(void) { return 0; }\n")
        subprocess.run(["git", "add", "provider.in"], cwd=self.root, check=True)
        (self.root / "generated.c").write_bytes((self.root / "provider.in").read_bytes())
        self.record(self.compiler, "-c", "generated.c", "-o", "generated.o")
        self.record(self.compiler, "-nostdlib", "-Wl,-e,main", "generated.o", "-o", "generated")
        (self.root / "generated.c").unlink()
        providers = self.root / "providers.json"
        providers.write_text(json.dumps({"generated.c": ["provider.in"]}))
        result = self.check("--artifact", "generated:generated",
                            "--generated-inputs", str(providers))
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads((self.root / "closure.json").read_text())
        self.assertIn("provider.in", data["configurations"]["generated"]["tracked_inputs"])
        self.assertIn("generated.c", data["configurations"]["generated"]["generated_inputs"])

    def test_source_snapshot_rejects_changed_generator_inputs(self):
        source_snapshot = self.root / "source-snapshot.json"
        result = subprocess.run([sys.executable, str(CHECKER), "--root", str(self.root),
                                 "--snapshot", str(source_snapshot)], capture_output=True,
                                text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        (self.root / "unused.h").write_text("#define VALUE 92\n")
        result = self.check("--source-snapshot", str(source_snapshot))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source snapshot", result.stderr)

    def test_source_change_during_closure_rejects_snapshot_claim(self):
        source_snapshot = self.root / "snapshot.json"
        source_snapshot.write_text(json.dumps(shipped_closure.source_snapshot(self.root)))
        original_configuration = shipped_closure.CaptureIndex.configuration

        def mutate_after_resolution(index, artifacts):
            result = original_configuration(index, artifacts)
            (self.root / "unused.h").write_text("#define VALUE 92\n")
            return result

        arguments = [str(CHECKER), "--root", str(self.root), "--records", str(self.records),
                     "--denominator", "denominator.json", "--artifact", "fixture:program",
                     "--source-snapshot", str(source_snapshot),
                     "--output", str(self.root / "closure.json")]
        with mock.patch.object(sys, "argv", arguments), mock.patch.object(
                shipped_closure.CaptureIndex, "configuration", mutate_after_resolution):
            self.assertEqual(shipped_closure.main(), 1)

    def test_unrecorded_archive_replacement_rejects_source_attribution(self):
        (self.root / "replacement.c").write_text("int selected(void) { return 84; }\n")
        subprocess.run([self.compiler, "-c", "replacement.c", "-o", "selected.o"],
                       cwd=self.root, check=True)
        subprocess.run([self.archive_tool, "rcs", "libfixture.a", "selected.o"],
                       cwd=self.root, check=True)
        self.record(self.compiler, "-nostdlib", "-Wl,-e,main", "main.o",
                    "libfixture.a", "-o", "program")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("archive", result.stderr)

    def test_tracked_linker_script_is_a_source_input(self):
        (self.root / "layout.ld").write_text(
            "SECTIONS { . = 0x10000; .text : { *(.text*) } }\n")
        subprocess.run(["git", "add", "layout.ld"], cwd=self.root, check=True)
        self.record(self.compiler, "-nostdlib", "-Wl,-e,main", "-T", "layout.ld",
                    "main.o", "libfixture.a", "-o", "scripted")
        result = self.check("--artifact", "scripted:scripted")
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads((self.root / "closure.json").read_text())
        self.assertIn("layout.ld", data["configurations"]["scripted"]["tracked_inputs"])

    def test_generated_linker_script_retains_declared_providers(self):
        (self.root / "layout.in").write_text(
            "SECTIONS { . = 0x10000; .text : { *(.text*) } }\n")
        subprocess.run(["git", "add", "layout.in"], cwd=self.root, check=True)
        shutil.copyfile(self.root / "layout.in", self.root / "generated.ld")
        self.record(self.compiler, "-nostdlib", "-Wl,-e,main", "-Tgenerated.ld",
                    "main.o", "libfixture.a", "-o", "scripted")
        providers = self.root / "providers.json"
        providers.write_text(json.dumps({"generated.ld": ["layout.in"]}))
        result = self.check("--artifact", "scripted:scripted",
                            "--generated-inputs", str(providers))
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads((self.root / "closure.json").read_text())
        self.assertIn("layout.in", data["configurations"]["scripted"]["tracked_inputs"])

    def test_wrong_execution_invocation_rejects_old_capture(self):
        accepted = self.check("--expected-invocation", "fixture-capture")
        self.assertEqual(accepted.returncode, 0, accepted.stderr)
        result = self.check("--expected-invocation", "new-distribution")
        self.assertEqual(result.returncode, 1)
        self.assertIn("invocation", result.stderr)

    def test_manifest_accounts_for_installed_native_startup(self):
        destination = self.root / "distrib/obj/destdir.rp2040/usr/lib"
        destination.mkdir(parents=True)
        shutil.copyfile(self.root / "main.o", destination / "crt0.o")
        manifest = self.root / "manifest"
        manifest.write_text("file /usr/lib/crt0.o\n")
        selections = shipped_closure.manifest_artifacts(self.root, manifest)
        self.assertEqual(list(selections.values()),
                         ["distrib/obj/destdir.rp2040/usr/lib/crt0.o"])
        (destination / "crt0.o").unlink()
        with self.assertRaises(shipped_closure.ClosureError):
            shipped_closure.manifest_artifacts(self.root, manifest)

    def test_stale_header_rejects_capture(self):
        (self.root / "used.h").write_text("#define VALUE 38\n")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("changed", result.stderr)

    def test_missing_compilation_receipt_rejects_closure(self):
        for path in self.records.glob("*.json"):
            receipt = json.loads(path.read_text())
            if receipt["source"] and receipt["source"]["path"] == "selected.c":
                path.unlink()
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("producer", result.stderr)

    def test_retained_output_mutation_rejects_capture(self):
        receipt = next(json.loads(path.read_text()) for path in self.records.glob("*.json"))
        retained = self.records / receipt["outputs"][0]["retained"]
        retained.write_bytes(retained.read_bytes() + b"mutation")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("retained", result.stderr)

    def test_unknown_denominator_source_rejects_closure(self):
        ledger = json.loads((self.root / "denominator.json").read_text())
        ledger["rows"] = [row for row in ledger["rows"] if row["path"] != "used.h"]
        (self.root / "denominator.json").write_text(json.dumps(ledger))
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("denominator", result.stderr)

    def test_artifact_mutation_rejects_capture(self):
        program = self.root / "program"
        program.write_bytes(program.read_bytes() + b"mutation")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("producer", result.stderr)

    def test_open_language_rows_reject_acceptance(self):
        result = self.check("--require-language-acceptance")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("language", result.stderr)

    def test_header_without_recorded_dependency_is_rejected(self):
        for path in self.records.glob("*.json"):
            receipt = json.loads(path.read_text())
            if receipt["source"] and receipt["source"]["path"] == "selected.c":
                receipt["dependencies"] = [row for row in receipt["dependencies"]
                                           if row["path"] != "used.h"]
                path.write_text(json.dumps(receipt))
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("dependency", result.stderr)


class ImageHardlinkTest(unittest.TestCase):
    def test_image_inode_identity_owns_aliases(self):
        listing = "/bin/box - 120 bytes\n     I-node: 9\n/bin/cat - 120 bytes\n     I-node: 9\n"
        shipped_closure.verify_image_hardlinks(listing, {"/bin/cat": "/bin/box"})

    def test_equal_size_separate_inodes_reject_alias(self):
        listing = "/bin/box - 120 bytes\n     I-node: 9\n/bin/cat - 120 bytes\n     I-node: 10\n"
        with self.assertRaisesRegex(shipped_closure.ClosureError, "inode"):
            shipped_closure.verify_image_hardlinks(listing, {"/bin/cat": "/bin/box"})

    def test_missing_inode_metadata_rejects_alias(self):
        with self.assertRaisesRegex(shipped_closure.ClosureError, "inode"):
            shipped_closure.verify_image_hardlinks("/bin/box - 120 bytes\n",
                                                  {"/bin/cat": "/bin/box"})


if __name__ == "__main__":
    unittest.main()
