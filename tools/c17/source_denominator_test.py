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
        (root / "sys/arch/rp2040/test/swapram").mkdir(parents=True)
        (root / "tools/flash-id").mkdir(parents=True)
        (root / "legacy/unsupported-platforms/usr.bin/picoc/platform").mkdir(parents=True)
        (root / "bin/good.c").write_text("int good(void) { return 0; }\n", encoding="utf-8")
        (root / "sys/arch/stm32/dev/uart.c").write_text(
            "int stm32_uart(void) { return 0; }\n", encoding="utf-8"
        )
        (root / "sys/arch/rp2040/test/swapram/swapram_test.c").write_text(
            "int swapram_test(void) { return 0; }\n", encoding="utf-8"
        )
        (root / "tools/flash-id/main.c").write_text(
            "int main(void) { return 0; }\n", encoding="utf-8"
        )
        (root / "bin/hazards.c").write_text(
            "#define OFFSET(type, member) __builtin_offsetof(type, member)\n"
            "#define NOOP() __asm__ __volatile__(\"nop\")\n"
            "#define TYPEOF(value)__typeof__(value)\n"
            "int old();\n"
            "void hazards(void *pointer, ...) {\n"
            "  union item { int value; } item;\n"
            "  int integer = (int)(uintptr_t)pointer;\n"
            "  char *zero = (char *)0;\n"
            "  va_list arguments;\n"
            "  int alias = *(int *)pointer;\n"
            "  char *storage = malloc(1);\n"
            "  int compound = (int){0};\n"
            "  integer <<= 1;\n"
            "}\n",
            encoding="utf-8",
        )
        (root / "bin/hazard_strings.c").write_text(
            "#define TEXT \"__builtin_alloca(x) __asm__ __volatile__(\\\"nop\\\")\"\n"
            "/* malloc(pointer); union hidden; (int){0}; */\n"
            "int clean(void) { return 0; }\n",
            encoding="utf-8",
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
        host_test_classification = source_denominator.derive_classification(
            root,
            {
                "path": "sys/arch/rp2040/test/swapram/swapram_test.c",
                "language_state": "unreviewed",
            },
        )
        if host_test_classification["build_surface"] != "host-test":
            raise AssertionError("RP2040 host test was classified as target kernel code")
        if host_test_classification["required_mode"] != "gnu17-hosted":
            raise AssertionError("RP2040 host test lost its hosted GNU17 mode")
        board_probe_classification = source_denominator.derive_classification(
            root,
            {"path": "tools/flash-id/main.c", "language_state": "unreviewed"},
        )
        if board_probe_classification["build_surface"] != "rp2040-board-probe":
            raise AssertionError("Pico SDK board probe was classified as a host tool")
        if board_probe_classification["required_mode"] != "pico-sdk-c11":
            raise AssertionError("Pico SDK board probe lost its configured C11 mode")
        if board_probe_classification["risk_tier"] != "T1":
            raise AssertionError("Pico SDK board probe lost its boundary risk tier")
        if board_probe_classification["risk_tier_basis"] != "boundary-family-conservative":
            raise AssertionError("Pico SDK board probe lost its tier basis")
        hazard_classification = source_denominator.derive_classification(
            root, {"path": "bin/hazards.c", "language_state": "unreviewed"}
        )
        if hazard_classification["extension_signals"] != [
            "gnu-typeof",
            "inline-assembly",
            "target-builtin",
        ]:
            raise AssertionError("macro extension signals were not classified")
        expected_hazards = {
            "aliasing": ["cast-dereference", "union-access"],
            "inline_assembly": ["inline-assembly"],
            "integer": ["integer-cast", "shift-expression"],
            "object_lifetime": ["compound-literal", "dynamic-storage"],
            "pointer": ["integer-pointer-cast", "pointer-integer-cast"],
            "prototype": ["old-style-declaration"],
            "variadic": ["variadic-interface"],
        }
        if hazard_classification["hazards"] != expected_hazards:
            raise AssertionError("hazard families lost calibrated positive candidates")
        clean_classification = source_denominator.derive_classification(
            root, {"path": "bin/hazard_strings.c", "language_state": "unreviewed"}
        )
        if clean_classification["extension_signals"]:
            raise AssertionError("literal text produced an extension candidate")
        if any(clean_classification["hazards"].values()):
            raise AssertionError("comment or literal text produced a hazard candidate")
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
                    "risk_tier": "unreviewed",
                    "risk_tier_basis": "caller-closure-required",
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
                    "risk_tier": "Q",
                    "risk_tier_basis": "quarantine",
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

        count_report = root / "docs/research/c17-semantic-migration-plan.md"
        count_report.parent.mkdir(parents=True)
        fixture_counts = {
            "accepted": 1,
            "excluded": 1,
            "migration-open": 0,
            "unreviewed": 0,
        }
        count_report.write_text(
            "Those classifications preserve all 1 accepted, 0 migration-open, 1\n"
            "excluded and 0 unreviewed language states.\n",
            encoding="utf-8",
        )
        source_denominator.validate_count_report(root, fixture_counts)
        count_report.write_text(
            "Those classifications preserve all 1 accepted, 1 migration-open, 1\n"
            "excluded and 0 unreviewed language states.\n",
            encoding="utf-8",
        )
        try:
            source_denominator.validate_count_report(root, fixture_counts)
        except source_denominator.DenominatorError:
            pass
        else:
            raise AssertionError("stale denominator prose was accepted")

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

        atomic_path = root / "atomic-ledger.json"
        atomic_path.write_bytes(b"original ledger bytes\n")
        atomic_bytes = atomic_path.read_bytes()

        class FailingTemporaryFile:
            def __init__(self, wrapped):
                self.wrapped = wrapped
                self.name = wrapped.name

            def __enter__(self):
                self.wrapped.__enter__()
                return self

            def __exit__(self, exception_type, exception, traceback):
                return self.wrapped.__exit__(exception_type, exception, traceback)

            def write(self, _text):
                raise OSError("injected temporary write failure")

            def fileno(self):
                return self.wrapped.fileno()

        def failing_temporary_file(**keywords):
            return FailingTemporaryFile(tempfile.NamedTemporaryFile(**keywords))

        try:
            source_denominator.atomic_write_text(
                atomic_path,
                "replacement ledger bytes\n",
                temporary_file_factory=failing_temporary_file,
            )
        except OSError as error:
            if str(error) != "injected temporary write failure":
                raise
        else:
            raise AssertionError("temporary write failure was not propagated")
        if atomic_path.read_bytes() != atomic_bytes:
            raise AssertionError("temporary write failure changed destination bytes")
        if list(root.glob(".atomic-ledger.json.*.tmp")):
            raise AssertionError("temporary write failure left a sibling file")

        def failing_replace(_source, _destination):
            raise OSError("injected replacement failure")

        try:
            source_denominator.atomic_write_text(
                atomic_path,
                "replacement ledger bytes\n",
                replace_file=failing_replace,
            )
        except OSError as error:
            if str(error) != "injected replacement failure":
                raise
        else:
            raise AssertionError("replacement failure was not propagated")
        if atomic_path.read_bytes() != atomic_bytes:
            raise AssertionError("replacement failure changed destination bytes")
        if list(root.glob(".atomic-ledger.json.*.tmp")):
            raise AssertionError("replacement failure left a sibling file")

        sync_path = root / "sync-ledger.json"
        sync_path.write_text(json.dumps(ledger) + "\n", encoding="utf-8")
        (root / "bin/new.c").write_text(
            "int new_source(void) { return 0; }\n", encoding="utf-8"
        )
        synced_discovered = ["bin/new.c", "src/guest.h"]
        source_denominator.sync_paths(
            root,
            sync_path,
            synced_discovered,
            quarantine,
            {"bin/new.c"},
            {"bin/good.c"},
        )
        synced = source_denominator.load_json(sync_path)
        if [row["path"] for row in synced["rows"]] != synced_discovered:
            raise AssertionError("path sync did not apply the exact sorted delta")
        guest_row = next(row for row in synced["rows"] if row["path"] == "src/guest.h")
        if guest_row != ledger["rows"][1]:
            raise AssertionError("path sync changed a retained row")
        new_row = next(row for row in synced["rows"] if row["path"] == "bin/new.c")
        if new_row["language_state"] != "unreviewed":
            raise AssertionError("path sync admitted an unreviewed new source")

        (root / "bin/new.c").write_text(
            "int new_source(void) { __asm__(\"nop\"); return 0; }\n",
            encoding="utf-8",
        )
        source_denominator.sync_paths(
            root,
            sync_path,
            synced_discovered,
            quarantine,
            set(),
            set(),
        )
        refreshed_sync = source_denominator.load_json(sync_path)
        refreshed_new_row = next(
            row for row in refreshed_sync["rows"] if row["path"] == "bin/new.c"
        )
        if refreshed_new_row["classification"]["extension_signals"] != [
            "inline-assembly"
        ]:
            raise AssertionError("path sync left retained classification stale")

        rejected_sync_path = root / "rejected-sync-ledger.json"
        rejected_sync_path.write_text(json.dumps(ledger) + "\n", encoding="utf-8")
        rejected_sync_bytes = rejected_sync_path.read_bytes()
        try:
            source_denominator.sync_paths(
                root,
                rejected_sync_path,
                synced_discovered,
                quarantine,
                set(),
                {"bin/good.c"},
            )
        except source_denominator.DenominatorError:
            pass
        else:
            raise AssertionError("path sync accepted an undeclared addition")
        if rejected_sync_path.read_bytes() != rejected_sync_bytes:
            raise AssertionError("rejected path sync changed ledger bytes")

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
        changed["rows"][0]["classification"]["risk_tier"] = "T0"
        cases.append(("unknown risk tier", changed, quarantine, discovered, False))
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
    print(f"PASS C17 source denominator calibration: {len(cases) + 6} failures rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
