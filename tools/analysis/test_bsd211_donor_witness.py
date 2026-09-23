"""Calibrate the pinned donor Git-object provenance chain."""

import copy
import gzip
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

import bsd211_donor_witness as witness
import bsd211_semantic_ledger as ledger


class DonorWitnessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = ledger.load(ledger.ROOT / ledger.LEDGER)
        cls.archive = (ledger.ROOT / cls.data["donor"]["witness_path"]).read_bytes()
        cls.contents = json.loads(gzip.decompress(cls.archive),
                                  object_pairs_hook=witness.unique_object)

    def setUp(self):
        self.data = copy.deepcopy(type(self).data)
        self.contents = copy.deepcopy(type(self).contents)

    def verify(self):
        witness.verify_witness(self.data, witness.archive_bytes(self.contents))

    def test_complete_witness_and_deterministic_archive(self):
        self.verify()
        self.assertEqual(witness.archive_bytes(self.contents), type(self).archive)
        self.assertEqual(hashlib.sha256(type(self).archive).hexdigest(),
                         self.data["donor"]["witness_sha256"])
        self.assertEqual(type(self).archive[:11],
                         b"\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff\x00")
        self.assertEqual(set(record["type"] for record in self.contents["objects"].values()),
                         {"commit", "tree", "blob"})

    def test_substituted_donor_tip_and_patch_mapping_fail(self):
        self.data["donor"]["commit"] = "0" * 40
        with self.assertRaisesRegex(ValueError, "donor witness identity"):
            self.verify()
        self.data = copy.deepcopy(type(self).data)
        self.data["patches"]["433"]["commit"] = self.data["patches"]["446"]["commit"]
        with self.assertRaisesRegex(ValueError, "patch 433 parent mismatch"):
            self.verify()

    def test_parent_raw_hash_and_donor_ledger_attestation_fail(self):
        self.data["patches"]["433"]["parent"] = "0" * 40
        with self.assertRaisesRegex(ValueError, "patch 433 parent mismatch"):
            self.verify()
        self.data = copy.deepcopy(type(self).data)
        self.data["patches"]["433"]["raw_patch_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "donor ledger attestation"):
            self.verify()
        self.data = copy.deepcopy(type(self).data)
        self.data["donor"]["ledger_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "donor patch ledger hash"):
            self.verify()

    def test_original_path_and_symbol_fail(self):
        self.data["fixes"][0]["original"][0]["path"] = "sys/sys/unrelated.c"
        with self.assertRaisesRegex(ValueError, "donor tree lacks"):
            self.verify()
        self.data = copy.deepcopy(type(self).data)
        self.data["fixes"][0]["original"][0]["symbol"] = "unavailable_symbol"
        with self.assertRaisesRegex(ValueError, "donor source symbol is absent"):
            self.verify()

    def test_corrupted_missing_and_extra_git_objects_fail(self):
        identifier = next(identifier for identifier, record in self.contents["objects"].items()
                          if record["type"] == "blob")
        self.contents["objects"][identifier]["data"] = "eA=="
        with self.assertRaisesRegex(ValueError, "donor object ID mismatch"):
            self.verify()
        self.contents = copy.deepcopy(type(self).contents)
        del self.contents["objects"][identifier]
        with self.assertRaisesRegex(ValueError, "missing donor object"):
            self.verify()
        self.contents = copy.deepcopy(type(self).contents)
        tree_id = next(identifier for identifier, record in self.contents["objects"].items()
                       if record["type"] == "tree")
        del self.contents["objects"][tree_id]
        with self.assertRaisesRegex(ValueError, "missing donor object"):
            self.verify()
        self.contents = copy.deepcopy(type(self).contents)
        extra = b"unrelated donor blob"
        extra_id = witness.git_object_id("blob", extra)
        self.contents["objects"][extra_id] = {
            "type": "blob", "data": "dW5yZWxhdGVkIGRvbm9yIGJsb2I=",
        }
        with self.assertRaisesRegex(ValueError, "unused donor witness object"):
            self.verify()

    def test_missing_witness_and_archive_hash_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "docs/research").mkdir(parents=True)
            (root / ledger.LEDGER).write_text(json.dumps(self.data))
            evidence = ledger.load(ledger.ROOT / self.data["execution_evidence"])
            (root / self.data["execution_evidence"]).write_text(json.dumps(evidence))
            report = ledger.ROOT / evidence["sanitized_report"]
            (root / evidence["sanitized_report"]).write_bytes(report.read_bytes())
            with self.assertRaises(FileNotFoundError):
                ledger.verify(root)
        self.data = copy.deepcopy(type(self).data)
        self.data["donor"]["witness_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "donor witness archive hash"):
            ledger.validate(self.data, ledger.load(
                ledger.ROOT / self.data["execution_evidence"]),
                (ledger.ROOT / "docs/research/211bsd-execution-report.sanitized.json").read_bytes(),
                type(self).archive,
                (ledger.ROOT / self.data["regression_bindings"]).read_bytes(),
                lambda *_: b"", lambda *_: "")


if __name__ == "__main__":
    unittest.main()
