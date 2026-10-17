"""Mutation controls for the bounded DPUCP donor ledger."""

import copy
import sys
from pathlib import Path

import check_dpucp_donor_ledger as donors


def expect_failure(name, root, document, requirement_ids):
    try:
        donors.validate_document(root, document, requirement_ids)
    except donors.DonorError:
        return None
    return name


def main():
    root = Path(__file__).resolve().parents[1]
    document = donors.load_json(root / "docs/research/dpucp-2024-donor-ledger.json")
    requirement_ids = donors.load_requirement_ids(
        root / "docs/research/dpucp-2024-requirements.tsv"
    )
    donors.validate_document(root, document, requirement_ids)

    promoted_ids = {
        "libc-reallocarray",
        "libc-memmem",
        "libc-getdelim-getline",
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
    cases.append(("premature unrelated promotion", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["size_estimate"]["surfaces"] = [""]
    cases.append(("blank size surface", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["sources"][0]["license"] = "UNKNOWN"
    cases.append(("unknown donor license", changed))

    changed = copy.deepcopy(document)
    changed["candidates"][0]["c17_rewrite"] = True
    cases.append(("nontext C17 rewrite", changed))

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
