#!/usr/bin/env python3
"""Validate the bounded SUS utility ownership ledger against live sources."""

import argparse
import copy
import json
import re
from pathlib import Path


class LedgerError(Exception):
    """The ledger contradicts its declared source authority."""


def require(condition, message):
    if not condition:
        raise LedgerError(message)


def manifest_entries(path):
    entries = {}
    pending_link = None
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        words = raw_line.split()
        if not words or words[0].startswith("#"):
            continue
        if words[0] in {"file", "pack"} and len(words) == 2:
            entries[words[1]] = words[1]
        elif words[0] == "link" and len(words) == 2:
            require(pending_link is None, "manifest starts a link before its target")
            pending_link = words[1]
        elif words[0] == "target" and pending_link is not None and len(words) == 2:
            entries[pending_link] = words[1]
            pending_link = None
    require(pending_link is None, "manifest ends with a link lacking its target")
    return entries


def make_words(path, variable):
    logical_lines = []
    current = ""
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].rstrip()
        if current:
            current += " " + line.lstrip()
        else:
            current = line
        if current.endswith("\\"):
            current = current[:-1].rstrip()
            continue
        logical_lines.append(current)
        current = ""
    require(not current, f"{path} ends inside a continued assignment")
    prefix = re.compile(rf"^{re.escape(variable)}\s*=\s*(.*)$")
    for line in logical_lines:
        match = prefix.match(line)
        if match:
            return match.group(1).split()
    raise LedgerError(f"{path} lacks {variable}")


def dispatch_names(path):
    tools = set(make_words(path, "TOOLS"))
    aliases = {
        alias.split(":", 1)[0]
        for alias in make_words(path, "ALIASES")
        if ":" in alias
    }
    return tools | aliases


def bourne_builtins(path):
    text = path.read_text(encoding="latin-1")
    start = text.find("struct sysnod commands[]")
    require(start >= 0, "bin/sh/msg.c lacks the commands table")
    end = text.find("\n};", start)
    require(end >= 0, "bin/sh/msg.c commands table is unterminated")
    return set(re.findall(r'\{\s*"([^"]+)"\s*,', text[start:end]))


def case_calls_external(path, case_name):
    text = path.read_text(encoding="latin-1")
    match = re.search(rf"\bcase\s+{re.escape(case_name)}\s*:", text)
    require(match is not None, f"{path} lacks case {case_name}")
    remainder = text[match.end() :]
    next_case = re.search(r"\bcase\s+[A-Z][A-Z0-9_]*\s*:", remainder)
    body = remainder[: next_case.start()] if next_case else remainder
    return re.search(r"\bexeca\s*\(", body) is not None


def bourne_behavior_cases(path):
    text = path.read_text(encoding="utf-8")
    return set(
        re.findall(r"(?m)^(?:ok|okrc|xfail)\s+([A-Za-z0-9_.-]+)\b", text)
    )


def root_targets(path):
    text = path.read_text(encoding="utf-8")
    return set(re.findall(r"(?m)^([A-Za-z0-9_.-]+)\s*:", text))


def expanded(group, field, name):
    if field in group:
        return group[field]
    pattern = group.get(field + "_pattern")
    return pattern.format(name=name) if pattern is not None else None


def source_name_exists(root, name):
    return any(
        (root / directory / name).exists()
        for directory in ("bin", "usr.bin", "sbin", "usr.sbin", "libexec")
    )


def validate_document(root, document):
    require(document.get("schema_version") == 1, "unsupported ledger schema")
    require(isinstance(document.get("groups"), list), "ledger groups must be a list")
    authorities = document.get("authorities", {})
    manifest = manifest_entries(root / authorities["manifest"])
    manifest_names = {Path(path).name for path in manifest}
    builtins = bourne_builtins(root / authorities["bourneshell_builtins"])
    builtin_dispatch = root / authorities["bourneshell_dispatch"]
    builtin_cases = bourne_behavior_cases(root / authorities["bourneshell_tests"])
    targets = root_targets(root / authorities["root_gates"])
    seen_names = set()
    dispatch_cache = {}

    for group in document["groups"]:
        names = group.get("names")
        require(isinstance(names, list) and names, "each ledger group needs names")
        kind = group.get("kind")
        require(
            kind
            in {
                "manifest",
                "source-only",
                "bourne-builtin-present",
                "bourne-builtin-dispatch-stub",
                "bourne-builtin-absent",
                "absent",
            },
            f"unknown ledger kind: {kind}",
        )
        gates = group.get("behavior_gates")
        require(isinstance(gates, list), f"{names[0]} lacks behavior_gates")
        for gate in gates:
            require(gate in targets, f"{names[0]} names missing gate {gate}")
        behavior_cases = group.get("behavior_cases", {})
        require(
            isinstance(behavior_cases, dict),
            f"{names[0]} behavior_cases must be an object",
        )
        if kind.startswith("bourne-builtin-"):
            if gates:
                require(
                    set(behavior_cases) == set(names),
                    f"{names[0]} behavior cases do not cover the group",
                )
            else:
                require(
                    not behavior_cases,
                    f"{names[0]} names behavior cases without a gate",
                )
            for name, case_name in behavior_cases.items():
                require(
                    case_name in builtin_cases,
                    f"{name} names missing shell case {case_name}",
                )

        dispatch = group.get("dispatch")
        if dispatch and dispatch not in dispatch_cache:
            dispatch_cache[dispatch] = dispatch_names(root / dispatch)

        for name in names:
            require(name not in seen_names, f"duplicate utility row: {name}")
            seen_names.add(name)
            source = expanded(group, "source", name)
            source_command = expanded(group, "source_command", name) or name
            installed_path = expanded(group, "installed_path", name)

            if kind == "manifest":
                require(source is not None, f"{name} lacks a source owner")
                require((root / source).exists(), f"{name} source is absent: {source}")
                require(
                    Path(source).name == source_command,
                    f"{name} source {source} does not own {source_command}",
                )
                require(installed_path in manifest, f"{name} is absent from the manifest")
                expected_target = expanded(group, "manifest_target", name)
                require(
                    manifest[installed_path] == expected_target,
                    f"{name} targets {manifest[installed_path]}, expected {expected_target}",
                )
                if dispatch:
                    require(
                        source_command == name,
                        f"{name} dispatch cannot select source command {source_command}",
                    )
                    require(
                        name in dispatch_cache[dispatch],
                        f"{name} is absent from {dispatch} dispatch",
                    )
                else:
                    require(
                        Path(expected_target).name == source_command,
                        f"{name} manifest target does not select {source_command}",
                    )
            elif kind == "source-only":
                require(source is not None, f"{name} lacks a source owner")
                require((root / source).exists(), f"{name} source is absent: {source}")
                require(
                    Path(source).name == source_command,
                    f"{name} source {source} does not own {source_command}",
                )
                require(name not in manifest_names, f"{name} unexpectedly ships")
            elif kind == "bourne-builtin-present":
                require(name in builtins, f"{name} is absent from Bourne shell builtins")
            elif kind == "bourne-builtin-dispatch-stub":
                require(name in builtins, f"{name} is absent from Bourne shell builtins")
                require(
                    group.get("external_command") == name,
                    f"{name} dispatch stub names a different external command",
                )
                require(name not in manifest_names, f"{name} external command ships")
                require(
                    case_calls_external(builtin_dispatch, group.get("dispatch_case", "")),
                    f"{name} dispatch case does not call execa",
                )
            elif kind == "bourne-builtin-absent":
                require(name not in builtins, f"{name} unexpectedly exists as a builtin")
            elif kind == "absent":
                require(name not in manifest_names, f"{name} unexpectedly ships")
                require(not source_name_exists(root, name), f"{name} unexpectedly has source")

    return len(seen_names)


def load_document(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise LedgerError(f"cannot read ledger {path}: {error}") from error


def expect_bad(label, action):
    try:
        action()
    except LedgerError:
        return
    raise LedgerError(f"negative control passed: {label}")


def self_test(root, document):
    count = validate_document(root, document)

    wrong_target = copy.deepcopy(document)
    wrong_target["groups"][0]["manifest_target"] = "/usr/bin/utilbox"
    expect_bad("wrong manifest owner", lambda: validate_document(root, wrong_target))

    wrong_source = copy.deepcopy(document)
    wrong_source["groups"][0]["source"] = "bin/sh"
    expect_bad("wrong source owner", lambda: validate_document(root, wrong_source))

    wrong_alias_source = copy.deepcopy(document)
    for group in wrong_alias_source["groups"]:
        if group["names"] == ["vi"]:
            group["source"] = "bin/sh"
            group["source_command"] = "sh"
            break
    expect_bad(
        "wrong alias source owner",
        lambda: validate_document(root, wrong_alias_source),
    )

    missing_builtin = copy.deepcopy(document)
    for group in missing_builtin["groups"]:
        if group["kind"] == "bourne-builtin-absent":
            group["kind"] = "bourne-builtin-present"
            break
    expect_bad("missing Bourne builtin", lambda: validate_document(root, missing_builtin))

    missing_behavior_case = copy.deepcopy(document)
    for group in missing_behavior_case["groups"]:
        if group.get("behavior_cases"):
            first_name = next(iter(group["behavior_cases"]))
            group["behavior_cases"][first_name] = "missing_case_label"
            break
    expect_bad(
        "missing Bourne behavior case",
        lambda: validate_document(root, missing_behavior_case),
    )

    wrong_dispatch = copy.deepcopy(document)
    for group in wrong_dispatch["groups"]:
        if group["kind"] == "bourne-builtin-dispatch-stub":
            group["dispatch_case"] = "SYSCD"
            break
    expect_bad("non-exec builtin dispatch", lambda: validate_document(root, wrong_dispatch))

    duplicate = copy.deepcopy(document)
    duplicate["groups"][1]["names"].append(duplicate["groups"][0]["names"][0])
    expect_bad("duplicate utility", lambda: validate_document(root, duplicate))
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--ledger", type=Path)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()

    root = arguments.root.resolve()
    ledger = arguments.ledger or root / "docs/research/sus-utility-ledger.json"
    try:
        document = load_document(ledger)
        if arguments.self_test:
            count = self_test(root, document)
        else:
            count = validate_document(root, document)
    except LedgerError as error:
        parser.error(str(error))
    print(f"SUS utility ledger: {count} names validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
