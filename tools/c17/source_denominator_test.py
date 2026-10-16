"""Mutation controls for the finite C17 source denominator."""

import copy
import hashlib
import json
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
        (root / "bin").mkdir()
        (root / "src").mkdir()
        (root / "docs").mkdir()
        (root / "sys/arch/stm32/dev").mkdir(parents=True)
        (root / "legacy/unsupported-platforms/usr.bin/picoc/platform").mkdir(parents=True)
        (root / "bin/good.c").write_text("int good(void) { return 0; }\n", encoding="utf-8")
        (root / "sys/arch/stm32/dev/uart.c").write_text(
            "int stm32_uart(void) { return 0; }\n", encoding="utf-8"
        )
        (root / "legacy/unsupported-platforms/usr.bin/picoc/platform/library_ffox.c").write_text(
            "int ffox(void) { return 0; }\n", encoding="utf-8"
        )
        (root / "src/guest.h").write_text("#define GUEST 1\n", encoding="utf-8")
        (root / "docs/receipt.md").write_text("receipt\n", encoding="utf-8")
        digest = hashlib.sha256((root / "src/guest.h").read_bytes()).hexdigest()
        stm32_classification = source_denominator.derive_classification(
            root,
            {"path": "sys/arch/stm32/dev/uart.c", "language_state": "unreviewed"},
        )
        if stm32_classification["build_surface"] != "maintained-arm":
            raise AssertionError("STM32 source lost its maintained ARM boundary")
        if stm32_classification["required_mode"] != "gnu17-freestanding":
            raise AssertionError("STM32 source lost its GNU17 freestanding mode")
        unsupported_classification = source_denominator.derive_classification(
            root,
            {
                "path": "legacy/unsupported-platforms/usr.bin/picoc/platform/library_ffox.c",
                "language_state": "unreviewed",
            },
        )
        if unsupported_classification["required_mode"] != "not-built":
            raise AssertionError("unsupported legacy source gained a production dialect")
        rows = [
            {
                "path": "bin/good.c",
                "class": "A",
                "scope": "maintained",
                "owner": "src",
                "profile": "modernized-cross-userland",
                "native_input": "no",
                "language_state": "accepted",
                "evidence": ["docs/receipt.md"],
                "blocker": "",
                "classification": {
                    "source_kind": "translation-unit",
                    "build_surface": "rp2040-userland",
                    "required_mode": "strict-c17-and-gnu17",
                    "extension_policy": "portable-only",
                    "extension_signals": [],
                    "risk_family": "portable-leaf",
                    "hazard_basis": "lexical-candidates-v1",
                    "hazards": {
                        "integer": [],
                        "pointer": [],
                        "prototype": [],
                        "variadic": [],
                        "aliasing": [],
                        "object_lifetime": [],
                        "inline_assembly": [],
                    },
                },
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
                "classification": {
                    "source_kind": "header",
                    "build_surface": "legacy-isolated",
                    "required_mode": "not-built",
                    "extension_policy": "not-applicable",
                    "extension_signals": [],
                    "risk_family": "legacy-isolated",
                    "hazard_basis": "not-applicable",
                    "hazards": {
                        "integer": [],
                        "pointer": [],
                        "prototype": [],
                        "variadic": [],
                        "aliasing": [],
                        "object_lifetime": [],
                        "inline_assembly": [],
                    },
                },
            },
        ]
        ledger = {"schema_version": 2, "rows": rows}
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
        discovered = ["bin/good.c", "src/guest.h"]
        source_denominator.validate_documents(root, discovered, ledger, quarantine, True)

        refresh_ledger = copy.deepcopy(ledger)
        refresh_ledger["rows"].append(copy.deepcopy(refresh_ledger["rows"][0]))
        refresh_path = root / "source-denominator.json"
        refresh_path.write_text(json.dumps(refresh_ledger) + "\n", encoding="utf-8")
        refresh_bytes = refresh_path.read_bytes()
        try:
            source_denominator.refresh_classification(
                root, refresh_path, discovered, quarantine, False
            )
        except source_denominator.DenominatorError:
            pass
        else:
            raise AssertionError("invalid refresh replaced the prior ledger")
        if refresh_path.read_bytes() != refresh_bytes:
            raise AssertionError("failed refresh changed ledger bytes")

        (root / "bin/good.c").write_text(
            "int good(void) { __asm__(\"nop\"); return 0; }\n", encoding="utf-8"
        )
        expect_failure("source hazard drift", root, discovered, ledger, quarantine, True)
        (root / "bin/good.c").write_text("int good(void) { return 0; }\n", encoding="utf-8")

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
        changed["rows"][0]["classification"]["required_mode"] = "gnu23"
        cases.append(("unknown required mode", changed, quarantine, discovered, False))
        changed = copy.deepcopy(ledger)
        changed["rows"][0]["classification"]["hazards"]["pointer"] = ["fabricated"]
        cases.append(("fabricated hazard signal", changed, quarantine, discovered, False))
        changed = copy.deepcopy(ledger)
        changed["rows"][0]["classification"]["build_surface"] = "rp2040-kernel"
        cases.append(("classification drift", changed, quarantine, discovered, False))
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
    print(f"PASS C17 source denominator calibration: {len(cases) + 2} mutations rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
