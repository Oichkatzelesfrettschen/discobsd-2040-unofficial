"""Calibrate finite row membership, witnesses, provenance and residuals."""

import copy
import hashlib
import json
import unittest
from unittest import mock

import bsd211_semantic_ledger as ledger


class SemanticLedgerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = ledger.load(ledger.ROOT / ledger.LEDGER)
        cls.evidence = ledger.load(ledger.ROOT / cls.data["execution_evidence"])
        paths = set(cls.evidence["source_comparison"]["source_sha256"])
        paths.add("tools/test-execution-inventory.json")
        paths.update(source["path"] for row in cls.data["fixes"] for source in row["recipient"])
        comparison = cls.evidence["source_comparison"]
        cls.sources = {
            (revision, path): ledger.blob(ledger.ROOT, revision, path)
            for revision in (cls.data["recipient_commit"], comparison["executed_tree"])
            for path in paths
        }
        cls.trees = {
            cls.data["recipient_commit"]: ledger.tree(
                ledger.ROOT, cls.data["recipient_commit"]
            )
        }

    def setUp(self):
        self.data = copy.deepcopy(type(self).data)
        self.evidence = copy.deepcopy(type(self).evidence)
        self.sources = copy.deepcopy(type(self).sources)
        self.trees = copy.deepcopy(type(self).trees)

    def read_blob(self, revision, path):
        try:
            return self.sources[(revision, path)]
        except KeyError as error:
            raise ledger.InfrastructureError(f"missing fixture blob {revision}:{path}") from error

    def read_tree(self, revision):
        try:
            return self.trees[revision]
        except KeyError as error:
            raise ledger.InfrastructureError(f"missing fixture tree {revision}") from error

    def validate(self):
        return ledger.validate(self.data, self.evidence, self.read_blob, self.read_tree)

    def test_real_ledger_has_disjoint_executed_and_open_rows(self):
        executed, total = self.validate()
        self.assertEqual(total, len(self.data["declared_ids"]))
        self.assertGreater(executed, 0)
        self.assertLess(executed, total)

    def test_duplicate_and_missing_keys(self):
        self.data["fixes"].append(copy.deepcopy(self.data["fixes"][0]))
        with self.assertRaisesRegex(ValueError, "duplicate fix"):
            self.validate()
        self.data["fixes"].pop()
        self.data["fixes"].pop()
        with self.assertRaisesRegex(ValueError, "key-set mismatch"):
            self.validate()

    def test_duplicate_json_keys(self):
        with self.assertRaisesRegex(ValueError, "duplicate JSON key"):
            json.loads('{"id": 1, "id": 2}', object_pairs_hook=ledger.unique_object)

    def test_whole_series_claim_is_rejected(self):
        self.data["complete_numbered_series"] = True
        with self.assertRaisesRegex(ValueError, "complete numbered-series"):
            self.validate()

    def test_raw_hash_and_qualification_are_required(self):
        self.data["patches"]["433"]["raw_patch_sha256"] = "unknown"
        with self.assertRaisesRegex(ValueError, "raw hash"):
            self.validate()
        self.data = copy.deepcopy(type(self).data)
        self.data["donor"]["raw_hash_authority"] = "independently verified"
        with self.assertRaisesRegex(ValueError, "donor-ledger qualification"):
            self.validate()

    def test_executed_row_requires_receipt(self):
        self.data["fixes"][0]["variants"] = []
        with self.assertRaisesRegex(ValueError, "lacks behavioral witness"):
            self.validate()

    def test_skipped_or_wrong_invocation_receipt_fails(self):
        receipt = self.evidence["variants"][0]
        receipt["outcome"] = "SKIP"
        with self.assertRaisesRegex(ValueError, "lacks PASS"):
            self.validate()
        receipt["outcome"] = "PASS"
        receipt["invocation"] = ["true"]
        with self.assertRaisesRegex(ValueError, "invocation mismatch"):
            self.validate()

    def test_owner_width_and_executable_hash_are_required(self):
        for field, value, message in (("owner", "other-job", "owner/width"),
                                      ("width", "unknown", "owner/width"),
                                      ("executable_sha256", "", "executable hash")):
            with self.subTest(field=field):
                self.evidence = copy.deepcopy(type(self).evidence)
                self.evidence["variants"][0][field] = value
                with self.assertRaisesRegex(ValueError, message):
                    self.validate()

    def test_source_hash_symbol_and_regression_witness(self):
        comparison = self.evidence["source_comparison"]["source_sha256"]
        comparison[next(iter(comparison))] = "0" * 64
        with self.assertRaisesRegex(ValueError, "source hash mismatch"):
            self.validate()
        self.evidence = copy.deepcopy(type(self).evidence)
        self.data["fixes"][0]["recipient"][0]["symbol"] = "unavailable_symbol"
        with self.assertRaisesRegex(ValueError, "symbol is absent"):
            self.validate()
        self.data = copy.deepcopy(type(self).data)
        self.data["fixes"][0]["regression_sources"] = []
        with self.assertRaisesRegex(ValueError, "missing regression source"):
            self.validate()

    def test_changed_or_missing_executed_source_is_rejected(self):
        comparison = self.evidence["source_comparison"]
        path = next(iter(comparison["source_sha256"]))
        key = (comparison["executed_tree"], path)
        self.sources[key] = b"changed executed source\n"
        with self.assertRaisesRegex(ValueError, "executed source hash mismatch"):
            self.validate()
        self.sources = copy.deepcopy(type(self).sources)
        del self.sources[key]
        with self.assertRaisesRegex(ledger.InfrastructureError, "missing fixture blob"):
            self.validate()

    def test_recipient_tree_and_retained_executed_commit_are_authenticated(self):
        self.trees[self.data["recipient_commit"]] = "0" * 40
        with self.assertRaisesRegex(ValueError, "recipient commit tree mismatch"):
            self.validate()
        self.trees = copy.deepcopy(type(self).trees)
        comparison = self.evidence["source_comparison"]
        comparison["executed_commit_object_base64"] = "eA=="
        comparison["executed_commit_object_sha256"] = hashlib.sha256(b"x").hexdigest()
        with self.assertRaisesRegex(ValueError, "executed commit object ID mismatch"):
            self.validate()

    def test_pinned_inventory_controls_receipt_expectations(self):
        comparison = self.evidence["source_comparison"]
        inventory_path = "tools/test-execution-inventory.json"
        recipient_key = (self.data["recipient_commit"], inventory_path)
        executed_key = (comparison["executed_tree"], inventory_path)
        inventory = ledger.load_blob(self.sources[recipient_key], "fixture inventory")
        receipt = self.evidence["variants"][0]
        variant = next(item for item in inventory["variants"]
                       if item["id"] == receipt["variant"])
        variant["owner"] = "pinned-other-job"
        contents = json.dumps(inventory, sort_keys=True).encode()
        self.sources[recipient_key] = contents
        self.sources[executed_key] = contents
        self.evidence["inventory_sha256"] = hashlib.sha256(contents).hexdigest()
        with self.assertRaisesRegex(ValueError, "owner/width drift"):
            self.validate()

    def test_verification_does_not_load_the_working_inventory(self):
        loaded = []
        original_load = ledger.load

        def recording_load(path):
            loaded.append(path.relative_to(ledger.ROOT).as_posix())
            return original_load(path)

        with mock.patch.object(ledger, "load", side_effect=recording_load):
            self.assertEqual(ledger.verify(ledger.ROOT), (9, 15))
        self.assertEqual(loaded, [ledger.LEDGER, self.data["execution_evidence"]])

    def test_receipt_forged_to_match_live_inventory_still_fails(self):
        self.validate()
        live_inventory = ledger.load(ledger.ROOT / "tools/test-execution-inventory.json")
        receipt = self.evidence["variants"][0]
        variant = next(item for item in live_inventory["variants"]
                       if item["id"] == receipt["variant"])
        variant["owner"] = "working-tree-only-owner"
        receipt["owner"] = variant["owner"]
        with self.assertRaisesRegex(ValueError, "owner/width drift"):
            self.validate()

    def test_executed_inventory_identity_is_checked_separately(self):
        comparison = self.evidence["source_comparison"]
        inventory_path = "tools/test-execution-inventory.json"
        key = (comparison["executed_tree"], inventory_path)
        self.sources[key] += b"\n"
        with self.assertRaisesRegex(ValueError, "executed execution inventory hash mismatch"):
            self.validate()

    def test_open_row_requires_next_action(self):
        row = next(row for row in self.data["fixes"] if row["validation"] == "open")
        row["next_action"] = ""
        with self.assertRaisesRegex(ValueError, "missing next_action"):
            self.validate()

    def test_dependency_cycle_unknown_and_open_dependency(self):
        first, second = self.data["fixes"][0], self.data["fixes"][1]
        first["dependencies"] = ["unavailable"]
        with self.assertRaisesRegex(ValueError, "unknown dependency"):
            self.validate()
        first["dependencies"] = [second["id"]]
        with self.assertRaisesRegex(ValueError, "open dependency"):
            self.validate()
        first["dependencies"] = [first["id"]]
        with self.assertRaisesRegex(ValueError, "dependency cycle"):
            self.validate()

    def test_unsafe_path_and_missing_git_object(self):
        self.data["fixes"][0]["original"][0]["path"] = "../outside"
        with self.assertRaisesRegex(ValueError, "unsafe"):
            self.validate()
        with self.assertRaises(ledger.InfrastructureError):
            ledger.blob(ledger.ROOT, "0" * 40, "AGENTS.md")

    def test_summary_projection_rejects_missing_or_changed_rows(self):
        contents = "<!-- semantic-ledger-summary:start -->\n" + ledger.summary(self.data) + \
            "\n<!-- semantic-ledger-summary:end -->"
        ledger.check_summary(self.data, contents)
        with self.assertRaisesRegex(ValueError, "summary differs"):
            ledger.check_summary(self.data, contents.replace("present", "missing", 1))
        with self.assertRaisesRegex(ValueError, "marker count"):
            ledger.check_summary(self.data, "")

    def test_receipt_and_source_comparison_cannot_be_dropped(self):
        needed = self.data["fixes"][0]["variants"][0]
        self.evidence["variants"] = [receipt for receipt in self.evidence["variants"]
                                     if receipt["variant"] != needed]
        with self.assertRaisesRegex(ValueError, "missing execution receipt"):
            self.validate()
        self.evidence = copy.deepcopy(type(self).evidence)
        path = self.data["fixes"][0]["recipient"][0]["path"]
        del self.evidence["source_comparison"]["source_sha256"][path]
        with self.assertRaisesRegex(ValueError, "missing execution/source comparison"):
            self.validate()


if __name__ == "__main__":
    unittest.main()
