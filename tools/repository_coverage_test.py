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


if __name__ == "__main__":
    unittest.main()
