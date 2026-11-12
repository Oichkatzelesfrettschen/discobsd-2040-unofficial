"""Mutation controls for the DPUCP-2024 requirement profile."""

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import check_dpucp_profile as dpucp
import check_sus_requirement_corpus as sus


def expect_failure(
    name,
    root,
    corpus,
    entities,
    profile,
    rows,
    expected_message=None,
):
    try:
        dpucp.validate_documents(root, corpus, entities, profile, rows)
    except dpucp.ProfileError as error:
        if expected_message is not None and expected_message not in str(error):
            raise AssertionError(
                f"mutation failed at the wrong invariant: {name}: {error}"
            ) from error
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

    expected_first_hardware_release = [
        "shell-command",
        "shell-getopts",
        "utility-printf",
        "utility-readlink",
        "utility-realpath",
        "utility-uudecode",
        "utility-timeout",
    ]
    if profile.get("first_hardware_release") != expected_first_hardware_release:
        raise AssertionError("first hardware release surface differs")
    expected_first_hardware_release_bindings = {
        "shell-command": (
            "dpucp-2024:utility:command",
            "shell-command-host-contract",
        ),
        "shell-getopts": (
            "dpucp-2024:utility:getopts",
            "shell-getopts-host-contract",
        ),
        "utility-printf": (
            "dpucp-2024:utility:printf",
            "utility-printf-native-ilp32-cross-contract",
        ),
        "utility-readlink": (
            "dpucp-2024:utility:readlink",
            "recovery-utilities-native-cross-contract",
        ),
        "utility-realpath": (
            "dpucp-2024:utility:realpath",
            "recovery-utilities-native-cross-contract",
        ),
        "utility-uudecode": (
            "dpucp-2024:utility:uudecode",
            "recovery-utilities-native-cross-contract",
        ),
        "utility-timeout": (
            "dpucp-2024:utility:timeout",
            "timeout-native-cross-link-contract",
        ),
    }
    if dpucp.FIRST_HARDWARE_RELEASE_BINDINGS != expected_first_hardware_release_bindings:
        raise AssertionError("first hardware release bindings differ")

    if profile["schema_version"] != 3:
        raise AssertionError("review accounting and physical limits require schema 3")
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
        "implementation": ["unreviewed", "present", "partial", "absent", "divergent"],
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
    for field in dpucp.physical_limits_from_linker(
        (root / dpucp.LINKER_SCRIPT).read_text()
    ):
        changed_profile = copy.deepcopy(profile)
        changed_profile["hardware_limits"][field] += 1
        expect_failure(
            field, root, corpus, entities, changed_profile, rows,
            f"hardware limit differs from linker: {field}",
        )
        cases.append((f"physical limit {field}", changed_profile, rows))

    changed_profile = copy.deepcopy(profile)
    changed_profile["hardware_limits"]["process_window_bytes"] = True
    cases.append(("Boolean hardware limit", changed_profile, rows))

    for state, evidence, residual, expected in (
        ("unreviewed", "static", "open", "unreviewed row claims evidence"),
        ("unreviewed", "none", "closed", "unreviewed row claims closure"),
        ("absent", "none", "open", "absence claim lacks audit witness"),
    ):
        changed_rows = copy.deepcopy(rows)
        changed_rows[0].update(implementation=state, evidence=evidence, residual=residual)
        expect_failure(expected, root, corpus, entities, profile, changed_rows, expected)
        cases.append((expected, profile, changed_rows))

    default = dpucp.default_requirement(entities[0], {})
    if default["implementation"] != "unreviewed" or default["evidence"] != "none":
        raise AssertionError("unmapped source implies an implementation verdict")

    audited_profile = copy.deepcopy(profile)
    audited_rows = copy.deepcopy(rows)
    audited_rows[0].update(
        implementation="absent", evidence="static", witness="fixture-absence-audit"
    )
    audited_profile["witnesses"]["fixture-absence-audit"] = {
        "command": "synthetic audited-absence acceptance fixture",
        "evidence": "static",
        "known_good": "tools/check_dpucp_profile_test.py",
        "known_bad": "tools/check_dpucp_profile.py",
    }
    audited_profile["requirements"]["ordered_row_sha256"] = dpucp.ordered_sha256(audited_rows)
    audited_profile["state_counts"] = dpucp.count_states(audited_rows)
    dpucp.validate_documents(root, corpus, entities, audited_profile, audited_rows)
    if dpucp.derive_requirements(corpus, entities, audited_rows) != audited_rows:
        raise AssertionError("refresh discards audited absence")

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

    first_release_cases = []

    changed_profile = copy.deepcopy(profile)
    changed_profile["first_hardware_release"] = expected_first_hardware_release[:-1]
    first_release_cases.append(
        (
            "first hardware release missing member",
            changed_profile,
            rows,
            "first hardware release surface drift",
        )
    )

    changed_profile = copy.deepcopy(profile)
    changed_profile["first_hardware_release"][0:2] = reversed(
        changed_profile["first_hardware_release"][0:2]
    )
    first_release_cases.append(
        (
            "first hardware release reordered members",
            changed_profile,
            rows,
            "first hardware release surface drift",
        )
    )

    changed_profile = copy.deepcopy(profile)
    changed_profile["first_hardware_release"][-1] = "utility-uudecode"
    first_release_cases.append(
        (
            "first hardware release duplicate substitution",
            changed_profile,
            rows,
            "first hardware release surface drift",
        )
    )

    changed_profile = copy.deepcopy(profile)
    changed_profile["first_hardware_release"][-1] = "utility-unknown"
    first_release_cases.append(
        (
            "first hardware release unknown member",
            changed_profile,
            rows,
            "first hardware release surface drift",
        )
    )

    changed_rows = copy.deepcopy(rows)
    readlink_row = next(
        row
        for row in changed_rows
        if row["requirement_id"] == "dpucp-2024:utility:readlink"
    )
    readlink_row["implementation"] = "absent"
    first_release_cases.append(
        (
            "first hardware release absent implementation",
            profile,
            changed_rows,
            "first release requirement is not implemented: utility-readlink",
        )
    )

    changed_rows = copy.deepcopy(rows)
    readlink_row = next(
        row
        for row in changed_rows
        if row["requirement_id"] == "dpucp-2024:utility:readlink"
    )
    readlink_row["evidence"] = "none"
    readlink_row["witness"] = ""
    first_release_cases.append(
        (
            "first hardware release missing evidence",
            profile,
            changed_rows,
            "first release requirement lacks evidence: utility-readlink",
        )
    )

    changed_rows = copy.deepcopy(rows)
    readlink_row = next(
        row
        for row in changed_rows
        if row["requirement_id"] == "dpucp-2024:utility:readlink"
    )
    readlink_row["witness"] = "getpgrp-host-contract"
    first_release_cases.append(
        (
            "first hardware release witness substitution",
            profile,
            changed_rows,
            "first release witness differs: utility-readlink",
        )
    )

    changed_profile = copy.deepcopy(profile)
    changed_profile["witnesses"]["recovery-utilities-native-cross-contract"][
        "command"
    ] = ":"
    first_release_cases.append(
        (
            "first hardware release witness command substitution",
            changed_profile,
            rows,
            "first release witness identity differs: recovery-utilities-native-cross-contract",
        )
    )

    changed_profile = copy.deepcopy(profile)
    recovery_witness = changed_profile["witnesses"][
        "recovery-utilities-native-cross-contract"
    ]
    recovery_witness["known_bad"] = recovery_witness["known_good"]
    first_release_cases.append(
        (
            "first hardware release calibration substitution",
            changed_profile,
            rows,
            "first release witness identity differs: recovery-utilities-native-cross-contract",
        )
    )

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

    for name, mutated_profile, mutated_rows, expected_message in first_release_cases:
        expect_failure(
            name,
            root,
            corpus,
            entities,
            mutated_profile,
            mutated_rows,
            expected_message,
        )

    for name, mutated_profile, mutated_rows in cases:
        expect_failure(name, root, corpus, entities, mutated_profile, mutated_rows)

    physical_controls = check_physical_sources(root, profile)
    check_failed_capacity_refresh(root, profile, rows)
    print(
        "PASS DPUCP-2024 calibration: "
        f"{len(cases) + len(first_release_cases) + 2 + physical_controls} mutations rejected; "
        "audited absence accepted and failed-capacity refresh preserves its inputs"
    )
    return 0


def check_physical_sources(root, profile):
    script = (root / dpucp.LINKER_SCRIPT).read_text()
    expected = profile["hardware_limits"]
    regions = dpucp.physical_limits_from_linker(script)
    if any(expected[field] != value for field, value in regions.items()):
        raise AssertionError("valid physical layout rejected")
    region_line = next(line for line in script.splitlines() if "USERRAM (rwx)" in line)
    mutations = (
        script.replace(region_line, ""),
        script + "\n" + region_line + "\n",
        script.replace("LENGTH = 192K", "LENGTH = 193K"),
        script.replace("LENGTH = 192K", "LENGTH = 191K"),
        script.replace("LENGTH = 192K", "LENGTH = unknown"),
        script.replace("LENGTH = 192K", "LENGTH = 0"),
    )
    for mutated in mutations:
        if mutated == script:
            raise AssertionError("physical source mutation did not change input")
        try:
            dpucp.physical_limits_from_linker(mutated)
        except dpucp.ProfileError:
            pass
        else:
            raise AssertionError("invalid physical source accepted")

    # Shift the user/kernel boundary together to distinguish source derivation
    # from a checker that embeds a second copy of the production window size.
    alternate = script.replace("LENGTH = 192K", "LENGTH = 191K").replace(
        "ORIGIN = 0x20030000, LENGTH = 58K", "ORIGIN = 0x2002FC00, LENGTH = 59K"
    )
    alternate_limits = dpucp.physical_limits_from_linker(alternate)
    if alternate_limits["process_window_bytes"] != 191 * 1024:
        raise AssertionError("physical limits do not follow the source")
    if alternate_limits["sram_bytes"] != expected["sram_bytes"]:
        raise AssertionError("contiguous SRAM budget changed")

    report = "flashimg: region 1536 kbytes, capacity 989 sectors of 1024 bytes, 989 kbytes\n"
    dpucp.validate_capacity_report(profile, report)
    capacity_mutations = (
        report.replace("1536", "1535"),
        report.replace("989 sectors", "988 sectors"),
        report.replace("1024 bytes", "512 bytes"),
        report.replace("989", "988"),
        report + report,
        "",
    )
    for mutated in capacity_mutations:
        try:
            dpucp.validate_capacity_report(profile, mutated)
        except dpucp.ProfileError:
            pass
        else:
            raise AssertionError("invalid capacity report accepted")
    return len(mutations) + len(capacity_mutations)


def check_failed_capacity_refresh(root, profile, rows):
    with tempfile.TemporaryDirectory(prefix="discobsd-profile-refresh-") as directory:
        fixture = Path(directory)
        profile_path = fixture / "profile.json"
        rows_path = fixture / "requirements.tsv"
        # Compact valid JSON makes even an otherwise identical refresh observable.
        before_profile = json.dumps(profile).encode()
        before_rows = dpucp.render_requirements(rows).encode()
        profile_path.write_bytes(before_profile)
        rows_path.write_bytes(before_rows)
        result = subprocess.run(
            [sys.executable, str(root / "tools/check_dpucp_profile.py"),
             "--root", str(root), "--profile", str(profile_path),
             "--requirements", str(rows_path), "--refresh", "--flashimg", sys.executable],
            capture_output=True, text=True, timeout=30,
        )
        if result.returncode == 0 or "flashimg capacity probe failed" not in result.stderr:
            raise AssertionError("failed capacity command was accepted or failed elsewhere")
        if profile_path.read_bytes() != before_profile or rows_path.read_bytes() != before_rows:
            raise AssertionError("failed capacity refresh modified its inputs")


if __name__ == "__main__":
    sys.exit(main())
