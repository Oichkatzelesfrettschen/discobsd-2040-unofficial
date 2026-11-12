"""Bind repository review coverage to an immutable Git tree and explicit witnesses.

Classification establishes a denominator. A reviewed component additionally
requires a content-addressed review record naming every assigned path. The
validator checks the record's structure and identities; a reviewer remains
responsible for the substance of the source analysis.
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path, PurePosixPath

ARTIFACT_CLASSES = {
    "maintained-implementation", "test", "build-configuration", "documentation",
    "generated-output", "imported-dependency", "retained-historical-artifact",
}
INPUTS = ("tools/c17/source-denominator.json", "tools/test-execution-inventory.json")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)


def git(root, *arguments):
    return subprocess.check_output(["git", "-C", str(root), *arguments])


def tracked_tree(root, revision):
    commit = git(root, "rev-parse", "--verify", "--end-of-options",
                 revision + "^{commit}").decode().strip()
    tree = git(root, "rev-parse", commit + "^{tree}").decode().strip()
    entries = {}
    for entry in git(root, "ls-tree", "-rz", "--full-tree", commit).split(b"\0"):
        if entry:
            metadata, name = entry.split(b"\t", 1)
            mode, _kind, identity = metadata.decode().split()
            entries[name.decode("utf-8")] = (mode, identity)
    return commit, tree, entries


def local_file(root, name):
    require(isinstance(name, str), "witness path must be text")
    path = PurePosixPath(name)
    require(bool(name) and not path.is_absolute() and ".." not in path.parts,
            f"witness path must stay inside the checkout: {name}")
    candidate = root / path
    require(candidate.resolve().is_relative_to(root), f"witness path escapes checkout: {name}")
    require(candidate.is_file() and not candidate.is_symlink(), f"missing witness path: {name}")
    return candidate


def imported_paths(root, commit):
    result = subprocess.run(
        ["git", "-C", str(root), "show", commit + ":tools/netbsd-imports.manifest"],
        capture_output=True, text=True,
    )
    paths = set()
    if result.returncode == 0:
        for line in result.stdout.splitlines():
            fields = line.split()
            if len(fields) >= 2 and re.fullmatch(r"[a-f0-9]{64}", fields[0]):
                paths.add(fields[1])
    return paths


def classify(path, imports):
    parts = PurePosixPath(path).parts
    filename = parts[-1]
    component = "/".join(parts[:2]) if len(parts) > 2 else parts[0]
    if path.startswith("sys/arch/") and len(parts) > 4:
        component = "/".join(parts[:5])
    if path.startswith("sys/fs/"):
        component = "/".join(parts[:3])
    if path.startswith("legacy/"):
        artifact = "retained-historical-artifact"
    elif path == ".claude/CLAUDE.md" or (
        path.startswith("sys/arch/") and "/compile/" in path and filename != "Config"
    ):
        artifact = "generated-output"
    elif path in imports or any(
        path.startswith(prefix) for prefix in (
            "sys/arch/rp2040/dhara/", "sys/arch/rp2040/heatshrink/",
            "benchmarks/coremark/",
        )
    ):
        artifact = "imported-dependency"
    elif filename in {"Makefile", "CMakeLists.txt", ".gitignore", "ruff.toml"} or (
        filename.startswith("Makefile.") or path.startswith(".github/") or
        filename.endswith((".mk", ".yml", ".yaml"))
    ):
        artifact = "build-configuration"
    elif path.startswith("tests/") or "_test." in filename or "/tests/" in path:
        artifact = "test"
    elif path.startswith(("docs/", "share/man/")) or "/doc/" in path or filename in {
        "AGENTS.md", "LICENSE", "NOTICE", "README", "README.md", "DEVICE-INVENTORY.md",
    } or filename.endswith((".md", ".txt")):
        artifact = "documentation"
    else:
        artifact = "maintained-implementation"
    return component, artifact


def initialize(root, revision):
    commit, tree, entries = tracked_tree(root, revision)
    imports = imported_paths(root, commit)
    rows = []
    for path, (mode, blob) in sorted(entries.items()):
        component, artifact = classify(path, imports)
        rows.append(dict(path=path, mode=mode, blob=blob, component=component,
                         artifact_class=artifact))
    return {
        "schema_version": 1,
        "baseline": {"commit": commit, "tree": tree},
        "inputs": [{"path": path, "blob": entries[path][1]} for path in INPUTS],
        "paths": rows,
        "components": [
            {"id": component, "state": "pending", "review_witness": None,
             "next_action": "Trace component contracts, callers and failure paths; record "
                            "source review, calibrated checks, findings and evidence limits."}
            for component in sorted({row["component"] for row in rows})
        ],
    }


def text_list(value, label, nonempty=True):
    require(isinstance(value, list) and (bool(value) or not nonempty) and
            all(isinstance(item, str) and item.strip() for item in value), label)


def verify_review(root, baseline, component, assigned, entries):
    reference = component.get("review_witness")
    require(isinstance(reference, dict), f"{component['id']}: missing review witness")
    path = local_file(root, reference.get("path"))
    require(hashlib.sha256(path.read_bytes()).hexdigest() == reference.get("sha256"),
            f"{component['id']}: witness identity differs")
    record = load_json(path)
    reviewed = record.get("paths")
    require(isinstance(reviewed, list) and len(reviewed) == len(set(reviewed)) and
            set(reviewed) == assigned, f"{component['id']}: reviewed path set differs")
    require(record.get("baseline") == baseline, f"{component['id']}: review baseline differs")
    require(isinstance(record.get("method"), str) and record["method"].strip(),
            f"{component['id']}: review method required")
    for field in ("failure_classes", "source_evidence", "limitations"):
        text_list(record.get(field), f"{component['id']}: missing {field}")
    require(set(record["source_evidence"]) <= set(entries), "unresolved source reference")
    for field in ("tests", "findings"):
        require(isinstance(record.get(field), list), f"{component['id']}: missing {field}")
    for test in record["tests"]:
        require(test.get("source_commit") == baseline, "test source revision differs")
        require(test.get("result") in {"pass", "fail", "blocked"}, "unsupported test result")
        require(isinstance(test.get("command"), str) and test["command"].strip(),
                "test command required")
        evidence = local_file(root, test.get("evidence"))
        require(hashlib.sha256(evidence.read_bytes()).hexdigest() == test.get("sha256"),
                "test evidence identity differs")
    findings = set()
    for finding in record["findings"]:
        require(isinstance(finding.get("id"), str) and finding["id"] and
                finding["id"] not in findings, "missing or duplicate finding identity")
        findings.add(finding["id"])
        require(finding.get("state") in {"open", "repaired"}, "unsupported finding state")
        require(isinstance(finding.get("evidence"), str) and finding["evidence"].strip(),
                "finding evidence required")
        if finding["state"] == "open":
            require(isinstance(finding.get("next_action"), str) and
                    finding["next_action"].strip(), "open finding requires next action")
        else:
            require(finding.get("test") in [test["command"] for test in record["tests"]
                                           if test["result"] == "pass"],
                    "repaired finding requires passing test witness")


def validate(root, data, require_reviewed=False):
    require(data.get("schema_version") == 1, "unsupported schema")
    baseline = data["baseline"]
    require(re.fullmatch(r"[a-f0-9]{40}|[a-f0-9]{64}", baseline["commit"]) is not None,
            "baseline must be a complete object identity")
    commit, tree, entries = tracked_tree(root, baseline["commit"])
    require(baseline == {"commit": commit, "tree": tree}, "baseline tree identity differs")
    paths = data["paths"]
    counts = Counter(row["path"] for row in paths)
    require(all(count == 1 for count in counts.values()), "duplicate path assignment")
    missing, unexpected = set(entries) - set(counts), set(counts) - set(entries)
    require(not missing, f"missing tracked paths: {sorted(missing)[:4]}")
    require(not unexpected, f"unexpected tracked paths: {sorted(unexpected)[:4]}")
    input_counts = Counter(item["path"] for item in data["inputs"])
    require(set(input_counts) == set(INPUTS) and all(n == 1 for n in input_counts.values()),
            "input identity set differs")
    for item in data["inputs"]:
        require(item["blob"] == entries[item["path"]][1], "input identity differs")
    components = {}
    for component in data["components"]:
        name = component["id"]
        require(isinstance(name, str) and name and name not in components,
                "missing or duplicate component")
        require(component["state"] in {"pending", "reviewed", "blocked"}, "unsupported state")
        components[name] = component
    assignments = {name: set() for name in components}
    for row in paths:
        require((row["mode"], row["blob"]) == entries[row["path"]],
                f"blob or mode differs: {row['path']}")
        require(row["component"] in components, f"unknown component: {row['component']}")
        require(row["artifact_class"] in ARTIFACT_CLASSES, "unsupported artifact class")
        assignments[row["component"]].add(row["path"])
    for name, component in components.items():
        require(bool(assignments[name]), f"unassigned component: {name}")
        if component["state"] == "reviewed":
            verify_review(root, commit, component, assignments[name], entries)
        else:
            require(isinstance(component.get("next_action"), str) and
                    component["next_action"].strip(), f"{name}: missing next action")
            require(component.get("review_witness") is None, "open component has review witness")
    states = Counter(component["state"] for component in components.values())
    require(not require_reviewed or states["reviewed"] == len(components), "pending review")
    return {"baseline": commit, "classified_paths": len(paths), "components": dict(states)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--snapshot", type=Path,
                        default=Path("tools/audit/repository-coverage.json"))
    parser.add_argument("--initialize", metavar="REVISION")
    parser.add_argument("--require-reviewed", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    snapshot = args.snapshot if args.snapshot.is_absolute() else root / args.snapshot
    try:
        require(snapshot.resolve().is_relative_to(root), "snapshot must stay inside checkout")
        if args.initialize:
            require(not snapshot.exists(), "snapshot already exists; preserve the audit baseline")
            data = initialize(root, args.initialize)
            snapshot.parent.mkdir(parents=True, exist_ok=True)
            with snapshot.open("x", encoding="utf-8") as output:
                json.dump(data, output, indent=2)
                output.write("\n")
        data = load_json(snapshot)
        print(json.dumps(validate(root, data, args.require_reviewed), sort_keys=True))
    except (KeyError, TypeError, ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"repository coverage: FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
