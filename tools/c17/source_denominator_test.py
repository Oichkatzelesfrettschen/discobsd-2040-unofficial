"""Mutation controls for the finite C17 source denominator."""

import copy
import hashlib
import sys
import tempfile
from pathlib import Path

import source_denominator


def expect_failure(name, root, discovered, ledger, quarantine, closure=False):
    try:
        source_denominator.validate_documents(root, discovered, ledger, quarantine, closure)
    except source_denominator.DenominatorError:
        return
    raise AssertionError(f"mutation passed: {name}")


def main():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / "src").mkdir()
        (root / "docs").mkdir()
        (root / "src/good.c").write_text("int good(void) { return 0; }\n", encoding="utf-8")
        (root / "src/guest.h").write_text("#define GUEST 1\n", encoding="utf-8")
        (root / "docs/receipt.md").write_text("receipt\n", encoding="utf-8")
        digest = hashlib.sha256((root / "src/guest.h").read_bytes()).hexdigest()
        rows = [
            {
                "path": "src/good.c",
                "class": "A",
                "scope": "maintained",
                "owner": "src",
                "profile": "modernized-cross-userland",
                "native_input": "no",
                "language_state": "accepted",
                "evidence": ["docs/receipt.md"],
                "blocker": "",
            },
            {
                "path": "src/guest.h",
                "class": "D",
                "scope": "quarantine",
                "owner": "src/guest",
                "profile": "quarantined-historical-source",
                "native_input": "no",
                "language_state": "excluded",
                "evidence": ["docs/receipt.md"],
                "blocker": "",
            },
        ]
        ledger = {"schema_version": 1, "rows": rows}
        quarantine = {
            "schema_version": 1,
            "entries": [
                {
                    "path": "src/guest.h",
                    "scope": "quarantine",
                    "sha256": digest,
                    "provenance": "docs/receipt.md",
                    "reason": "fixture",
                }
            ],
        }
        discovered = ["src/good.c", "src/guest.h"]
        source_denominator.validate_documents(root, discovered, ledger, quarantine, True)

        cases = []
        changed = copy.deepcopy(ledger)
        changed["rows"].pop()
        cases.append(("missing tracked path", changed, quarantine, discovered, False))
        changed = copy.deepcopy(ledger)
        changed["rows"].append(copy.deepcopy(changed["rows"][0]))
        cases.append(("duplicate path", changed, quarantine, discovered, False))
        changed = copy.deepcopy(ledger)
        changed["rows"][0]["class"] = "unknown"
        cases.append(("unknown class", changed, quarantine, discovered, False))
        changed = copy.deepcopy(ledger)
        del changed["rows"][0]["owner"]
        cases.append(("missing field", changed, quarantine, discovered, False))
        changed = copy.deepcopy(ledger)
        changed["rows"][0]["path"] = "../escape.c"
        cases.append(("path traversal", changed, quarantine, discovered, False))
        changed = copy.deepcopy(ledger)
        changed["rows"][0]["evidence"] = []
        cases.append(("accepted without evidence", changed, quarantine, discovered, False))
        changed = copy.deepcopy(ledger)
        changed["rows"][0]["class"] = "unreviewed"
        changed["rows"][0]["scope"] = "unreviewed"
        cases.append(
            ("accepted with unreviewed classification", changed, quarantine, discovered, True)
        )
        changed = copy.deepcopy(quarantine)
        changed["entries"][0]["sha256"] = "0" * 64
        cases.append(("changed quarantine bytes", ledger, changed, discovered, False))
        changed = copy.deepcopy(ledger)
        changed["rows"][0]["class"] = "unreviewed"
        changed["rows"][0]["scope"] = "unreviewed"
        changed["rows"][0]["profile"] = "unreviewed"
        changed["rows"][0]["native_input"] = "unreviewed"
        changed["rows"][0]["language_state"] = "unreviewed"
        changed["rows"][0]["evidence"] = []
        changed["rows"][0]["blocker"] = "review"
        cases.append(("closure with unreviewed row", changed, quarantine, discovered, True))
        for name, mutated_ledger, mutated_quarantine, paths, closure in cases:
            expect_failure(name, root, paths, mutated_ledger, mutated_quarantine, closure)
    print(f"PASS C17 source denominator calibration: {len(cases)} mutations rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
