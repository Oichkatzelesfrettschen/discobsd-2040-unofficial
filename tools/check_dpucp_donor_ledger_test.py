"""Mutation controls for the bounded DPUCP donor ledger."""

import copy
import json
import sys
from pathlib import Path

import check_dpucp_donor_ledger as donors


def expect_failure(name, root, document, requirement_ids):
    try:
        donors.validate_document(root, document, requirement_ids)
    except donors.DonorError:
        return None
    return name


def require_documented_mutation_count(testing_text, mutation_count):
    gate_marker = "| `check-dpucp-donor-ledger` |"
    gate_line = next(
        (line for line in testing_text.splitlines() if line.startswith(gate_marker)),
        None,
    )
    if gate_line is None:
        raise AssertionError("missing check-dpucp-donor-ledger documentation")
    expected = f"{mutation_count} mutations reject"
    if expected not in gate_line:
        raise AssertionError(
            "DPUCP donor mutation count documentation differs from calibration: "
            f"expected {mutation_count}"
        )
    for required_phrase in (
        "recovery utility growth exception",
        "six recovery mutations reject",
        "target-native link-map seam",
    ):
        if required_phrase not in gate_line:
            raise AssertionError(
                f"DPUCP donor documentation lacks {required_phrase}"
            )


def main():
    root = Path(__file__).resolve().parents[1]
    for utility in ("readlink", "realpath"):
        makefile_text = (root / "usr.bin" / utility / "Makefile").read_text(
            encoding="utf-8"
        )
        if "LINK_MAP_FLAG=\t-Wl,-Map,${LINK_MAP}" not in makefile_text:
            raise AssertionError(f"{utility} lacks the target-native link-map seam")
        if "${LDFLAGS} ${LINK_MAP_FLAG} -o $@.elf" not in makefile_text:
            raise AssertionError(f"{utility} link does not consume the link-map seam")
    document = donors.load_json(root / "docs/research/dpucp-2024-donor-ledger.json")
    requirement_ids = donors.load_requirement_ids(
        root / "docs/research/dpucp-2024-requirements.tsv"
    )
    donors.validate_document(root, document, requirement_ids)

    measurement_path = root / "docs/research/libc-array-memory-line-reading.json"
    if not measurement_path.is_file():
        raise AssertionError("missing machine-readable Task 4 measurement")
    measurement = json.loads(measurement_path.read_text(encoding="utf-8"))
    validator = getattr(donors, "validate_foundation_measurement", None)
    if validator is None:
        raise AssertionError("missing Task 4 measurement validator")
    validator(measurement)

    shell_measurement_path = (
        root / "docs/research/shell-command-getopts-growth-exception.json"
    )
    if not shell_measurement_path.is_file():
        raise AssertionError("missing machine-readable shell growth exception")
    shell_measurement = json.loads(
        shell_measurement_path.read_text(encoding="utf-8")
    )
    shell_validator = getattr(donors, "validate_shell_measurement", None)
    if shell_validator is None:
        raise AssertionError("missing shell growth-exception validator")
    shell_validator(shell_measurement)

    printf_measurement_path = (
        root / "docs/research/utility-printf-growth-exception.json"
    )
    if not printf_measurement_path.is_file():
        raise AssertionError("missing machine-readable printf growth exception")
    printf_measurement = json.loads(
        printf_measurement_path.read_text(encoding="utf-8")
    )
    printf_validator = getattr(donors, "validate_printf_measurement", None)
    if printf_validator is None:
        raise AssertionError("missing printf growth-exception validator")
    printf_validator(printf_measurement, root)

    recovery_measurement_path = (
        root / "docs/research/recovery-path-utilities-growth-exception.json"
    )
    if getattr(donors, "RECOVERY_MEASUREMENT_DATA", None) != str(
        recovery_measurement_path.relative_to(root)
    ):
        raise AssertionError("recovery growth exception is not bound to the verifier")
    if not recovery_measurement_path.is_file():
        raise AssertionError("missing machine-readable recovery growth exception")
    recovery_measurement = json.loads(
        recovery_measurement_path.read_text(encoding="utf-8")
    )
    recovery_validator = getattr(donors, "validate_recovery_measurement", None)
    if recovery_validator is None:
        raise AssertionError("missing recovery growth-exception validator")
    recovery_validator(recovery_measurement, root)

    for state in ("base", "candidate"):
        kernel_measurement = shell_measurement["kernel"][state]
        if kernel_measurement.get("section_sum_spare_bytes") != 26226:
            raise AssertionError(f"{state} kernel section-sum spare is absent")
        if kernel_measurement.get("load_alignment_bytes") != 2:
            raise AssertionError(f"{state} kernel load alignment is absent")
        if kernel_measurement.get("loaded_flash_span_bytes") != 104848:
            raise AssertionError(f"{state} kernel loaded span is absent")
        if kernel_measurement.get("load_tail_margin_bytes") != 26224:
            raise AssertionError(f"{state} kernel load-tail margin is absent")

    changed_measurement = copy.deepcopy(measurement)
    changed_measurement["root"]["candidate"]["data_blocks"] += 1
    try:
        validator(changed_measurement)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("root arithmetic mutation passed")

    changed_measurement = copy.deepcopy(measurement)
    changed_measurement["resources"]["line_reader"]["stack_bytes"] += 4
    try:
        validator(changed_measurement)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("resource-bound mutation passed")

    changed_shell_measurement = copy.deepcopy(shell_measurement)
    changed_shell_measurement["shell"]["candidate"]["text_bytes"] += 4
    try:
        shell_validator(changed_shell_measurement)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("shell delta mutation passed")

    changed_shell_measurement = copy.deepcopy(shell_measurement)
    changed_shell_measurement["root"]["candidate"]["free_blocks"] += 1
    try:
        shell_validator(changed_shell_measurement)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("shell root arithmetic mutation passed")

    changed_shell_measurement = copy.deepcopy(shell_measurement)
    changed_shell_measurement["kernel"]["candidate"]["load_alignment_bytes"] += 1
    try:
        shell_validator(changed_shell_measurement)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("kernel load-span mutation passed")

    changed_shell_measurement = copy.deepcopy(shell_measurement)
    overflow = changed_shell_measurement["shell"]["process_window_bytes"]
    changed_shell_measurement["shell"]["candidate"]["text_bytes"] += overflow
    changed_shell_measurement["shell"]["candidate"][
        "window_occupancy_before_stack_bytes"
    ] += overflow
    changed_shell_measurement["shell"]["candidate"][
        "window_margin_before_stack_bytes"
    ] -= overflow
    changed_shell_measurement["shell"]["delta"]["text_bytes"] += overflow
    changed_shell_measurement["shell"]["delta"][
        "window_occupancy_before_stack_bytes"
    ] += overflow
    try:
        shell_validator(changed_shell_measurement)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("negative shell process-window margin passed")

    changed_printf_measurement = copy.deepcopy(printf_measurement)
    changed_printf_measurement["programs"]["utilbox"]["candidate"][
        "text_bytes"
    ] += 4
    try:
        printf_validator(changed_printf_measurement, root)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("printf utilbox delta mutation passed")

    changed_printf_measurement = copy.deepcopy(printf_measurement)
    changed_printf_measurement["programs"]["standalone_printf"]["base"][
        "raw_bytes"
    ] += 1
    changed_printf_measurement["programs"]["standalone_printf"]["candidate"][
        "raw_bytes"
    ] += 1
    try:
        printf_validator(changed_printf_measurement, root)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("printf a.out raw-size mutation passed")

    changed_printf_measurement = copy.deepcopy(printf_measurement)
    changed_printf_measurement["root"]["candidate"]["free_blocks"] += 1
    try:
        printf_validator(changed_printf_measurement, root)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("printf root arithmetic mutation passed")

    changed_printf_measurement = copy.deepcopy(printf_measurement)
    changed_printf_measurement["composition"]["shipped_form"] = "standalone"
    try:
        printf_validator(changed_printf_measurement, root)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("printf shipped-form mutation passed")

    changed_printf_measurement = copy.deepcopy(printf_measurement)
    changed_printf_measurement["composition"][
        "candidate_standalone_libc_members"
    ] += 1
    try:
        printf_validator(changed_printf_measurement, root)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("printf archive-member count mutation passed")

    changed_recovery_measurement = copy.deepcopy(recovery_measurement)
    changed_recovery_measurement["programs"]["textbox"]["candidate"][
        "text_bytes"
    ] += 4
    try:
        recovery_validator(changed_recovery_measurement, root)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("recovery textbox delta mutation passed")

    changed_recovery_measurement = copy.deepcopy(recovery_measurement)
    changed_recovery_measurement["programs"]["uudecode"]["base"][
        "raw_bytes"
    ] += 1
    changed_recovery_measurement["programs"]["uudecode"]["candidate"][
        "raw_bytes"
    ] += 1
    try:
        recovery_validator(changed_recovery_measurement, root)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("recovery a.out raw-size mutation passed")

    changed_recovery_measurement = copy.deepcopy(recovery_measurement)
    changed_recovery_measurement["root"]["candidate"]["free_blocks"] += 1
    try:
        recovery_validator(changed_recovery_measurement, root)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("recovery root arithmetic mutation passed")

    changed_recovery_measurement = copy.deepcopy(recovery_measurement)
    changed_recovery_measurement["composition"]["shipped_form"] = "standalone"
    try:
        recovery_validator(changed_recovery_measurement, root)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("recovery shipped-form mutation passed")

    changed_recovery_measurement = copy.deepcopy(recovery_measurement)
    changed_recovery_measurement["composition"][
        "candidate_textbox_libc_members"
    ] += 1
    try:
        recovery_validator(changed_recovery_measurement, root)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("recovery archive-member count mutation passed")

    changed_recovery_measurement = copy.deepcopy(recovery_measurement)
    changed_recovery_measurement["resources"]["realpath_stack"][
        "classification"
    ] = "dynamic"
    try:
        recovery_validator(changed_recovery_measurement, root)
    except donors.DonorError:
        pass
    else:
        raise AssertionError("recovery dynamic stack classification passed")

    promoted_ids = {
        "libc-reallocarray",
        "libc-memmem",
        "libc-getdelim-getline",
        "shell-getopts",
        "shell-command",
        "utility-printf",
        "utility-readlink",
        "utility-realpath",
        "utility-uudecode",
    }
    for candidate in document["candidates"]:
        if candidate["id"] in promoted_ids:
            if candidate["disposition"] != "promoted":
                raise AssertionError(f"unpromoted Task 4 donor: {candidate['id']}")
            if candidate["size_estimate"]["status"] != "measured":
                raise AssertionError(f"unmeasured Task 4 donor: {candidate['id']}")

    cases = []
    changed = copy.deepcopy(document)
    changed["candidates"].append(copy.deepcopy(changed["candidates"][0]))
    cases.append(("duplicate candidate", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["requirements"] = ["dpucp-2024:utility:absent"]
    cases.append(("unknown DPUCP requirement", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["sources"][0]["path"] = "../escape.c"
    cases.append(("unsafe donor path", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["sources"][0]["repository"] = "missing-src"
    cases.append(("unknown source repository", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["sources"][0]["sha256"] = "short"
    cases.append(("bad donor hash", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["dependencies"] = []
    cases.append(("missing dependency closure", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["size_estimate"]["evidence"] = ""
    cases.append(("measured size without evidence", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["deciding_gate"] = ""
    cases.append(("candidate without deciding gate", changed))

    changed = copy.deepcopy(document)
    changed["repositories"]["netbsd-src"]["revision"] = "0" * 40
    cases.append(("repository revision drift", changed))

    changed = copy.deepcopy(document)
    del changed["candidates"][0]
    cases.append(("missing bounded candidate", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["id"] = "libc-invented"
    cases.append(("unknown bounded candidate", changed))

    changed = copy.deepcopy(document)
    changed["repositories"]["netbsd-src"]["origin"] = "https://example.invalid/src.git"
    cases.append(("repository origin drift", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["sources"][0]["sha256"] = "z" * 64
    cases.append(("nonhex donor hash", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["invariants"][0] = ""
    cases.append(("blank invariant", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["archive_members"] = []
    cases.append(("missing archive expectation", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["requirements"] = [
        "dpucp-2024:system-interface:getline"
    ]
    cases.append(("candidate requirement drift", changed))

    changed = copy.deepcopy(document)
    changed["claim"] = "DPUCP-2024 is POSIX conformant."
    cases.append(("conformance claim", changed))

    changed = copy.deepcopy(document)
    nl_candidate = next(
        candidate
        for candidate in changed["candidates"]
        if candidate["id"] == "utility-nl-bre"
    )
    nl_candidate["disposition"] = "candidate"
    cases.append(("premature BRE admission", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["size_estimate"]["evidence"] = "AGENTS.md"
    cases.append(("fabricated measured size", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["size_estimate"]["status"] = "estimated"
    cases.append(("unknown measured state", changed))

    changed = copy.deepcopy(document)
    shell_candidate = next(
        candidate
        for candidate in changed["candidates"]
        if candidate["id"] == "shell-command"
    )
    shell_candidate["disposition"] = "promoted"
    shell_candidate["size_estimate"]["status"] = "measured"
    shell_candidate["size_estimate"]["evidence"] = (
        "docs/research/libc-array-memory-line-reading.md"
    )
    cases.append(("shell promotion with unrelated size evidence", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["size_estimate"]["surfaces"] = [""]
    cases.append(("blank size surface", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["sources"][0]["license"] = "UNKNOWN"
    cases.append(("unknown donor license", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["c17_rewrite"] = True
    cases.append(("nontext C17 rewrite", changed))

    testing_path = root / "sys/arch/rp2040/doc/TESTING.md"
    testing_text = testing_path.read_text(encoding="utf-8")
    require_documented_mutation_count(testing_text, len(cases))
    known_bad_documentation = testing_text.replace(
        f"{len(cases)} mutations reject",
        f"{len(cases) + 1} mutations reject",
    )
    try:
        require_documented_mutation_count(known_bad_documentation, len(cases))
    except AssertionError:
        pass
    else:
        raise AssertionError("DPUCP donor mutation-count drift passed")

    passed_mutations = []
    for name, mutated in cases:
        passed = expect_failure(name, root, mutated, requirement_ids)
        if passed is not None:
            passed_mutations.append(passed)

    if passed_mutations:
        raise AssertionError("mutations passed: " + ", ".join(passed_mutations))

    print(f"PASS DPUCP donor calibration: {len(cases)} mutations rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
