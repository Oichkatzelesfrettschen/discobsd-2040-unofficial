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


def main():
    root = Path(__file__).resolve().parents[1]
    corpus = sus.load_json(root / "docs/research/sus-requirement-corpus.json")
    entities = sus.parse_entities(root / "docs/research/sus-requirement-entities.tsv")
    profile = dpucp.load_json(root / "docs/research/dpucp-2024-profile.json")
    rows = dpucp.parse_requirements(root / "docs/research/dpucp-2024-requirements.tsv")
    dpucp.validate_documents(root, corpus, entities, profile, rows)

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

    derived = dpucp.derive_requirements(corpus, entities)
    if dpucp.render_requirements(derived) != dpucp.render_requirements(rows):
        raise AssertionError("committed requirements differ from deterministic derivation")

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

    print(f"PASS DPUCP-2024 calibration: {len(cases)} mutations rejected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
