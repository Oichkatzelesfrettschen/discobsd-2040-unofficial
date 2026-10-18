"""Calibrate per-surface resource accounting and reject unsupported acceptance."""

import copy
import hashlib
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
    return {"schema_version": 2, "source": {"commit": "b" * 40},
            "collector_sha256": "9" * 64,
            "toolchain": {"cc": "fixture compiler", "ld": "fixture linker",
                          "bmake": "fixture make"},
            "measurement_tools": {"hsaout": "2" * 64, "fsutil": "3" * 64,
                                  "objdump": "4" * 64},
            "manifest": {"sha256": "c" * 64, "packs": ["/bin/fixture"],
                         "native_files": ["/usr/lib/crt0.o"],
                         "links": {"/bin/alias": "/bin/fixture"}},
            "execution": {"state": "completed", "exit_code": 0,
                          "receipt_sha256": "d" * 64, "log_sha256": "e" * 64,
                          "closure_sha256": "a" * 64},
            "kernels": {"PICO": copy.deepcopy(kernel), "PICO_UART": copy.deepcopy(kernel)},
            "programs": {"/bin/fixture": {"sha256": "f" * 64, "text": 1024,
                "data": 32, "bss": 64, "raw_bytes": 1088,
                "packed_bytes": 900, "packed_blocks": 1,
                "libc_members": ["write.o"]}},
            "native_artifacts": {"/usr/lib/crt0.o": {"sha256": "1" * 64, "bytes": 64,
                                                       "archive_members": []}},
            "archive_selections": {
                "PICO": {"external:libgcc.a@" + "5" * 64: {"div.o": "6" * 64}},
                "PICO_UART": {"external:libgcc.a@" + "5" * 64: {"div.o": "6" * 64}},
                "root.bin.fixture": {"tree:lib/libc.a": {"write.o": "7" * 64},
                                     "tree:lib/libutil.a": {"helper.o": "8" * 64}},
                "native.usr.lib.crt0.o": {}},
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
        self.assertTrue((self.directory / "delta.json").is_file(), result.stderr)
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
        self.assertTrue((self.directory / "delta.json").is_file(), result.stderr)
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
        candidate["archive_selections"]["root.bin.fixture"]["tree:lib/libc.a"].update(
            {"unused.o": "b" * 64})
        result = self.invoke("compare", candidate, snapshot())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("archive", result.stderr)
        self.assertTrue((self.directory / "delta.json").is_file(), result.stderr)
        data = json.loads((self.directory / "delta.json").read_text())
        self.assertEqual(data["archive_changes"]["/bin/fixture"]["added"], ["unused.o"])

    def assert_added_selection_rejected(self, group, archive):
        candidate = snapshot()
        candidate["archive_selections"][group].setdefault(archive, {})["added.o"] = "c" * 64
        result = self.invoke("compare", candidate, snapshot())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("added archive dependencies", result.stderr)
        delta = json.loads((self.directory / "delta.json").read_text())
        self.assertEqual(delta["archive_selection_changes"][group][archive]["added"], ["added.o"])
        self.assertEqual(delta["positive_deltas"], [])

    def test_added_kernel_member_rejects_even_with_zero_numeric_growth(self):
        self.assert_added_selection_rejected("PICO", "external:libgcc.a@" + "5" * 64)

    def test_added_nonlibc_member_rejects_even_with_zero_numeric_growth(self):
        self.assert_added_selection_rejected("root.bin.fixture", "tree:lib/libutil.a")

    def test_added_archive_rejects_even_with_zero_numeric_growth(self):
        self.assert_added_selection_rejected("root.bin.fixture", "tree:lib/libnew.a")

    def test_removed_nonlibc_archive_is_visible_and_accepted(self):
        candidate = snapshot()
        del candidate["archive_selections"]["root.bin.fixture"]["tree:lib/libutil.a"]
        result = self.invoke("compare", candidate, snapshot())
        self.assertEqual(result.returncode, 0, result.stderr)
        delta = json.loads((self.directory / "delta.json").read_text())
        self.assertEqual(delta["archive_selection_changes"]["root.bin.fixture"]["tree:lib/libutil.a"],
                         {"added": [], "removed": ["helper.o"], "changed": []})

    def test_changed_member_bytes_are_visible_without_new_dependency(self):
        candidate = snapshot()
        candidate["archive_selections"]["root.bin.fixture"]["tree:lib/libutil.a"].update(
            {"helper.o": "d" * 64})
        result = self.invoke("compare", candidate, snapshot())
        self.assertEqual(result.returncode, 0, result.stderr)
        delta = json.loads((self.directory / "delta.json").read_text())
        self.assertEqual(delta["archive_selection_changes"]["root.bin.fixture"]["tree:lib/libutil.a"],
                         {"added": [], "removed": [], "changed": ["helper.o"]})

    def test_missing_archive_group_rejects_partial_comparison(self):
        candidate = snapshot()
        del candidate["archive_selections"]["PICO_UART"]
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("archive selection denominator", result.stderr)

    def test_libc_projection_cannot_omit_a_selected_member(self):
        candidate = snapshot()
        candidate["programs"]["/bin/fixture"]["libc_members"] = []
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("libc archive projection", result.stderr)

    def test_member_hash_and_name_validation_rejects_malformed_evidence(self):
        for members in ({"helper.o": "wrong hash"}, {"../helper.o": "e" * 64}, {}):
            with self.subTest(members=members):
                candidate = snapshot()
                candidate["archive_selections"]["root.bin.fixture"]["tree:lib/libutil.a"] = members
                result = self.invoke("validate", candidate)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("archive", result.stderr)

    def test_missing_closure_binding_rejects_complete_archive_claim(self):
        candidate = snapshot()
        del candidate["execution"]["closure_sha256"]
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("closure", result.stderr)

    def test_changed_collector_cannot_claim_paired_measurement(self):
        candidate = snapshot()
        candidate["collector_sha256"] = "f" * 64
        result = self.invoke("compare", candidate, snapshot())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("collector", result.stderr)

    def test_historical_receipt_validates_without_complete_comparison(self):
        historical = snapshot()
        historical["schema_version"] = 1
        del historical["archive_selections"]
        result = self.invoke("validate", historical)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("historical", result.stdout)
        result = self.invoke("compare", historical, historical)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("schema 2", result.stderr)

    def test_external_archive_identity_uses_retained_bytes_not_install_directory(self):
        self.assertTrue(callable(getattr(shipped_resources, "collect_archive_selections", None)),
                        "collector must preserve complete archive selections")
        groups = {
            "PICO": {"archive_members": {"external:/first/libgcc.a": {"div.o": "a" * 64}},
                     "external_inputs": {"external:/first/libgcc.a": "b" * 64}},
            "PICO_UART": {"archive_members": {"external:/second/libgcc.a": {"div.o": "a" * 64}},
                          "external_inputs": {"external:/second/libgcc.a": "b" * 64}}}
        actual = shipped_resources.collect_archive_selections(groups)
        expected_archive = {"external:libgcc.a@" + "b" * 64: {"div.o": "a" * 64}}
        self.assertEqual(actual, {"PICO": expected_archive, "PICO_UART": expected_archive})
        groups["PICO"]["external_inputs"] = {}
        with self.assertRaisesRegex(shipped_resources.ResourceError, "external archive"):
            shipped_resources.collect_archive_selections(groups)

    def test_collector_preserves_every_internal_archive_and_empty_group(self):
        groups = {
            "root.bin.fixture": {"archive_members": {
                "lib/libc.a": {"write.o": "a" * 64},
                "lib/libutil.a": {"helper.o": "b" * 64}}},
            "native.usr.lib.crt0.o": {"archive_members": {}}}
        actual = shipped_resources.collect_archive_selections(groups)
        self.assertEqual(actual, {
            "root.bin.fixture": {"tree:lib/libc.a": {"write.o": "a" * 64},
                                 "tree:lib/libutil.a": {"helper.o": "b" * 64}},
            "native.usr.lib.crt0.o": {}})

    def test_unsafe_tree_archive_identity_rejects_partial_ownership(self):
        candidate = snapshot()
        archives = candidate["archive_selections"]["root.bin.fixture"]
        archives["tree:../libutil.a"] = archives.pop("tree:lib/libutil.a")
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tree archive path", result.stderr)

    def test_colliding_archive_identities_cannot_erase_selection_evidence(self):
        self.assertTrue(callable(getattr(shipped_resources, "collect_archive_selections", None)),
                        "collector must preserve complete archive selections")
        groups = {"PICO": {"archive_members": {
            "external:/first/libgcc.a": {"first.o": "a" * 64},
            "external:/second/libgcc.a": {"second.o": "b" * 64}},
            "external_inputs": {"external:/first/libgcc.a": "c" * 64,
                                "external:/second/libgcc.a": "c" * 64}}}
        with self.assertRaisesRegex(shipped_resources.ResourceError, "collision"):
            shipped_resources.collect_archive_selections(groups)

    def test_native_archive_projection_and_growth_follow_full_selection(self):
        baseline = snapshot()
        baseline["manifest"]["native_files"].append("/usr/lib/libc.a")
        baseline["native_artifacts"]["/usr/lib/libc.a"] = {
            "sha256": "a" * 64, "bytes": 128, "archive_members": ["init.o"]}
        baseline["archive_selections"]["native.usr.lib.libc.a"] = {
            "tree:lib/libc/aout.libc.a": {"init.o": "b" * 64}}
        candidate = copy.deepcopy(baseline)
        candidate["native_artifacts"]["/usr/lib/libc.a"]["archive_members"] = []
        result = self.invoke("validate", candidate)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("native archive projection", result.stderr)
        candidate = copy.deepcopy(baseline)
        candidate["native_artifacts"]["/usr/lib/libc.a"]["archive_members"].append("unused.o")
        candidate["archive_selections"]["native.usr.lib.libc.a"][
            "tree:lib/libc/aout.libc.a"].update({"unused.o": "c" * 64})
        result = self.invoke("compare", candidate, baseline)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("added archive dependencies", result.stderr)
        delta = json.loads((self.directory / "delta.json").read_text())
        self.assertEqual(delta["archive_selection_changes"]["native.usr.lib.libc.a"][
            "tree:lib/libc/aout.libc.a"]["added"], ["unused.o"])

    def test_canonical_group_owns_exactly_its_expected_artifact(self):
        self.assertTrue(callable(getattr(shipped_resources, "verify_artifact_owners", None)))
        expected = {"PICO": {"path": "kernel/unix", "sha256": "a" * 64},
                    "root.bin.fixture": {"path": "dest/bin/fixture", "sha256": "b" * 64},
                    "native.usr.lib.crt0.o": {"path": "dest/usr/lib/crt0.o", "sha256": "c" * 64}}
        groups = {name: {"artifacts": [dict(artifact)]} for name, artifact in expected.items()}
        shipped_resources.verify_artifact_owners(groups, expected)
        for name in expected:
            for field, replacement in (("path", "other/object"), ("sha256", "d" * 64)):
                with self.subTest(group=name, field=field):
                    changed = copy.deepcopy(groups)
                    changed[name]["artifacts"][0][field] = replacement
                    with self.assertRaisesRegex(shipped_resources.ResourceError, "artifact owner"):
                        shipped_resources.verify_artifact_owners(changed, expected)

    def test_capture_rejects_misowned_groups_before_measurement(self):
        root = self.directory / "owner-fixture"
        root.mkdir()
        manifest = root / "distrib/rp2040/_manifest"
        manifest.parent.mkdir(parents=True)
        manifest.write_text("pack /bin/fixture\nfile /usr/lib/crt0.o\n")
        paths = {"PICO": "sys/arch/rp2040/compile/PICO/unix",
                 "PICO_UART": "sys/arch/rp2040/compile/PICO_UART/unix",
                 "root.bin.fixture": "distrib/obj/destdir.rp2040/bin/fixture",
                 "native.usr.lib.crt0.o": "distrib/obj/destdir.rp2040/usr/lib/crt0.o"}
        groups = {}
        for name, relative in paths.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            payload = (name + " attribution fixture").encode()
            path.write_bytes(payload)
            groups[name] = {"artifacts": [{"path": relative,
                             "sha256": hashlib.sha256(payload).hexdigest()}],
                            "archive_members": {}}
        log = root / "gate.log"
        log.write_text("attribution metadata control\n")
        receipt = {"state": "completed", "exit_code": 0,
                   "terminal_queue_dropped_bytes": 0, "status_path": "status.json",
                   "log_path": "gate.log", "command": ["bmake", "MACHINE=rp2040", "distribution"],
                   "started_at": "2000-01-01T00:00:00Z", "updated_at": "2000-01-01T00:00:01Z",
                   "invocation_id": "artifact-attribution-control"}
        status = root / "status.json"
        status.write_text(json.dumps(receipt))
        execution = {"receipt_sha256": hashlib.sha256(status.read_bytes()).hexdigest(),
                     "log_sha256": hashlib.sha256(log.read_bytes()).hexdigest(),
                     "started_at": receipt["started_at"], "updated_at": receipt["updated_at"],
                     "invocation_id": receipt["invocation_id"]}
        for victim in ("PICO_UART", "root.bin.fixture", "native.usr.lib.crt0.o"):
            with self.subTest(victim=victim):
                changed = copy.deepcopy(groups)
                changed["PICO"]["artifacts"].extend(changed[victim]["artifacts"])
                changed[victim]["artifacts"] = []
                closure = root / "closure.json"
                closure.write_text(json.dumps({"configurations": changed, "provenance": {
                    "distribution": execution,
                    "manifest_sha256": hashlib.sha256(manifest.read_bytes()).hexdigest()}}))
                result = subprocess.run([sys.executable, str(CHECKER), "capture", "--root",
                    str(root), "--closure", str(closure), "--distribution-receipt", str(status),
                    "--output", str(root / "resources.json")], capture_output=True,
                    text=True, check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("artifact owner", result.stderr)
                self.assertFalse((root / "resources.json").exists())

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
        self.assertTrue((self.directory / "delta.json").is_file(), result.stderr)
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
