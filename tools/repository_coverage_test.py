"""Calibrate the tracked-path coverage gate against real Git objects."""

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).with_name("repository_coverage.py")


class CoverageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        sources = {
            "bin/cat/cat.c": "int main(void) { return 0; }\n",
            "tests/cat/check.sh": "#!/bin/sh\nexit 0\n",
            "docs/report.md": "Fixture report\n",
            "legacy/source.c": "/* retained fixture */\n",
            "tools/c17/source-denominator.json": '{"rows": []}\n',
            "tools/test-execution-inventory.json": '{"schema": 1}\n',
        }
        for name, content in sources.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        self.git("init", "-q")
        self.git("add", ".")
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-qm", "fixture")
        self.snapshot = self.root / "coverage.json"
        result = self.run_tool("--initialize", "HEAD")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.data = json.loads(self.snapshot.read_text(encoding="utf-8"))

    def git(self, *arguments):
        return subprocess.check_output(["git", "-C", str(self.root), *arguments], text=True)

    def run_tool(self, *arguments):
        return subprocess.run(
            [sys.executable, str(TOOL), "--root", str(self.root),
             "--snapshot", str(self.snapshot), *arguments],
            capture_output=True, text=True,
        )

    def validate(self, data, reason=None, *arguments):
        self.snapshot.write_text(json.dumps(data), encoding="utf-8")
        result = self.run_tool(*arguments)
        if reason is None:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn(reason, result.stderr)

    def test_complete_classification_keeps_pending_review_visible(self):
        self.assertEqual(len(self.data["paths"]), 6)
        self.validate(self.data)
        self.validate(self.data, "pending review", "--require-reviewed")

    def test_missing_path(self):
        self.data["paths"].pop()
        self.validate(self.data, "missing tracked paths")

    def test_duplicate_path(self):
        self.data["paths"].append(copy.deepcopy(self.data["paths"][0]))
        self.validate(self.data, "duplicate path")

    def test_changed_blob(self):
        self.data["paths"][0]["blob"] = "0" * 40
        self.validate(self.data, "blob or mode differs")

    def test_unexpected_path(self):
        extra = copy.deepcopy(self.data["paths"][0])
        extra["path"] = "unexpected.c"
        self.data["paths"].append(extra)
        self.validate(self.data, "unexpected tracked paths")

    def test_falsely_closed_component(self):
        self.data["components"][0]["state"] = "reviewed"
        self.validate(self.data, "review witness")

    def test_unresolved_component(self):
        self.data["paths"][0]["component"] = "absent"
        self.validate(self.data, "unknown component")

    def test_stale_input(self):
        self.data["inputs"][0]["blob"] = "0" * 40
        self.validate(self.data, "input identity")

    def test_invalid_class(self):
        self.data["paths"][0]["artifact_class"] = "correct"
        self.validate(self.data, "artifact class")

    def test_review_witness_binds_paths_and_bytes(self):
        component = self.data["components"][0]
        paths = [row["path"] for row in self.data["paths"]
                 if row["component"] == component["id"]]
        witness = {"baseline": self.data["baseline"]["commit"], "paths": paths,
                   "method": "Read the source and compare the fixture contract.",
                   "failure_classes": ["incorrect return value"],
                   "source_evidence": paths, "tests": [], "findings": [],
                   "limitations": ["Source inspection only; fixture was not executed."]}
        path = self.root / "review.json"
        path.write_text(json.dumps(witness), encoding="utf-8")
        component.update(state="reviewed", next_action="",
                         review_witness={"path": "review.json",
                                         "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        self.validate(self.data)
        path.write_text("{}", encoding="utf-8")
        self.validate(self.data, "witness identity")

    def test_partial_review_witness(self):
        component = self.data["components"][0]
        path = self.root / "review.json"
        path.write_text(json.dumps({"baseline": self.data["baseline"]["commit"],
                                   "paths": []}), encoding="utf-8")
        component.update(state="reviewed", next_action="",
                         review_witness={"path": "review.json",
                                         "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        self.validate(self.data, "reviewed path set")

    def test_unresolved_witness_path(self):
        self.data["components"][0].update(
            state="reviewed", review_witness={"path": "missing.json", "sha256": "0" * 64})
        self.validate(self.data, "witness path")

    def test_unknown_state(self):
        self.data["components"][0]["state"] = "complete"
        self.validate(self.data, "unsupported state")

    def test_pending_without_next_action(self):
        self.data["components"][0]["next_action"] = ""
        self.validate(self.data, "next action")

    def test_initialize_preserves_existing_ledger(self):
        before = self.snapshot.read_bytes()
        result = self.run_tool("--initialize", "HEAD")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.snapshot.read_bytes(), before)

    def repair_witness(self):
        baseline = self.data["baseline"]["commit"]
        (self.root / "bin/cat/cat.c").write_text("int main(void) { return 1; }\n")
        self.git("add", "bin/cat/cat.c")
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-qm", "repair fixture")
        repair = self.git("rev-parse", "HEAD").strip()
        evidence = self.root / "test.log"
        evidence.write_text("fixture result: PASS\n")
        component = next(row for row in self.data["components"] if row["id"] == "bin/cat")
        witness = {
            "baseline": baseline, "paths": ["bin/cat/cat.c"],
            "method": "Review the fixed baseline and reproduce the fixture failure.",
            "failure_classes": ["incorrect exit status"],
            "source_evidence": ["bin/cat/cat.c"], "limitations": ["Fixture only."],
            "tests": [{"source_commit": repair,
                       "source_tree": self.git("rev-parse", "HEAD^{tree}").strip(),
                       "command": "fixture-check", "result": "pass", "evidence": "test.log",
                       "sha256": hashlib.sha256(evidence.read_bytes()).hexdigest()}],
            "findings": [{"id": "fixture-exit", "state": "repaired", "evidence": "fixture",
                          "repair_commit": repair, "test": "fixture-check"}],
        }
        component.update(state="reviewed", next_action="")
        return component, witness

    def save_witness(self, component, witness):
        path = self.root / "repair-review.json"
        path.write_text(json.dumps(witness), encoding="utf-8")
        component["review_witness"] = {
            "path": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}

    def test_descendant_repair_preserves_baseline_identities(self):
        component, witness = self.repair_witness()
        self.save_witness(component, witness)
        self.validate(self.data)

    def test_mutable_test_source_revision(self):
        component, witness = self.repair_witness()
        witness["tests"][0]["source_commit"] = "HEAD"
        self.save_witness(component, witness)
        self.validate(self.data, "complete commit identity")

    def test_stale_test_tree(self):
        component, witness = self.repair_witness()
        witness["tests"][0]["source_tree"] = "0" * 40
        self.save_witness(component, witness)
        self.validate(self.data, "test source tree differs")

    def test_unrelated_test_revision(self):
        component, witness = self.repair_witness()
        unrelated = self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                             "commit-tree", witness["tests"][0]["source_tree"],
                             "-m", "unrelated root").strip()
        witness["tests"][0]["source_commit"] = unrelated
        self.save_witness(component, witness)
        self.validate(self.data, "outside baseline ancestry")

    def test_test_before_repair(self):
        component, witness = self.repair_witness()
        witness["tests"][0].update(
            source_commit=self.data["baseline"]["commit"],
            source_tree=self.data["baseline"]["tree"])
        self.save_witness(component, witness)
        self.validate(self.data, "outside baseline ancestry")

    def test_repair_requires_passing_result(self):
        component, witness = self.repair_witness()
        witness["tests"][0]["result"] = "fail"
        self.save_witness(component, witness)
        self.validate(self.data, "passing test witness")

    def test_test_evidence_substitution(self):
        component, witness = self.repair_witness()
        self.save_witness(component, witness)
        (self.root / "test.log").write_text("fixture result: FAIL\n")
        self.validate(self.data, "test evidence identity differs")


if __name__ == "__main__":
    unittest.main()
