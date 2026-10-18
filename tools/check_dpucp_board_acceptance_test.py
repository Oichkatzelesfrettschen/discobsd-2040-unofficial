"""Mutation controls for the retained DPUCP-2024 board acceptance record."""

import copy
from pathlib import Path

import check_dpucp_board_acceptance as board_acceptance


def expect_failure(name, root, evidence, expected_message):
    try:
        board_acceptance.validate(root, evidence)
    except board_acceptance.EvidenceError as error:
        if expected_message not in str(error):
            raise AssertionError(
                f"mutation failed at the wrong invariant: {name}: {error}"
            ) from error
        return
    raise AssertionError(f"mutation passed: {name}")


def main():
    root = Path(__file__).resolve().parents[1]
    evidence = board_acceptance.load_json(
        root / "docs/research/dpucp-2024-board-acceptance.json"
    )
    board_acceptance.validate(root, evidence)

    mutations = []

    changed = copy.deepcopy(evidence)
    changed["schema_version"] = 2
    mutations.append(("schema", changed, "unsupported board evidence schema"))

    changed = copy.deepcopy(evidence)
    changed["profile"] = "POSIX.1-2024"
    mutations.append(("profile", changed, "board evidence profile differs"))

    changed = copy.deepcopy(evidence)
    changed["evidence_class"] = "renode"
    mutations.append(("evidence class", changed, "evidence class is not physical-board"))

    changed = copy.deepcopy(evidence)
    changed["source_commit"] = "0" * 40
    mutations.append(("source commit", changed, "source commit differs"))

    changed = copy.deepcopy(evidence)
    changed["release_surfaces"][0:2] = reversed(changed["release_surfaces"][0:2])
    mutations.append(("surface order", changed, "release surface differs"))

    changed = copy.deepcopy(evidence)
    changed["artifacts"]["kernel"]["sha256"] = "0" * 64
    mutations.append(("kernel identity", changed, "kernel artifact differs"))

    changed = copy.deepcopy(evidence)
    changed["flash"]["execution_count"] = 2
    mutations.append(("flash count", changed, "flash execution count differs"))

    changed = copy.deepcopy(evidence)
    changed["flash"]["command_projection"][1] = "/home/user/discobsd-flash"
    mutations.append(("host path", changed, "flash command projection differs"))

    changed = copy.deepcopy(evidence)
    changed["acceptance"]["runner"]["state"] = "failed"
    mutations.append(("runner state", changed, "acceptance runner differs"))

    changed = copy.deepcopy(evidence)
    changed["acceptance"]["runner"]["terminal_queue_dropped_bytes"] = 1
    mutations.append(("dropped bytes", changed, "acceptance runner differs"))

    changed = copy.deepcopy(evidence)
    changed["acceptance"]["cases"] = changed["acceptance"]["cases"][:-1]
    mutations.append(("missing case", changed, "board case set differs"))

    changed = copy.deepcopy(evidence)
    changed["acceptance"]["cases"][5]["observed_status"] = 1
    mutations.append(("case status", changed, "board case differs: getopts"))

    changed = copy.deepcopy(evidence)
    changed["acceptance"]["cases"][9]["observed_output"] = "wrong"
    mutations.append(("case output", changed, "board case differs: uudecode"))

    changed = copy.deepcopy(evidence)
    changed["acceptance"]["cases"][11]["command"] = ":"
    mutations.append((
        "case command",
        changed,
        "board case differs: timeout-expiry",
    ))

    changed = copy.deepcopy(evidence)
    changed["attribution"]["renode"] = "pass"
    mutations.append(("renode promotion", changed, "evidence attribution differs"))

    changed = copy.deepcopy(evidence)
    changed["residual_uncertainty"] = []
    mutations.append(("missing residuals", changed, "residual uncertainty differs"))

    changed = copy.deepcopy(evidence)
    changed["claim"] = "The image conforms to POSIX.1-2024."
    mutations.append(("conformance claim", changed, "compatibility claim differs"))

    for name, changed_evidence, expected_message in mutations:
        expect_failure(name, root, changed_evidence, expected_message)

    print(f"PASS DPUCP board acceptance calibration: {len(mutations)} mutations rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
