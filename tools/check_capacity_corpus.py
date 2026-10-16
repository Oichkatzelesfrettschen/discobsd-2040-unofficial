"""Validate the bounded RP2040 capacity-decision corpus."""

import argparse
import copy
import json
import re
import sys
from pathlib import Path, PurePosixPath

RESOURCES = {"NPROC", "NINODE", "NFILE", "NCLIST", "NBUF", "USIZE"}
PRODUCTION_CONFIG = "sys/arch/rp2040/include/machparam.h"
REQUIRED_OBLIGATIONS = {
    "NPROC": {"normal", "exhaustion", "recovery"},
    "NINODE": {"normal", "exhaustion", "recovery"},
    "NFILE": {"normal", "exhaustion", "recovery"},
    "NCLIST": {"normal", "exhaustion", "recovery"},
    "NBUF": {"normal", "exhaustion", "recovery", "writeback"},
    "USIZE": {
        "normal",
        "signal",
        "deliberate-fault",
        "swapram",
        "context-switch",
        "geometry",
        "recovery",
    },
}
EXECUTION_CLASSES = {"host", "ilp32", "qemu-user", "renode", "board"}
LOCAL_EXECUTION_CLASSES = {"host", "ilp32", "qemu-user"}
RECEIPT_STATES = {"executable", "retained", "open"}
DECISION_STATES = {"retain", "reduce-approved"}
CASE_FIELDS = {
    "id",
    "resources",
    "obligations",
    "fixture",
    "execution_class",
    "receipt_state",
    "receipt",
    "dependencies",
    "expected",
    "recovery",
}
DECISION_FIELDS = {
    "resource",
    "active_value",
    "state",
    "greatest_observed_peak",
    "reserve_rule",
    "deciding_cases",
}


class CorpusError(Exception):
    """A capacity-corpus invariant failed."""


def require(condition, message):
    if not condition:
        raise CorpusError(message)


def safe_relative_path(path):
    pure = PurePosixPath(path)
    return bool(path) and not pure.is_absolute() and ".." not in pure.parts


def production_values(root):
    config_path = root / PRODUCTION_CONFIG
    try:
        config = config_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise CorpusError(f"cannot read production configuration: {error}") from error
    values = {}
    for resource in RESOURCES:
        matches = re.findall(
            rf"^#define[ \t]+{resource}[ \t]+([0-9]+)(?:[ \t]|$)",
            config,
            re.MULTILINE,
        )
        require(len(matches) == 1, f"production value is not unique: {resource}")
        values[resource] = int(matches[0])
    return values


def visit(case_id, cases, visiting, visited):
    if case_id in visited:
        return
    require(case_id not in visiting, f"dependency cycle at {case_id}")
    visiting.add(case_id)
    for dependency in cases[case_id]["dependencies"]:
        visit(dependency, cases, visiting, visited)
    visiting.remove(case_id)
    visited.add(case_id)


def validate(root, document):
    require(document.get("schema_version") == 1, "unsupported capacity schema")
    raw_cases = document.get("cases")
    raw_decisions = document.get("decisions")
    require(isinstance(raw_cases, list), "cases must be a list")
    require(isinstance(raw_decisions, list), "decisions must be a list")
    active_values = production_values(root)

    cases = {}
    coverage = {resource: set() for resource in RESOURCES}
    for case in raw_cases:
        require(isinstance(case, dict) and set(case) == CASE_FIELDS, f"malformed case: {case!r}")
        case_id = case["id"]
        require(isinstance(case_id, str) and case_id, "case id must be nonempty")
        require(case_id not in cases, f"duplicate case id: {case_id}")
        require(case["execution_class"] in EXECUTION_CLASSES, f"unknown execution class: {case_id}")
        require(case["receipt_state"] in RECEIPT_STATES, f"unknown receipt state: {case_id}")
        if case["receipt_state"] == "executable":
            require(
                case["execution_class"] in LOCAL_EXECUTION_CLASSES,
                f"hardware execution requires a retained receipt: {case_id}",
            )
        receipt = case["receipt"]
        if case["receipt_state"] == "retained":
            require(
                isinstance(receipt, str) and safe_relative_path(receipt),
                f"missing retained receipt: {case_id}",
            )
            require((root / receipt).is_file(), f"missing receipt artifact: {receipt}")
        else:
            require(receipt is None, f"non-retained case names a receipt: {case_id}")
        require(
            isinstance(case["resources"], list) and case["resources"],
            f"missing resources: {case_id}",
        )
        require(set(case["resources"]) <= RESOURCES, f"unknown resource: {case_id}")
        require(
            isinstance(case["obligations"], list) and case["obligations"],
            f"missing obligations: {case_id}",
        )
        require(isinstance(case["dependencies"], list), f"invalid dependencies: {case_id}")
        require(
            isinstance(case["expected"], str) and case["expected"],
            f"missing expected result: {case_id}",
        )
        require(
            isinstance(case["recovery"], str) and case["recovery"],
            f"missing recovery result: {case_id}",
        )
        fixture = case["fixture"]
        require(
            isinstance(fixture, str) and safe_relative_path(fixture),
            f"unsafe fixture path: {case_id}",
        )
        require((root / fixture).is_file(), f"missing fixture path: {fixture}")
        for resource in case["resources"]:
            coverage[resource].update(case["obligations"])
        cases[case_id] = case

    for case_id, case in cases.items():
        for dependency in case["dependencies"]:
            require(dependency in cases, f"unknown dependency {dependency} in {case_id}")
    visited = set()
    for case_id in cases:
        visit(case_id, cases, set(), visited)

    decisions = {}
    for decision in raw_decisions:
        require(
            isinstance(decision, dict) and set(decision) == DECISION_FIELDS,
            f"malformed decision: {decision!r}",
        )
        resource = decision["resource"]
        require(resource in RESOURCES, f"unknown decision resource: {resource}")
        require(resource not in decisions, f"duplicate decision: {resource}")
        require(
            decision["active_value"] == active_values[resource], f"active value drift: {resource}"
        )
        require(decision["state"] in DECISION_STATES, f"unknown decision state: {resource}")
        require(
            isinstance(decision["reserve_rule"], str) and decision["reserve_rule"],
            f"missing reserve rule: {resource}",
        )
        require(
            isinstance(decision["greatest_observed_peak"], int)
            and 0 <= decision["greatest_observed_peak"] < decision["active_value"],
            f"invalid observed peak: {resource}",
        )
        require(
            isinstance(decision["deciding_cases"], list) and decision["deciding_cases"],
            f"missing deciding cases: {resource}",
        )
        for case_id in decision["deciding_cases"]:
            require(case_id in cases, f"unknown deciding case {case_id} for {resource}")
            require(
                resource in cases[case_id]["resources"], f"case {case_id} does not cover {resource}"
            )
        if decision["state"] == "reduce-approved":
            deciding = [cases[case_id] for case_id in decision["deciding_cases"]]
            closure = set()
            pending = list(decision["deciding_cases"])
            while pending:
                case_id = pending.pop()
                if case_id in closure:
                    continue
                closure.add(case_id)
                pending.extend(cases[case_id]["dependencies"])
            require(
                all(
                    cases[case_id]["receipt_state"] == "retained"
                    or (
                        cases[case_id]["receipt_state"] == "executable"
                        and cases[case_id]["execution_class"] in LOCAL_EXECUTION_CLASSES
                    )
                    for case_id in closure
                ),
                f"{resource} has an open deciding dependency",
            )
            for execution_class in ("renode", "board"):
                admitted = {
                    obligation
                    for case in deciding
                    if case["execution_class"] == execution_class
                    and case["receipt_state"] == "retained"
                    for obligation in case["obligations"]
                }
                missing = REQUIRED_OBLIGATIONS[resource] - admitted
                require(
                    not missing,
                    f"{resource} lacks retained {execution_class} obligations: "
                    + ", ".join(sorted(missing)),
                )
        decisions[resource] = decision
    require(set(decisions) == RESOURCES, "capacity decisions do not cover all six resources")
    for resource, obligations in REQUIRED_OBLIGATIONS.items():
        missing = obligations - coverage[resource]
        require(not missing, f"{resource} lacks corpus obligations: {', '.join(sorted(missing))}")
        for execution_class in ("renode", "board"):
            declared = {
                obligation
                for case in cases.values()
                if resource in case["resources"] and case["execution_class"] == execution_class
                for obligation in case["obligations"]
            }
            missing = obligations - declared
            require(
                not missing,
                f"{resource} lacks declared {execution_class} obligations: "
                + ", ".join(sorted(missing)),
            )
    return cases, decisions


def self_test(root, document):
    validate(root, document)
    mutations = []
    changed = copy.deepcopy(document)
    changed["cases"].pop()
    mutations.append(("omitted coverage row", changed))
    changed = copy.deepcopy(document)
    changed["cases"][0]["id"] = changed["cases"][1]["id"]
    mutations.append(("duplicate id", changed))
    changed = copy.deepcopy(document)
    changed["cases"][0]["fixture"] = "tests/rp2040/absent.c"
    mutations.append(("stale fixture", changed))
    changed = copy.deepcopy(document)
    changed["cases"][0]["execution_class"] = "hardware"
    mutations.append(("unknown execution class", changed))
    changed = copy.deepcopy(document)
    changed["decisions"][0]["state"] = "reduce-approved"
    mutations.append(("reduction without board admission", changed))
    changed = copy.deepcopy(document)
    changed["decisions"][0]["active_value"] += 1
    mutations.append(("production configuration disagreement", changed))
    changed = copy.deepcopy(document)
    changed["cases"] = [
        case for case in changed["cases"] if case["id"] != "process-table-exhaustion"
    ]
    changed["decisions"][0]["deciding_cases"].remove("process-table-exhaustion")
    mutations.append(("omitted allocator exhaustion", changed))
    changed = copy.deepcopy(document)
    retained = next(case for case in changed["cases"] if case["receipt_state"] == "retained")
    retained["receipt"] = None
    mutations.append(("retained case without receipt", changed))
    changed = copy.deepcopy(document)
    changed["decisions"][-1]["state"] = "reduce-approved"
    mutations.append(("reduction with open deciding dependency", changed))
    changed = copy.deepcopy(document)
    decision = changed["decisions"][-1]
    decision["state"] = "reduce-approved"
    case_by_id = {case["id"]: case for case in changed["cases"]}
    closure = set()
    pending = list(decision["deciding_cases"])
    while pending:
        case_id = pending.pop()
        if case_id in closure:
            continue
        closure.add(case_id)
        pending.extend(case_by_id[case_id]["dependencies"])
    for case_id in closure:
        case = case_by_id[case_id]
        if case["execution_class"] in {"board", "renode"}:
            case["receipt_state"] = "retained"
            case["receipt"] = "docs/research/board-capacity-acceptance-93b709fc.txt"
    transitive_board = case_by_id["swapram-window-epochs"]
    transitive_board["receipt_state"] = "executable"
    transitive_board["receipt"] = None
    mutations.append(("hardware dependency without retained receipt", changed))
    changed = copy.deepcopy(document)
    changed["cases"][0]["dependencies"] = [changed["cases"][0]["id"]]
    mutations.append(("dependency cycle", changed))
    for name, mutated in mutations:
        try:
            validate(root, mutated)
        except CorpusError:
            continue
        raise CorpusError(f"calibration mutation passed: {name}")
    return len(mutations)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--corpus", default="tests/rp2040/capacity-corpus.json")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    try:
        document = json.loads((root / args.corpus).read_text(encoding="utf-8"))
        cases, decisions = validate(root, document)
        mutation_count = self_test(root, document) if args.self_test else 0
    except (OSError, UnicodeError, json.JSONDecodeError, CorpusError) as error:
        print(f"FAIL capacity corpus: {error}", file=sys.stderr)
        return 1
    suffix = f", {mutation_count} mutations rejected" if args.self_test else ""
    print(f"PASS capacity corpus: {len(cases)} cases, {len(decisions)} retained decisions{suffix}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
