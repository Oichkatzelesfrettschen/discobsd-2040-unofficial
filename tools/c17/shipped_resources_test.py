"""Calibrate per-surface resource accounting and reject unsupported acceptance."""

import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import shipped_resources

CHECKER = Path(__file__).with_name("shipped_resources.py")


def snapshot():
    kernel = {"sha256": "a" * 64, "sections": [
        {"name": ".boot2", "size": 256, "vma": 0x10000000,
         "lma": 0x10000000, "contents": True},
        {"name": ".text", "size": 1024, "vma": 0x10000100,
         "lma": 0x10000100, "contents": True},
        {"name": ".data", "size": 32, "vma": 0x20024000,
         "lma": 0x10000500, "contents": True},
        {"name": ".bss", "size": 64, "vma": 0x20024020,
         "lma": 0x20024020, "contents": False}]}
    return {"schema_version": 1, "source": {"commit": "b" * 40},
            "toolchain": {"cc": "fixture compiler", "ld": "fixture linker",
                          "bmake": "fixture make"},
            "measurement_tools": {"hsaout": "2" * 64, "fsutil": "3" * 64,
                                  "objdump": "4" * 64},
            "manifest": {"sha256": "c" * 64, "packs": ["/bin/fixture"],
                         "native_files": ["/usr/lib/crt0.o"],
                         "links": {"/bin/alias": "/bin/fixture"}},
            "execution": {"state": "completed", "exit_code": 0,
                          "receipt_sha256": "d" * 64, "log_sha256": "e" * 64},
            "kernels": {"PICO": copy.deepcopy(kernel), "PICO_UART": copy.deepcopy(kernel)},
            "programs": {"/bin/fixture": {"sha256": "f" * 64, "text": 1024,
                "data": 32, "bss": 64, "raw_bytes": 1088,
                "packed_bytes": 900, "packed_blocks": 1,
                "libc_members": ["write.o"]}},
            "native_artifacts": {"/usr/lib/crt0.o": {"sha256": "1" * 64, "bytes": 64,
                                                       "archive_members": []}},
            "root": {"sha256": "0" * 64, "partition_blocks": 988,
                     "inode_list_blocks": 9, "data_blocks": 695,
                     "free_blocks": 284, "free_inodes": 20},
            "runtime": {"stack": {"status": "unmeasured"},
                        "heap": {"status": "unmeasured"},
                        "storage_traffic": {"status": "unmeasured"}}}


class ResourceAccountingTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def invoke(self, command, candidate, base=None):
        path = self.directory / "candidate.json"
        path.write_text(json.dumps(candidate))
        arguments = [sys.executable, str(CHECKER), command, "--candidate", str(path)]
        if base is not None:
            base_path = self.directory / "base.json"
            base_path.write_text(json.dumps(base))
            arguments += ["--base", str(base_path), "--output", str(self.directory / "delta.json")]
        return subprocess.run(arguments, capture_output=True, text=True, check=False)

    def test_complete_static_snapshot_retains_runtime_residual(self):
        result = self.invoke("validate", snapshot())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("runtime remains unmeasured", result.stdout)

    def test_unchanged_surfaces_compare_without_growth(self):
        result = self.invoke("compare", snapshot(), snapshot())
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads((self.directory / "delta.json").read_text())
        self.assertEqual(data["positive_deltas"], [])
        self.assertEqual(data["runtime_acceptance"], "open")

    def test_kernel_load_extent_includes_initializers(self):
        candidate = snapshot()
        candidate["kernels"]["PICO"]["sections"][2]["lma"] = 0x1001FFF0
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("flash", result.stderr)

    def test_kernel_sram_section_cannot_cross_region(self):
        candidate = snapshot()
        candidate["kernels"]["PICO"]["sections"][3]["vma"] = 0x2003E7F0
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("region", result.stderr)

    def test_overlapping_kernel_sections_are_rejected(self):
        candidate = snapshot()
        candidate["kernels"]["PICO"]["sections"][3]["vma"] = 0x20024010
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("overlap", result.stderr)

    def test_aout_bss_counts_toward_process_window(self):
        candidate = snapshot()
        candidate["programs"]["/bin/fixture"]["bss"] = 147456
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("window", result.stderr)

    def test_missing_manifest_program_rejects_partial_measurement(self):
        candidate = snapshot()
        candidate["programs"] = {}
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("manifest", result.stderr)

    def test_exhausted_root_is_rejected(self):
        candidate = snapshot()
        candidate["root"]["data_blocks"] = 979
        candidate["root"]["free_blocks"] = 0
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("capacity", result.stderr)

    def test_one_byte_process_growth_cannot_use_other_program_savings(self):
        candidate = snapshot()
        candidate["programs"]["/bin/fixture"]["bss"] += 1
        result = self.invoke("compare", candidate, snapshot())
        self.assertNotEqual(result.returncode, 0)
        data = json.loads((self.directory / "delta.json").read_text())
        self.assertIn("program:/bin/fixture:loaded_bytes", data["positive_deltas"])

    def test_packed_blocks_include_single_indirect_metadata(self):
        candidate = snapshot()
        candidate["programs"]["/bin/fixture"].update(
            packed_bytes=27977, packed_blocks=29)
        result = self.invoke("validate", candidate)
        self.assertEqual(result.returncode, 0, result.stderr)
        candidate["programs"]["/bin/fixture"]["packed_blocks"] = 28
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("block", result.stderr)

    def test_added_archive_member_requires_review(self):
        candidate = snapshot()
        candidate["programs"]["/bin/fixture"]["libc_members"] = ["unused.o", "write.o"]
        result = self.invoke("compare", candidate, snapshot())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("archive", result.stderr)
        data = json.loads((self.directory / "delta.json").read_text())
        self.assertEqual(data["archive_changes"]["/bin/fixture"]["added"], ["unused.o"])

    def test_native_artifact_growth_is_a_constrained_surface(self):
        candidate = snapshot()
        native = {"/usr/lib/crt0.o": {"sha256": "1" * 64, "bytes": 64,
                                     "archive_members": []}}
        candidate["native_artifacts"] = copy.deepcopy(native)
        baseline = snapshot()
        baseline["native_artifacts"] = copy.deepcopy(native)
        candidate["native_artifacts"]["/usr/lib/crt0.o"]["bytes"] += 1
        result = self.invoke("compare", candidate, baseline)
        self.assertNotEqual(result.returncode, 0)
        data = json.loads((self.directory / "delta.json").read_text())
        self.assertIn("native:/usr/lib/crt0.o:bytes", data["positive_deltas"])

    def test_changed_measurement_tool_cannot_claim_comparable_receipt(self):
        candidate = snapshot()
        candidate["measurement_tools"]["fsutil"] = "5" * 64
        result = self.invoke("compare", candidate, snapshot())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("measurement", result.stderr)

    def test_missing_execution_provenance_is_rejected(self):
        candidate = snapshot()
        del candidate["execution"]["receipt_sha256"]
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("receipt_sha256", result.stderr)

    def test_different_toolchains_cannot_claim_paired_measurement(self):
        candidate = snapshot()
        candidate["toolchain"]["cc"] = "different compiler"
        result = self.invoke("compare", candidate, snapshot())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("toolchain", result.stderr)

    def test_unrelated_successful_receipt_cannot_be_distribution_evidence(self):
        status = self.directory / "status.json"
        log = self.directory / "gate.log"
        log.write_text("fixture completed\n")
        receipt = {"state": "completed", "exit_code": 0,
                   "terminal_queue_dropped_bytes": 0, "status_path": "status.json",
                   "log_path": "gate.log", "command": ["bmake", "MACHINE=rp2040", "build"]}
        status.write_text(json.dumps(receipt))
        closure = self.directory / "closure.json"
        closure.write_text(json.dumps({"configurations": {}, "provenance": {}}))
        result = subprocess.run([sys.executable, str(CHECKER), "capture", "--root",
                                 str(self.directory), "--closure", str(closure),
                                 "--distribution-receipt", str(status), "--output",
                                 str(self.directory / "resources.json")], capture_output=True,
                                text=True, check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("command", result.stderr)

    def test_changed_image_cannot_reuse_dependency_provenance(self):
        image = self.directory / "image"
        image.write_bytes(b"accepted image")
        provenance = {"image_sha256": shipped_resources.digest(image)}
        self.assertEqual(shipped_resources.bound_image(image, provenance),
                         provenance["image_sha256"])
        image.write_bytes(b"replacement image")
        with self.assertRaisesRegex(shipped_resources.ResourceError, "image"):
            shipped_resources.bound_image(image, provenance)

    def test_missing_runtime_field_is_rejected(self):
        candidate = snapshot()
        del candidate["runtime"]["heap"]
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("heap", result.stderr)


if __name__ == "__main__":
    unittest.main()
