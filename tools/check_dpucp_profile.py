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


class ProfileError(RuntimeError):
    """Report a DPUCP schema, denominator, or evidence contradiction."""


def require(condition, message):
    if not condition:
        raise ProfileError(message)


def valid_relative_path(value):
    path = PurePosixPath(value)
    return bool(value) and not path.is_absolute() and ".." not in path.parts


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


def validate_documents(root, corpus, entities, profile, rows):
    require(profile.get("schema_version") == 1, "unsupported DPUCP schema")
    identity = profile.get("profile")
    require(isinstance(identity, dict), "profile identity is absent")
    require(identity.get("id") == "DPUCP-2024", "profile id drift")
    require(
        identity.get("name") == "DiscoBSD/Pico Unix Compatibility Profile 2024",
        "profile name drift",
    )

    claims = profile.get("claims")
    require(isinstance(claims, dict), "profile claims are absent")
    require(claims.get("posix_conformance") is False, "profile claims POSIX conformance")
    require(claims.get("unix_conformance") is False, "profile claims UNIX conformance")

    language = profile.get("language")
    require(isinstance(language, dict), "language contract is absent")
    require(language.get("iso_basis") == "ISO/IEC 9899:2018", "C17 basis drift")
    require(language.get("production") == "gnu17", "production language mode drift")
    require(language.get("portable_gate") == "c17", "portable language gate drift")

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
