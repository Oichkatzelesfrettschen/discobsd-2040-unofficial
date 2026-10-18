"""Mutation controls for the DPUCP-2024 multicall composition ledger."""

import copy
import tempfile
from pathlib import Path

import check_multicall_composition as composition


def expect_failure(name, root, document):
    try:
        composition.validate_document(root, document)
    except composition.CompositionError:
        return
    raise AssertionError(f"mutation passed: {name}")


def main():
    root = Path(__file__).resolve().parents[1]
    document = composition.load_json(
        root / "docs/research/dpucp-2024-multicall-composition.json"
    )
    composition.validate_document(root, document)

    with tempfile.TemporaryDirectory() as temporary_name:
        map_path = Path(temporary_name) / "fixture.map"
        map_path.write_text(
            "Archive member included to satisfy reference by file (symbol)\n"
            "/build/libc.a(puts.o) caller.o (puts)\n"
            "/build/libc.a(write.o) /build/libc.a(puts.o) (write)\n"
            "                0x00000000 puts\n",
            encoding="utf-8",
        )
        members = composition.parse_libc_members(map_path)
        if members != ("libc.a(puts.o)", "libc.a(write.o)"):
            raise AssertionError(f"link-map member parse drift: {members}")

    cases = []

    changed = copy.deepcopy(document)
    changed["claims"]["posix_conformance"] = True
    cases.append(("POSIX conformance claim", changed))

    changed = copy.deepcopy(document)
    changed["denominator"]["multicall_executables"] += 1
    cases.append(("multicall denominator drift", changed))

    changed = copy.deepcopy(document)
    changed["denominator"]["manifest_hard_links"] -= 1
    cases.append(("manifest hard-link drift", changed))

    changed = copy.deepcopy(document)
    changed["boxes"].append(copy.deepcopy(changed["boxes"][0]))
    cases.append(("duplicate box", changed))

    changed = copy.deepcopy(document)
    changed["boxes"][0]["shipped_names"].pop()
    cases.append(("missing shipped applet", changed))

    changed = copy.deepcopy(document)
    changed["boxes"][0]["libc_member_count"] += 1
    cases.append(("archive-member total drift", changed))

    changed = copy.deepcopy(document)
    changed["archive_summary"]["consumer_edges"] += 1
    cases.append(("archive consumer-edge drift", changed))

    changed = copy.deepcopy(document)
    changed["root"]["candidate"]["free_blocks"] += 1
    cases.append(("packed-root arithmetic drift", changed))

    changed = copy.deepcopy(document)
    changed["clean_distribution_receipts"]["candidate"]["exit_code"] = 1
    cases.append(("failed clean-distribution receipt", changed))

    changed = copy.deepcopy(document)
    first_source = next(iter(changed["source_sha256"]))
    changed["source_sha256"][first_source] = "0" * 64
    cases.append(("source identity drift", changed))

    changed = copy.deepcopy(document)
    changed["boxes"][0]["aliases"].append("duplicate:cat")
    cases.append(("Makefile member drift", changed))

    for name, changed in cases:
        expect_failure(name, root, changed)

    print(
        "PASS DPUCP-2024 multicall composition calibration: "
        f"{len(cases)} mutations rejected"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
