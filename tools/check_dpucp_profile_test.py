"""Mutation controls for the DPUCP-2024 requirement profile."""

import copy
import sys
from pathlib import Path

import check_dpucp_profile as dpucp
import check_sus_requirement_corpus as sus


def expect_failure(name, root, corpus, entities, profile, rows):
    try:
        dpucp.validate_documents(root, corpus, entities, profile, rows)
    except dpucp.ProfileError:
        return
    raise AssertionError(f"mutation passed: {name}")


def require_logname_binding(profile, rows):
    logname_row = next(
        (
            row
            for row in rows
            if row["requirement_id"] == "dpucp-2024:utility:logname"
        ),
        None,
    )
    witness_id = "logname-host-multicall-contract"
    if logname_row is None:
        raise AssertionError("missing logname requirement row")
    if (
        logname_row["implementation"] != "partial"
        or logname_row["evidence"] != "host"
        or logname_row["residual"] != "open"
        or logname_row["witness"] != witness_id
    ):
        raise AssertionError("unbound logname compatibility witness")
    witness = profile["witnesses"].get(witness_id)
    if witness is None:
        raise AssertionError("missing logname profile witness")
    if witness["evidence"] != "host":
        raise AssertionError("logname witness evidence is not host")
    for target in ("check-id-aliases", "check-multicall-composition"):
        if target not in witness["command"]:
            raise AssertionError(f"logname witness omits {target}")
    expected_fixtures = {
        "known_good": "tests/id_aliases/check.sh",
        "known_bad": "tests/id_aliases/logname_effective_user_bad.sh",
    }
    for fixture_kind, expected_path in expected_fixtures.items():
        if witness[fixture_kind] != expected_path:
            raise AssertionError(f"logname {fixture_kind} fixture differs")


def main():
    root = Path(__file__).resolve().parents[1]
    corpus = sus.load_json(root / "docs/research/sus-requirement-corpus.json")
    entities = sus.parse_entities(root / "docs/research/sus-requirement-entities.tsv")
    profile = dpucp.load_json(root / "docs/research/dpucp-2024-profile.json")
    rows = dpucp.parse_requirements(root / "docs/research/dpucp-2024-requirements.tsv")
    dpucp.validate_documents(root, corpus, entities, profile, rows)

    if profile["schema_version"] != 2:
        raise AssertionError("later compatibility contracts require schema 2")
    expected_family_fields = {
        "id",
        "title",
        "option_boundary",
        "dependencies",
        "minimum_useful_subset",
        "hardware_feasibility_questions",
        "footprint_estimate",
        "deciding_experiment",
        "source_paths",
    }
    for family in profile["later_families"]:
        if not isinstance(family, dict) or set(family) != expected_family_fields:
            raise AssertionError("later compatibility family lacks its contract fields")
        if "authorities" not in family["option_boundary"]:
            raise AssertionError("later compatibility family lacks authority set")

    expected_shell_witnesses = {
        "shell-command-host-contract",
        "shell-getopts-host-contract",
    }
    missing_shell_witnesses = expected_shell_witnesses - profile["witnesses"].keys()
    if missing_shell_witnesses:
        raise AssertionError(
            "missing shell profile witnesses: "
            + ", ".join(sorted(missing_shell_witnesses))
        )
    shell_rows = {
        row["name"]: row
        for row in rows
        if row["requirement_id"] in {
            "dpucp-2024:utility:command",
            "dpucp-2024:utility:getopts",
        }
    }
    for name, witness_id in (
        ("command", "shell-command-host-contract"),
        ("getopts", "shell-getopts-host-contract"),
    ):
        row = shell_rows[name]
        if (
            row["implementation"] != "partial"
            or row["evidence"] != "host"
            or row["residual"] != "open"
            or row["witness"] != witness_id
        ):
            raise AssertionError(f"unbound shell compatibility witness: {name}")
        witness = profile["witnesses"][witness_id]
        if witness["known_good"] == witness["known_bad"]:
            raise AssertionError(f"uncalibrated shell witness: {name}")

    printf_row = next(
        row
        for row in rows
        if row["requirement_id"] == "dpucp-2024:utility:printf"
    )
    printf_witness_id = "utility-printf-native-ilp32-cross-contract"
    if (
        printf_row["implementation"] != "partial"
        or printf_row["evidence"] != "host"
        or printf_row["residual"] != "open"
        or printf_row["witness"] != printf_witness_id
    ):
        raise AssertionError("unbound printf compatibility witness")
    printf_witness = profile["witnesses"].get(printf_witness_id)
    if printf_witness is None:
        raise AssertionError("missing printf profile witness")
    if printf_witness["evidence"] != "host":
        raise AssertionError("printf witness evidence is not host")
    for target in (
        "check-printf-contracts",
        "check-printf-contracts-ilp32",
        "check-printf-contracts-cross",
    ):
        if target not in printf_witness["command"]:
            raise AssertionError(f"printf witness omits {target}")
    if printf_witness["known_good"] == printf_witness["known_bad"]:
        raise AssertionError("uncalibrated printf witness")

    for witness_id in (
        "alarm-process-control-host-contract",
        "kill-process-control-ilp32-contract",
        "waitpid-process-control-host-contract",
    ):
        witness = profile["witnesses"].get(witness_id)
        if witness is None:
            raise AssertionError(f"missing process-control witness: {witness_id}")
        if witness["known_good"] == witness["known_bad"]:
            raise AssertionError(f"uncalibrated process-control witness: {witness_id}")

    timeout_row = next(
        row
        for row in rows
        if row["requirement_id"] == "dpucp-2024:utility:timeout"
    )
    timeout_witness_id = "timeout-native-cross-link-contract"
    if (
        timeout_row["implementation"] != "partial"
        or timeout_row["evidence"] != "host"
        or timeout_row["residual"] != "open"
        or timeout_row["witness"] != timeout_witness_id
    ):
        raise AssertionError("unbound timeout compatibility witness")
    timeout_witness = profile["witnesses"].get(timeout_witness_id)
    if timeout_witness is None:
        raise AssertionError("missing timeout profile witness")
    for target in (
        "check-timeout-contracts",
        "check-timeout-contracts-cross",
        "check-timeout-target-link",
    ):
        if target not in timeout_witness["command"]:
            raise AssertionError(f"timeout witness omits {target}")
    if timeout_witness["known_good"] == timeout_witness["known_bad"]:
        raise AssertionError("uncalibrated timeout witness")

    require_logname_binding(profile, rows)

    changed_profile = copy.deepcopy(profile)
    changed_profile["witnesses"]["logname-host-multicall-contract"][
        "known_bad"
    ] = "tests/id_aliases/identity_stubs.c"
    try:
        require_logname_binding(changed_profile, rows)
    except AssertionError:
        pass
    else:
        raise AssertionError("logname known-bad mutation passed")

    changed_rows = copy.deepcopy(rows)
    logname_row = next(
        row
        for row in changed_rows
        if row["requirement_id"] == "dpucp-2024:utility:logname"
    )
    logname_row["implementation"] = "absent"
    logname_row["evidence"] = "none"
    logname_row["witness"] = ""
    try:
        require_logname_binding(profile, changed_rows)
    except AssertionError:
        pass
    else:
        raise AssertionError("logname compatibility mutation passed")

    expected_state_order = {
        "applicability": [
            "base",
            "selected-option",
            "unselected-standard-option",
            "environment-constrained",
        ],
        "implementation": ["present", "partial", "absent", "divergent"],
        "evidence": ["none", "static", "host", "cross", "qemu-user", "renode", "board"],
        "residual": ["closed", "open", "blocked"],
    }
    state_counts = dpucp.count_states(rows)
    for dimension, expected in expected_state_order.items():
        if list(state_counts[dimension]) != expected:
            raise AssertionError(f"nondeterministic state order for {dimension}")

    derived = dpucp.derive_requirements(corpus, entities, rows)
    if dpucp.render_requirements(derived) != dpucp.render_requirements(rows):
        raise AssertionError("committed requirements differ from deterministic derivation")
    if any(line.endswith("\t") for line in dpucp.render_requirements(rows).splitlines()):
        raise AssertionError("rendered requirement row ends in trailing whitespace")

    reviewed = copy.deepcopy(rows)
    reviewed[0]["option_owner"] = "none-known"
    reviewed[0]["applicability"] = "environment-constrained"
    reviewed[0]["implementation"] = "partial"
    reviewed[0]["evidence"] = "static"
    reviewed[0]["next_action"] = "Run the deciding environment experiment."
    preserved = dpucp.derive_requirements(corpus, entities, reviewed)
    for column in dpucp.REQUIREMENT_COLUMNS[5:]:
        if preserved[0][column] != reviewed[0][column]:
            raise AssertionError(f"refresh discarded reviewed {column}")

    cases = []
    changed_rows = copy.deepcopy(rows)
    changed_rows.pop()
    cases.append(("missing Issue 8 row", profile, changed_rows))

    changed_rows = copy.deepcopy(rows)
    changed_rows.append(copy.deepcopy(changed_rows[-1]))
    cases.append(("duplicate requirement", profile, changed_rows))

    issue7_only = next(entity for entity in entities if entity.edition_relation == "issue7-only")
    changed_rows = copy.deepcopy(rows)
    changed_rows.append(dpucp.default_requirement(issue7_only, {}))
    cases.append(("Issue 7-only row", profile, changed_rows))

    changed_rows = copy.deepcopy(rows)
    changed_rows[0]["applicability"] = "unknown"
    cases.append(("unknown applicability", profile, changed_rows))

    changed_rows = copy.deepcopy(rows)
    changed_rows[0]["residual"] = "closed"
    changed_rows[0]["evidence"] = "host"
    changed_rows[0]["next_action"] = ""
    cases.append(("closed row without witness", profile, changed_rows))

    changed_rows = copy.deepcopy(rows)
    changed_rows[0]["next_action"] = ""
    cases.append(("open row without deciding action", profile, changed_rows))

    changed_rows = copy.deepcopy(rows)
    changed_rows[0]["applicability"] = "unselected-standard-option"
    changed_rows[0]["option_owner"] = "none-known"
    cases.append(("unselected option without owner", profile, changed_rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["denominator"]["issue8_entities"] += 1
    cases.append(("Issue 8 count drift", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["requirements"]["ordered_row_sha256"] = "0" * 64
    cases.append(("requirement hash drift", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["claims"]["posix_conformance"] = True
    cases.append(("POSIX conformance claim", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["profile"]["version"] = "2024.1"
    cases.append(("profile version drift", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["later_families"] = []
    cases.append(("missing later compatibility families", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    del changed_profile["later_families"][0]["dependencies"]
    cases.append(("missing later-family contract field", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["later_families"][0]["option_boundary"]["kind"] = "unknown"
    cases.append(("unknown later-family option boundary", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["later_families"][0]["option_boundary"]["authorities"] = [
        "https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/regex.h.html"
    ]
    cases.append(("later-family option authority drift", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["later_families"][3]["option_boundary"]["owners"] = [
        "CX -- selectable locale objects"
    ]
    cases.append(("later-family option owner drift", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["later_families"][0]["dependencies"] = []
    cases.append(("empty later-family dependency closure", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["later_families"][0]["source_paths"] = [
        "docs/research/missing-family-source.md"
    ]
    cases.append(("missing later-family source", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    del changed_profile["later_families"][0]["footprint_estimate"]["surfaces"][
        "runtime_memory"
    ]
    cases.append(("incomplete later-family footprint", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["later_families"][0]["deciding_experiment"] = ""
    cases.append(("missing later-family deciding experiment", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["witnesses"]["fixture-static"] = {
        "command": "fixture-command",
        "evidence": "static",
        "known_good": "tools/check_dpucp_profile_test.py",
        "known_bad": "tools/check_dpucp_profile_test.py",
    }
    changed_rows = copy.deepcopy(rows)
    changed_rows[0]["implementation"] = "present"
    changed_rows[0]["evidence"] = "board"
    changed_rows[0]["residual"] = "closed"
    changed_rows[0]["witness"] = "fixture-static"
    changed_rows[0]["next_action"] = ""
    changed_profile["requirements"]["ordered_row_sha256"] = dpucp.ordered_sha256(changed_rows)
    changed_profile["state_counts"] = dpucp.count_states(changed_rows)
    cases.append(("witness evidence mismatch", changed_profile, changed_rows))

    for name, mutated_profile, mutated_rows in cases:
        expect_failure(name, root, corpus, entities, mutated_profile, mutated_rows)

    print(
        "PASS DPUCP-2024 calibration: "
        f"{len(cases) + 2} mutations rejected"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
