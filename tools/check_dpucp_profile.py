"""Validate and regenerate the DPUCP-2024 requirement profile."""

from __future__ import annotations

import argparse
import copy
import csv
import hashlib
import io
import json
from pathlib import Path, PurePosixPath

import check_sus_requirement_corpus as sus

REQUIREMENT_COLUMNS = (
    "requirement_id",
    "kind",
    "name",
    "issue8_citation",
    "family",
    "option_owner",
    "applicability",
    "implementation",
    "evidence",
    "residual",
    "witness",
    "next_action",
)
FAMILIES = {
    "base-definition": "base-definitions",
    "system-interface": "system-interfaces",
    "utility": "shell-and-utilities",
}
APPLICABILITY_STATES = (
    "base",
    "selected-option",
    "unselected-standard-option",
    "environment-constrained",
)
IMPLEMENTATION_STATES = ("present", "partial", "absent", "divergent")
EVIDENCE_STATES = ("none", "static", "host", "cross", "qemu-user", "renode", "board")
RESIDUAL_STATES = ("closed", "open", "blocked")
UNOWNED_OPTIONS = {"none-known", "pending-source-analysis"}
FIRST_HARDWARE_RELEASE = (
    "shell-command",
    "shell-getopts",
    "utility-printf",
    "utility-readlink",
    "utility-realpath",
    "utility-uudecode",
    "utility-timeout",
)
FIRST_HARDWARE_RELEASE_BINDINGS = {
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
FIRST_HARDWARE_RELEASE_WITNESSES = {
    "shell-command-host-contract": {
        "command": (
            ': "${PYTHON:?set PYTHON to the intended interpreter}"; '
            "export PYTHON; bmake MACHINE=rp2040 check-posix-sh"
        ),
        "evidence": "host",
        "known_good": "bin/sh/tests/posix-sh.sh",
        "known_bad": "bin/sh/tests/fixtures/command-null-builtin.json",
    },
    "shell-getopts-host-contract": {
        "command": (
            ': "${PYTHON:?set PYTHON to the intended interpreter}"; '
            "export PYTHON; bmake MACHINE=rp2040 check-posix-sh"
        ),
        "evidence": "host",
        "known_good": "bin/sh/tests/posix-sh.sh",
        "known_bad": "bin/sh/tests/fixtures/getopts-optind-skip.json",
    },
    "utility-printf-native-ilp32-cross-contract": {
        "command": (
            ': "${PYTHON:?set PYTHON to the intended interpreter}"; '
            "export PYTHON; bmake MACHINE=rp2040 check-printf-contracts "
            "check-printf-contracts-ilp32 check-printf-contracts-cross"
        ),
        "evidence": "host",
        "known_good": "tests/printf_contracts/behavior_test.py",
        "known_bad": "tests/printf_contracts/known_bad.sh",
    },
    "recovery-utilities-native-cross-contract": {
        "command": (
            ': "${PYTHON:?set PYTHON to the intended interpreter}"; '
            "export PYTHON; bmake MACHINE=rp2040 "
            "check-recovery-utility-contracts "
            "check-recovery-utility-contracts-cross"
        ),
        "evidence": "host",
        "known_good": "tests/recovery_utility_contracts/check.py",
        "known_bad": "tests/recovery_utility_contracts/known_bad.c",
    },
    "timeout-native-cross-link-contract": {
        "command": (
            ': "${PYTHON:?set PYTHON to the intended interpreter}"; '
            "export PYTHON; bmake MACHINE=rp2040 check-timeout-contracts "
            "check-timeout-contracts-cross check-timeout-target-link"
        ),
        "evidence": "host",
        "known_good": "tests/timeout_contracts/behavior_test.py",
        "known_bad": "tests/timeout_contracts/known_bad.c",
    },
}
LATER_FAMILY_IDS = (
    "on-device-c",
    "regex-and-text-processing",
    "networking",
    "locale",
    "threads",
    "realtime",
    "dynamic-linking",
)
LATER_FAMILY_BOUNDARIES = {
    "on-device-c": (
        "standard-option",
        (
            "https://pubs.opengroup.org/onlinepubs/9799919799/utilities/c17.html",
        ),
    ),
    "regex-and-text-processing": (
        "mixed",
        (
            "https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/regex.h.html",
            "https://pubs.opengroup.org/onlinepubs/9799919799/utilities/nl.html",
        ),
    ),
    "networking": (
        "mixed",
        (
            "https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_socket.h.html",
            "https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/unistd.h.html",
        ),
    ),
    "locale": (
        "mixed",
        (
            "https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/locale.h.html",
            "https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/unistd.h.html",
            "https://pubs.opengroup.org/onlinepubs/9799919799/utilities/localedef.html",
        ),
    ),
    "threads": (
        "mixed",
        (
            "https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/pthread.h.html",
            "https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/unistd.h.html",
        ),
    ),
    "realtime": (
        "mixed",
        (
            "https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/unistd.h.html",
        ),
    ),
    "dynamic-linking": (
        "mixed",
        (
            "https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/dlfcn.h.html",
            "https://pubs.opengroup.org/onlinepubs/9799919799/utilities/c17.html",
        ),
    ),
}
LATER_FAMILY_OWNERS = {
    "on-device-c": (
        "CD -- C-Language Development Utilities (_POSIX2_C_DEV)",
    ),
    "regex-and-text-processing": (
        "Base -- Regular Expression Handling (_POSIX_REGEXP)",
        "XSI -- utility-specific extensions including nl p<BRE>",
    ),
    "networking": (
        "Base -- socket interfaces",
        "IP6 -- IPv6 (_POSIX_IPV6)",
        "RS -- raw sockets (_POSIX_RAW_SOCKETS)",
    ),
    "locale": (
        "Base -- ISO C locale interfaces and mandatory CX extensions, including locale objects",
        "_POSIX2_LOCALEDEF -- locale creation capability; XSI requires support",
    ),
    "threads": (
        "Base -- Threads (_POSIX_THREADS)",
        "TPS -- Thread Execution Scheduling",
        "TPI and TPP -- non-robust mutex priority inheritance and protection",
        "RPI and RPP -- robust mutex priority inheritance and protection",
        "TSH -- process-shared thread synchronization",
        "TSS and TSA -- thread stack attributes",
        "TCT and TSP -- thread CPU-time clocks and sporadic-server scheduling",
    ),
    "realtime": (
        "Base -- realtime signals, timers, semaphores, asynchronous I/O, and memory protection",
        "CPT -- Process CPU-Time Clocks",
        "PS -- Process Scheduling",
        "PIO -- Prioritized Input and Output",
        "MSG -- Message Passing",
        "SHM -- Shared Memory Objects",
        "ML and MLR -- Memory Locking",
        "TYM -- Typed Memory Objects",
    ),
    "dynamic-linking": (
        "Base -- dynamic linking interfaces in dlfcn.h",
        "CD -- c17 shared-library production and dynamic-link selection",
    ),
}
LATER_FAMILY_FIELDS = {
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
OPTION_BOUNDARY_FIELDS = {"kind", "owners", "base_residual", "authorities"}
OPTION_BOUNDARY_KINDS = {"base", "standard-option", "mixed"}
FOOTPRINT_FIELDS = {"class", "confidence", "surfaces", "basis"}
FOOTPRINT_CLASSES = {"low", "medium", "high", "system"}
FOOTPRINT_SURFACES = {"kernel", "process", "packed_root", "runtime_memory"}


class ProfileError(RuntimeError):
    """Report a DPUCP schema, denominator, or evidence contradiction."""


def require(condition, message):
    if not condition:
        raise ProfileError(message)


def valid_relative_path(value):
    path = PurePosixPath(value)
    return bool(value) and not path.is_absolute() and ".." not in path.parts


def require_string_list(value, label):
    require(isinstance(value, list) and value, f"{label} is absent")
    require(
        all(isinstance(item, str) and item.strip() for item in value),
        f"{label} contains an empty or non-string value",
    )
    require(len(value) == len(set(value)), f"{label} contains duplicate values")


def validate_later_families(root, profile):
    families = profile.get("later_families")
    require(isinstance(families, list), "later compatibility families are absent")
    require(
        [family.get("id") if isinstance(family, dict) else None for family in families]
        == list(LATER_FAMILY_IDS),
        "later compatibility family set or order drift",
    )
    for family in families:
        family_id = family["id"]
        require(set(family) == LATER_FAMILY_FIELDS, f"family fields drift: {family_id}")
        require(
            isinstance(family["title"], str) and family["title"].strip(),
            f"family title is absent: {family_id}",
        )

        boundary = family["option_boundary"]
        require(isinstance(boundary, dict), f"option boundary is absent: {family_id}")
        require(
            set(boundary) == OPTION_BOUNDARY_FIELDS,
            f"option boundary fields drift: {family_id}",
        )
        require(
            boundary["kind"] in OPTION_BOUNDARY_KINDS,
            f"option boundary kind is unknown: {family_id}",
        )
        expected_kind, expected_authorities = LATER_FAMILY_BOUNDARIES[family_id]
        require(
            boundary["kind"] == expected_kind,
            f"option boundary kind drift: {family_id}",
        )
        require_string_list(boundary["owners"], f"option owners for {family_id}")
        require(
            boundary["owners"] == list(LATER_FAMILY_OWNERS[family_id]),
            f"option owners drift: {family_id}",
        )
        require(
            isinstance(boundary["base_residual"], str)
            and boundary["base_residual"].strip(),
            f"Base residual is absent: {family_id}",
        )
        require_string_list(
            boundary["authorities"], f"Issue 8 option authorities for {family_id}"
        )
        require(
            boundary["authorities"] == list(expected_authorities),
            f"Issue 8 option authorities drift: {family_id}",
        )

        for field in (
            "dependencies",
            "minimum_useful_subset",
            "hardware_feasibility_questions",
        ):
            require_string_list(family[field], f"{field} for {family_id}")

        estimate = family["footprint_estimate"]
        require(isinstance(estimate, dict), f"footprint estimate is absent: {family_id}")
        require(
            set(estimate) == FOOTPRINT_FIELDS,
            f"footprint estimate fields drift: {family_id}",
        )
        require(
            estimate["class"] in FOOTPRINT_CLASSES,
            f"footprint class is unknown: {family_id}",
        )
        require(
            estimate["confidence"] == "screening",
            f"footprint confidence is unsupported: {family_id}",
        )
        surfaces = estimate["surfaces"]
        require(
            isinstance(surfaces, dict) and set(surfaces) == FOOTPRINT_SURFACES,
            f"footprint surfaces drift: {family_id}",
        )
        require(
            all(isinstance(value, str) and value.strip() for value in surfaces.values()),
            f"footprint surface estimate is absent: {family_id}",
        )
        require(
            isinstance(estimate["basis"], str) and estimate["basis"].strip(),
            f"footprint basis is absent: {family_id}",
        )

        require(
            isinstance(family["deciding_experiment"], str)
            and family["deciding_experiment"].strip(),
            f"deciding experiment is absent: {family_id}",
        )
        require_string_list(family["source_paths"], f"source paths for {family_id}")
        for source_path in family["source_paths"]:
            require(valid_relative_path(source_path), f"unsafe family source path: {family_id}")
            require((root / source_path).is_file(), f"missing family source path: {family_id}")


def load_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ProfileError(f"cannot read {path}: {error}") from error


def render_requirements(rows):
    output = io.StringIO()
    writer = csv.DictWriter(
        output,
        fieldnames=REQUIREMENT_COLUMNS,
        delimiter="\t",
        lineterminator="\n",
    )
    writer.writeheader()
    writer.writerows(rows)
    return output.getvalue().replace("\t\n", '\t""\n')


def parse_requirements(path):
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise ProfileError(f"cannot read {path}: {error}") from error
    reader = csv.DictReader(io.StringIO(text), delimiter="\t")
    require(
        tuple(reader.fieldnames or ()) == REQUIREMENT_COLUMNS,
        "DPUCP requirement columns drift",
    )
    rows = []
    for line_number, row in enumerate(reader, 2):
        require(None not in row and None not in row.values(), f"malformed row {line_number}")
        rows.append(dict(row))
    return rows


def ordered_sha256(rows):
    return hashlib.sha256(render_requirements(rows).encode("utf-8")).hexdigest()


def mapped_entities(corpus):
    mapped = {}
    for mapping in corpus.get("tiny_system_mappings", []):
        state = mapping.get("state")
        for entity_id in mapping.get("entities", []):
            previous = mapped.get(entity_id)
            if previous is None or state == "bounded":
                mapped[entity_id] = state
    return mapped


def default_requirement(entity, mapped):
    entity_id = f"{entity.kind}:{entity.name}"
    mapping_state = mapped.get(entity_id)
    implementation = "partial" if mapping_state in {"bounded", "partial"} else "absent"
    evidence = "static" if mapping_state in {"bounded", "partial"} else "none"
    return {
        "requirement_id": f"dpucp-2024:{entity_id}",
        "kind": entity.kind,
        "name": entity.name,
        "issue8_citation": entity.issue8_source,
        "family": FAMILIES[entity.kind],
        "option_owner": "pending-source-analysis",
        "applicability": "base",
        "implementation": implementation,
        "evidence": evidence,
        "residual": "open",
        "witness": "",
        "next_action": (
            f"Classify Issue 8 facets and add the lowest sufficient executable witness for "
            f"{entity_id}."
        ),
    }


def derive_requirements(corpus, entities, previous_rows=None):
    mapped = mapped_entities(corpus)
    previous = {
        row["requirement_id"]: row for row in (previous_rows or [])
    }
    state_columns = REQUIREMENT_COLUMNS[5:]
    rows = []
    for entity in entities:
        if not entity.issue8_source:
            continue
        row = default_requirement(entity, mapped)
        prior = previous.get(row["requirement_id"])
        if prior is not None:
            for column in state_columns:
                row[column] = prior[column]
        rows.append(row)
    return rows


def count_states(rows):
    dimensions = {
        "applicability": APPLICABILITY_STATES,
        "implementation": IMPLEMENTATION_STATES,
        "evidence": EVIDENCE_STATES,
        "residual": RESIDUAL_STATES,
    }
    return {
        dimension: {state: sum(row[dimension] == state for row in rows) for state in states}
        for dimension, states in dimensions.items()
    }


def validate_witnesses(root, profile, row):
    witness_ids = [item for item in row["witness"].split(";") if item]
    witnesses = profile.get("witnesses")
    require(isinstance(witnesses, dict), "profile witness catalog is absent")
    for witness_id in witness_ids:
        witness = witnesses.get(witness_id)
        require(isinstance(witness, dict), f"unknown witness {witness_id}")
        require(bool(witness.get("command")), f"witness lacks command: {witness_id}")
        require(
            witness.get("evidence") in EVIDENCE_STATES
            and witness.get("evidence") != "none",
            f"bad witness evidence: {witness_id}",
        )
        for calibration in ("known_good", "known_bad"):
            calibration_path = witness.get(calibration)
            require(
                isinstance(calibration_path, str) and valid_relative_path(calibration_path),
                f"unsafe {calibration} path for witness {witness_id}",
            )
            require(
                (root / calibration_path).is_file(),
                f"missing {calibration} path for witness {witness_id}",
            )
    return witness_ids


def validate_first_hardware_release(profile, rows_by_id):
    release = profile.get("first_hardware_release")
    require(
        release == list(FIRST_HARDWARE_RELEASE),
        "first hardware release surface drift",
    )
    witnesses = profile.get("witnesses")
    require(isinstance(witnesses, dict), "profile witness catalog is absent")
    for release_id in FIRST_HARDWARE_RELEASE:
        requirement_id, witness_id = FIRST_HARDWARE_RELEASE_BINDINGS[release_id]
        row = rows_by_id.get(requirement_id)
        require(row is not None, f"first release requirement is absent: {release_id}")
        require(
            row["implementation"] in {"present", "partial"},
            f"first release requirement is not implemented: {release_id}",
        )
        require(
            row["evidence"] != "none",
            f"first release requirement lacks evidence: {release_id}",
        )
        require(
            witness_id in row["witness"].split(";"),
            f"first release witness differs: {release_id}",
        )
        require(
            witnesses.get(witness_id) == FIRST_HARDWARE_RELEASE_WITNESSES[witness_id],
            f"first release witness identity differs: {witness_id}",
        )


def validate_documents(root, corpus, entities, profile, rows):
    require(profile.get("schema_version") == 2, "unsupported DPUCP schema")
    identity = profile.get("profile")
    require(isinstance(identity, dict), "profile identity is absent")
    require(identity.get("id") == "DPUCP-2024", "profile id drift")
    require(
        identity.get("name") == "DiscoBSD/Pico Unix Compatibility Profile 2024",
        "profile name drift",
    )
    require(identity.get("version") == "2024.2", "profile version drift")

    claims = profile.get("claims")
    require(isinstance(claims, dict), "profile claims are absent")
    require(claims.get("posix_conformance") is False, "profile claims POSIX conformance")
    require(claims.get("unix_conformance") is False, "profile claims UNIX conformance")

    language = profile.get("language")
    require(isinstance(language, dict), "language contract is absent")
    require(language.get("iso_basis") == "ISO/IEC 9899:2018", "C17 basis drift")
    require(language.get("production") == "gnu17", "production language mode drift")
    require(language.get("portable_gate") == "c17", "portable language gate drift")
    validate_later_families(root, profile)

    corpus_counts = corpus.get("corpus", {})
    issue8_entities = [entity for entity in entities if entity.issue8_source]
    issue8_only = [entity for entity in issue8_entities if entity.edition_relation == "issue8-only"]
    denominator = profile.get("denominator")
    require(isinstance(denominator, dict), "profile denominator is absent")
    require(
        denominator.get("union_entities") == corpus_counts.get("entity_count") == 1617,
        "union denominator drift",
    )
    require(
        denominator.get("issue8_entities")
        == corpus_counts.get("edition_counts", {}).get("issue8")
        == len(issue8_entities)
        == 1518,
        "Issue 8 denominator drift",
    )
    require(
        denominator.get("issue8_only_entities") == len(issue8_only) == 152,
        "Issue 8-only denominator drift",
    )

    require(len(rows) == len(issue8_entities), "DPUCP ledger does not cover Issue 8")
    expected_entities = {(entity.kind, entity.name): entity for entity in issue8_entities}
    seen = {}
    for row_number, row in enumerate(rows, 2):
        require(set(row) == set(REQUIREMENT_COLUMNS), f"malformed DPUCP row {row_number}")
        key = (row["kind"], row["name"])
        require(key in expected_entities, f"unknown DPUCP entity on row {row_number}")
        require(key not in seen, f"duplicate DPUCP entity on row {row_number}")
        entity = expected_entities[key]
        expected_id = f"dpucp-2024:{entity.kind}:{entity.name}"
        require(row["requirement_id"] == expected_id, f"requirement id drift: {expected_id}")
        require(
            row["issue8_citation"] == entity.issue8_source,
            f"Issue 8 citation drift: {expected_id}",
        )
        require(row["family"] == FAMILIES[entity.kind], f"family drift: {expected_id}")
        require(bool(row["option_owner"]), f"missing option owner: {expected_id}")
        require(row["applicability"] in APPLICABILITY_STATES, f"bad applicability: {expected_id}")
        require(
            row["implementation"] in IMPLEMENTATION_STATES,
            f"bad implementation: {expected_id}",
        )
        require(row["evidence"] in EVIDENCE_STATES, f"bad evidence: {expected_id}")
        require(row["residual"] in RESIDUAL_STATES, f"bad residual: {expected_id}")
        if row["applicability"] == "unselected-standard-option":
            require(
                row["option_owner"] not in UNOWNED_OPTIONS,
                f"unselected option lacks standard owner: {expected_id}",
            )
        witness_ids = validate_witnesses(root, profile, row)
        for witness_id in witness_ids:
            require(
                profile["witnesses"][witness_id]["evidence"] == row["evidence"],
                f"witness evidence differs from row: {expected_id}",
            )
        if row["residual"] == "closed":
            require(row["evidence"] != "none", f"closed row lacks evidence: {expected_id}")
            require(witness_ids, f"closed row lacks executable witness: {expected_id}")
            require(not row["next_action"], f"closed row retains next action: {expected_id}")
            if row["applicability"] != "unselected-standard-option":
                require(
                    row["implementation"] == "present",
                    f"closed applicable row is not present: {expected_id}",
                )
        else:
            require(bool(row["next_action"]), f"open row lacks deciding action: {expected_id}")
        if row["evidence"] == "none":
            require(not witness_ids, f"evidence-free row names witness: {expected_id}")
        seen[key] = row

    require(set(seen) == set(expected_entities), "DPUCP entity set differs from Issue 8")
    expected_order = [(entity.kind, entity.name) for entity in issue8_entities]
    require(list(seen) == expected_order, "DPUCP rows are not in corpus order")
    validate_first_hardware_release(
        profile,
        {row["requirement_id"]: row for row in rows},
    )

    requirements = profile.get("requirements")
    require(isinstance(requirements, dict), "requirements metadata is absent")
    require(
        requirements.get("path") == "docs/research/dpucp-2024-requirements.tsv",
        "requirement path drift",
    )
    require(requirements.get("row_count") == len(rows), "requirement count drift")
    require(
        requirements.get("ordered_row_sha256") == ordered_sha256(rows),
        "requirement hash drift",
    )
    require(profile.get("state_counts") == count_states(rows), "DPUCP state counts drift")
    return count_states(rows)


def self_test(root, corpus, entities, profile, rows):
    validate_documents(root, corpus, entities, profile, rows)

    def expect_bad(label, mutated_profile, mutated_rows):
        try:
            validate_documents(root, corpus, entities, mutated_profile, mutated_rows)
        except ProfileError:
            return
        raise ProfileError(f"negative control passed: {label}")

    missing = copy.deepcopy(rows)
    missing.pop()
    expect_bad("missing requirement", profile, missing)

    duplicate = copy.deepcopy(rows)
    duplicate.append(copy.deepcopy(duplicate[-1]))
    expect_bad("duplicate requirement", profile, duplicate)

    bad_action = copy.deepcopy(rows)
    bad_action[0]["next_action"] = ""
    expect_bad("open row without action", profile, bad_action)

    bad_claim = copy.deepcopy(profile)
    bad_claim["claims"]["posix_conformance"] = True
    expect_bad("conformance claim", bad_claim, rows)


def refresh_generated(root, profile_path, requirements_path, corpus, entities, profile):
    previous_rows = parse_requirements(requirements_path) if requirements_path.is_file() else []
    rows = derive_requirements(corpus, entities, previous_rows)
    profile = copy.deepcopy(profile)
    profile["requirements"]["row_count"] = len(rows)
    profile["requirements"]["ordered_row_sha256"] = ordered_sha256(rows)
    profile["state_counts"] = count_states(rows)
    requirements_path.write_text(render_requirements(rows), encoding="utf-8")
    profile_path.write_text(json.dumps(profile, indent=2) + "\n", encoding="utf-8")


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument(
        "--profile",
        type=Path,
        default=Path("docs/research/dpucp-2024-profile.json"),
    )
    parser.add_argument(
        "--requirements",
        type=Path,
        default=Path("docs/research/dpucp-2024-requirements.tsv"),
    )
    parser.add_argument("--refresh", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def resolve(root, path):
    return path if path.is_absolute() else root / path


def main():
    arguments = parse_args()
    root = arguments.root.resolve()
    corpus = sus.load_json(root / "docs/research/sus-requirement-corpus.json")
    entities = sus.parse_entities(root / "docs/research/sus-requirement-entities.tsv")
    profile_path = resolve(root, arguments.profile)
    requirements_path = resolve(root, arguments.requirements)
    profile = load_json(profile_path)
    if arguments.refresh:
        refresh_generated(root, profile_path, requirements_path, corpus, entities, profile)
    rows = parse_requirements(requirements_path)
    current_profile = profile if not arguments.refresh else load_json(profile_path)
    counts = validate_documents(root, corpus, entities, current_profile, rows)
    if arguments.self_test:
        self_test(root, corpus, entities, load_json(profile_path), rows)
    print(
        "DPUCP-2024 profile: "
        f"{len(rows)} Issue 8 rows, {counts['residual']['closed']} closed, "
        f"{counts['residual']['open']} open, {counts['residual']['blocked']} blocked"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
