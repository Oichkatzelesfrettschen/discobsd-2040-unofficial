"""Mutation controls for the DPUCP-2024 multicall composition ledger."""

import copy
import tempfile
from pathlib import Path
from unittest import mock

import check_multicall_composition as composition


def mutation_is_rejected(root, document):
    try:
        composition.validate_document(root, document)
    except composition.CompositionError:
        return True
    return False


def makefile_contract_failures(root):
    makefile_text = (root / "Makefile").read_text(encoding="utf-8")
    required_composition_order = (
        "CROSS_COMPOSITION_GATES=\tcheck-libc-array-memory-line-cross \\\n"
        "\t\t.WAIT check-timeout-target-link .WAIT \\\n"
        "\t\tcheck-multicall-composition"
    )
    failures = []
    if required_composition_order not in makefile_text:
        failures.append("parallel composition gates")
    image_prerequisite = (
        "check-multicall-composition: check-python .WAIT symlinks "
        ".WAIT ${FSIMG}"
    )
    if image_prerequisite not in makefile_text:
        failures.append("missing root image prerequisite")
    if "${MAKE} -C ${TOPSRC}/bin/sh MACHINE=${MACHINE} clean all" in makefile_text:
        failures.append("parallel shell clean and all")
    box_clean_then_link = (
        "\t\t\t${MAKE} -C ${TOPSRC}/$$directory MACHINE=${MACHINE} clean; \\\n"
        "\t\t\t${MAKE} -C ${TOPSRC}/$$directory MACHINE=${MACHINE} \\\n"
        "\t\t\t    LINK_MAP=\"$$map_root/$$program.map\" all;"
    )
    if box_clean_then_link not in makefile_text:
        failures.append("stale multicall link maps")
    return failures


def main():
    root = Path(__file__).resolve().parents[1]
    document = composition.load_json(
        root / "docs/research/dpucp-2024-multicall-composition.json"
    )
    composition.validate_document(root, document)

    size_records = composition.parse_size_inventory(
        root / document["measurement_inventory"]["path"]
    )
    live_measurements = [
        (
            record["raw_bytes"],
            record["packed_bytes"],
            record["raw_blocks"],
            record["packed_blocks"],
        )
        for record in size_records
    ]
    live_measurements[0] = (34016, 28170, 35, 29)
    composition.validate_live_measurements(
        size_records, document["boxes"], live_measurements, exact=False
    )
    try:
        composition.validate_live_measurements(
            size_records, document["boxes"], live_measurements, exact=True
        )
    except composition.CompositionError:
        pass
    else:
        raise AssertionError("exact measurement accepted cross-toolchain byte drift")
    no_savings = list(live_measurements)
    first_box_standalone = sum(
        measurement[3]
        for record, measurement in zip(size_records, live_measurements)
        if record["kind"] == "standalone"
        and record["box"] == document["boxes"][0]["id"]
    )
    no_savings[0] = (*no_savings[0][:3], first_box_standalone)
    try:
        composition.validate_live_measurements(
            size_records, document["boxes"], no_savings, exact=False
        )
    except composition.CompositionError:
        pass
    else:
        raise AssertionError("cross-toolchain measurement accepted no block savings")

    candidate_receipt = composition.load_json(
        root
        / document["clean_distribution_receipts"]["candidate"]["receipt_path"]
    )
    retained_root = candidate_receipt["root_image"]
    live_root = {
        field: retained_root[field]
        for field in (
            "partition_blocks",
            "inode_list_blocks",
            "free_blocks",
            "free_inodes",
            "data_blocks",
        )
    }
    live_root["data_blocks"] += 1
    live_root["free_blocks"] -= 1
    composition.validate_live_root_measurement(
        document, retained_root, live_root, exact=False
    )
    try:
        composition.validate_live_root_measurement(
            document, retained_root, live_root, exact=True
        )
    except composition.CompositionError:
        pass
    else:
        raise AssertionError("exact root measurement accepted cross-toolchain drift")
    invalid_root = dict(live_root)
    invalid_root["partition_blocks"] += 1
    try:
        composition.validate_live_root_measurement(
            document, retained_root, invalid_root, exact=False
        )
    except composition.CompositionError:
        pass
    else:
        raise AssertionError("cross-toolchain root accepted partition drift")

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

    retained_members = ("libc.a(memcpy.o)", "libc.a(write.o)")
    linux_members = (
        "libc.a(memcpy.o)",
        "libc.a(memmove.o)",
        "libc.a(write.o)",
    )
    added_members, removed_members = composition.validate_live_archive_members(
        retained_members, linux_members, exact=False
    )
    if added_members != ("libc.a(memmove.o)",) or removed_members:
        raise AssertionError(
            "cross-toolchain archive delta accounting differs: "
            f"added={added_members}, removed={removed_members}"
        )
    try:
        composition.validate_live_archive_members(
            retained_members, linux_members, exact=True
        )
    except composition.CompositionError:
        pass
    else:
        raise AssertionError("exact archive replay accepted a linker-member delta")
    try:
        composition.validate_live_archive_members(
            retained_members, (), exact=False
        )
    except composition.CompositionError:
        pass
    else:
        raise AssertionError("cross-toolchain archive accounting accepted no members")

    version_summary = {
        "arm_cc_version": "recorded compiler",
        "arm_ld_version": "recorded linker",
        "bmake_version": "recorded bmake",
    }
    with mock.patch.object(
        composition,
        "command_version",
        side_effect=("recorded compiler", "different linker", "recorded bmake"),
    ):
        exact, observed_cc, observed_ld, observed_bmake = (
            composition.measurement_environment_matches(version_summary)
        )
    if exact or (
        observed_cc,
        observed_ld,
        observed_bmake,
    ) != ("recorded compiler", "different linker", "recorded bmake"):
        raise AssertionError("ARM linker drift retained exact measurement mode")

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
    changed["boxes"][0]["raw_bytes"] = -1
    cases.append(("negative raw byte measurement", changed))

    changed = copy.deepcopy(document)
    changed["boxes"][0]["packed_bytes"] = 999999999
    cases.append(("forged packed byte measurement", changed))

    changed = copy.deepcopy(document)
    changed["boxes"][0]["raw_blocks"] = -10
    cases.append(("negative raw block measurement", changed))

    changed = copy.deepcopy(document)
    changed["root"]["candidate"]["links"] -= 1
    changed["root"]["delta"]["links"] = 0
    cases.append(("candidate manifest-link drift", changed))

    changed = copy.deepcopy(document)
    changed["root"]["candidate"]["data_blocks"] += 1
    changed["root"]["candidate"]["free_blocks"] -= 1
    changed["root"]["delta"]["data_blocks"] += 1
    changed["root"]["delta"]["free_blocks"] -= 1
    cases.append(("positive candidate root growth", changed))

    changed = copy.deepcopy(document)
    changed["clean_distribution_receipts"]["candidate"]["exit_code"] = 1
    cases.append(("failed clean-distribution receipt", changed))

    changed = copy.deepcopy(document)
    changed["clean_distribution_receipts"]["candidate"]["receipt_sha256"] = (
        "0" * 64
    )
    cases.append(("placeholder clean-distribution digest", changed))

    changed = copy.deepcopy(document)
    changed["clean_distribution_receipts"]["candidate"]["receipt_path"] = (
        "docs/research/missing-multicall-receipt.json"
    )
    cases.append(("missing clean-distribution receipt", changed))

    changed = copy.deepcopy(document)
    changed["clean_distribution_receipts"]["candidate"]["receipt_sha256"] = (
        "f" * 64
    )
    cases.append(("clean-distribution digest mismatch", changed))

    changed = copy.deepcopy(document)
    first_source = next(iter(changed["source_sha256"]))
    changed["source_sha256"][first_source] = "0" * 64
    cases.append(("source identity drift", changed))

    changed = copy.deepcopy(document)
    changed["boxes"][0]["aliases"].append("duplicate:cat")
    cases.append(("Makefile member drift", changed))

    failures = makefile_contract_failures(root)
    failures.extend(
        name for name, changed in cases
        if not mutation_is_rejected(root, changed)
    )
    for receipt_name in ("base", "candidate"):
        if "receipt_path" not in document["clean_distribution_receipts"][receipt_name]:
            failures.append(f"missing retained {receipt_name} receipt")
    if "measurement_inventory" not in document:
        failures.append("missing retained size inventory")
    if failures:
        raise AssertionError("unrejected controls: " + ", ".join(failures))

    print(
        "PASS DPUCP-2024 multicall composition calibration: "
        f"{len(cases)} mutations rejected"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
