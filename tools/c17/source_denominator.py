"""Verify the finite Git-tracked C and header source denominator."""

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path, PurePosixPath

CLASSES = {"A", "B", "C", "D", "E", "unreviewed"}
SCOPES = {"maintained", "quarantine", "archive", "generated", "unreviewed"}
PROFILES = {
    "rp2040-kernel-port",
    "modernized-cross-userland",
    "historical-cross-userland",
    "native-smaller-c-implementation",
    "quarantined-historical-source",
    "unreviewed",
}
NATIVE_INPUT = {"yes", "no", "unreviewed"}
LANGUAGE_STATES = {"unreviewed", "migration-open", "accepted", "excluded"}
REQUIRED_ROW_FIELDS = {
    "path",
    "class",
    "scope",
    "owner",
    "profile",
    "native_input",
    "language_state",
    "evidence",
    "blocker",
}
REQUIRED_QUARANTINE_FIELDS = {
    "path",
    "scope",
    "sha256",
    "provenance",
    "reason",
}


class DenominatorError(Exception):
    """A denominator invariant failed."""


def require(condition, message):
    if not condition:
        raise DenominatorError(message)


def valid_path(path):
    pure = PurePosixPath(path)
    return bool(path) and not pure.is_absolute() and ".." not in pure.parts


def tracked_sources(root):
    result = subprocess.run(
        ["git", "-c", "core.fsmonitor=false", "ls-files", "-z"],
        cwd=root,
        check=True,
        capture_output=True,
    )
    return sorted(
        path.decode("utf-8")
        for path in result.stdout.split(b"\0")
        if path and path.endswith((b".c", b".h"))
    )


def file_sha256(root, relative_path):
    digest = hashlib.sha256()
    with (root / relative_path).open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise DenominatorError(f"cannot read {path}: {error}") from error


def validate_documents(root, discovered, ledger, quarantine, closure=False):
    require(ledger.get("schema_version") == 1, "unsupported ledger schema")
    require(quarantine.get("schema_version") == 1, "unsupported quarantine schema")
    rows = ledger.get("rows")
    entries = quarantine.get("entries")
    require(isinstance(rows, list), "ledger rows must be a list")
    require(isinstance(entries, list), "quarantine entries must be a list")

    rows_by_path = {}
    counts = {state: 0 for state in LANGUAGE_STATES}
    for row in rows:
        require(isinstance(row, dict), "each ledger row must be an object")
        require(set(row) == REQUIRED_ROW_FIELDS, f"malformed ledger row: {row!r}")
        path = row["path"]
        require(isinstance(path, str) and valid_path(path), f"unsafe ledger path: {path!r}")
        require(path.endswith((".c", ".h")), f"non-C denominator path: {path}")
        require(path not in rows_by_path, f"duplicate ledger path: {path}")
        require(row["class"] in CLASSES, f"unknown class for {path}")
        require(row["scope"] in SCOPES, f"unknown scope for {path}")
        require(row["profile"] in PROFILES, f"unknown profile for {path}")
        require(row["native_input"] in NATIVE_INPUT, f"unknown native-input state for {path}")
        require(row["language_state"] in LANGUAGE_STATES, f"unknown language state for {path}")
        require(isinstance(row["owner"], str) and row["owner"], f"missing owner for {path}")
        require(isinstance(row["blocker"], str), f"invalid blocker for {path}")
        require(isinstance(row["evidence"], list), f"invalid evidence for {path}")
        for evidence in row["evidence"]:
            require(
                isinstance(evidence, str) and valid_path(evidence),
                f"unsafe evidence path for {path}",
            )
            require((root / evidence).is_file(), f"missing evidence for {path}: {evidence}")
        if row["language_state"] == "accepted":
            require(row["class"] != "unreviewed", f"accepted row has no class: {path}")
            require(row["scope"] != "unreviewed", f"accepted row has no scope: {path}")
            require(row["profile"] != "unreviewed", f"accepted row has no profile: {path}")
            require(
                row["native_input"] != "unreviewed", f"accepted row has no native decision: {path}"
            )
            require(row["evidence"], f"accepted row has no evidence: {path}")
            require(not row["blocker"], f"accepted row retains a blocker: {path}")
        if row["language_state"] == "excluded":
            require(row["class"] == "D", f"only class D can be excluded: {path}")
            require(
                row["scope"] in {"quarantine", "archive"}, f"excluded row lacks boundary: {path}"
            )
        counts[row["language_state"]] += 1
        rows_by_path[path] = row

    discovered_set = set(discovered)
    row_set = set(rows_by_path)
    missing = sorted(discovered_set - row_set)
    extra = sorted(row_set - discovered_set)
    require(not missing, "tracked paths absent from denominator: " + ", ".join(missing[:8]))
    require(not extra, "untracked paths present in denominator: " + ", ".join(extra[:8]))
    require(list(rows_by_path) == sorted(rows_by_path), "ledger rows are not path-sorted")

    quarantine_by_path = {}
    for entry in entries:
        require(isinstance(entry, dict), "each quarantine entry must be an object")
        require(set(entry) == REQUIRED_QUARANTINE_FIELDS, f"malformed quarantine entry: {entry!r}")
        path = entry["path"]
        require(isinstance(path, str) and valid_path(path), f"unsafe quarantine path: {path!r}")
        require(path not in quarantine_by_path, f"duplicate quarantine path: {path}")
        require(entry["scope"] in {"quarantine", "archive"}, f"invalid quarantine scope: {path}")
        require(path in rows_by_path, f"quarantine path absent from denominator: {path}")
        row = rows_by_path[path]
        require(row["language_state"] == "excluded", f"quarantine path is not excluded: {path}")
        require(row["scope"] == entry["scope"], f"scope mismatch for {path}")
        require(len(entry["sha256"]) == 64, f"invalid quarantine hash: {path}")
        require(file_sha256(root, path) == entry["sha256"], f"quarantine bytes changed: {path}")
        require(entry["provenance"] and entry["reason"], f"incomplete quarantine record: {path}")
        quarantine_by_path[path] = entry
    excluded = {path for path, row in rows_by_path.items() if row["language_state"] == "excluded"}
    require(excluded == set(quarantine_by_path), "excluded rows and quarantine manifest differ")
    require(
        list(quarantine_by_path) == sorted(quarantine_by_path),
        "quarantine entries are not path-sorted",
    )

    if closure:
        require(counts["unreviewed"] == 0, "closure rejected: unreviewed source rows remain")
        require(counts["migration-open"] == 0, "closure rejected: open migrations remain")
    return counts


def initial_row(path):
    if path.startswith("legacy/non-arm/"):
        return {
            "path": path,
            "class": "D",
            "scope": "archive",
            "owner": "legacy/non-arm",
            "profile": "quarantined-historical-source",
            "native_input": "no",
            "language_state": "excluded",
            "evidence": ["docs/research/c17-formatting-linting.md"],
            "blocker": "",
        }
    if path.startswith("legacy/pdp11-v6/usr.bin/pdp11/"):
        return {
            "path": path,
            "class": "D",
            "scope": "quarantine",
            "owner": "legacy/pdp11-v6/usr.bin/pdp11",
            "profile": "quarantined-historical-source",
            "native_input": "no",
            "language_state": "excluded",
            "evidence": ["docs/research/c17-formatting-linting.md"],
            "blocker": "",
        }
    if path.startswith("usr.bin/smlrc/"):
        return {
            "path": path,
            "class": "B",
            "scope": "maintained",
            "owner": "usr.bin/smlrc",
            "profile": "native-smaller-c-implementation",
            "native_input": "yes",
            "language_state": "migration-open",
            "evidence": ["docs/research/STYLE-GUIDE.md"],
            "blocker": "File-level C17 and native-suite acceptance remain unreviewed.",
        }
    owner = path.split("/", 1)[0]
    return {
        "path": path,
        "class": "unreviewed",
        "scope": "unreviewed",
        "owner": owner,
        "profile": "unreviewed",
        "native_input": "unreviewed",
        "language_state": "unreviewed",
        "evidence": [],
        "blocker": (
            "Class, evaluated production profile and migration-unit evidence "
            "require review."
        ),
    }


def bootstrap(root, ledger_path, quarantine_path):
    require(not ledger_path.exists(), f"refusing to replace existing ledger: {ledger_path}")
    require(
        not quarantine_path.exists(), f"refusing to replace existing manifest: {quarantine_path}"
    )
    rows = [initial_row(path) for path in tracked_sources(root)]
    entries = []
    for row in rows:
        if row["language_state"] != "excluded":
            continue
        entries.append(
            {
                "path": row["path"],
                "scope": row["scope"],
                "sha256": file_sha256(root, row["path"]),
                "provenance": "docs/research/c17-formatting-linting.md",
                "reason": "Named historical source boundary outside the maintained ARM migration.",
            }
        )
    ledger_path.write_text(
        json.dumps({"schema_version": 1, "rows": rows}, indent=2) + "\n", encoding="utf-8"
    )
    quarantine_path.write_text(
        json.dumps({"schema_version": 1, "entries": entries}, indent=2) + "\n",
        encoding="utf-8",
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--ledger", default="tools/c17/source-denominator.json")
    parser.add_argument("--quarantine", default="tools/c17/quarantine-manifest.json")
    parser.add_argument("--bootstrap", action="store_true")
    parser.add_argument("--closure", action="store_true")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    ledger_path = root / args.ledger
    quarantine_path = root / args.quarantine
    try:
        if args.bootstrap:
            bootstrap(root, ledger_path, quarantine_path)
        counts = validate_documents(
            root,
            tracked_sources(root),
            load_json(ledger_path),
            load_json(quarantine_path),
            args.closure,
        )
    except (DenominatorError, subprocess.CalledProcessError) as error:
        print(f"FAIL C17 source denominator: {error}", file=sys.stderr)
        return 1
    print(
        "PASS C17 source denominator accounted: "
        + ", ".join(f"{state}={counts[state]}" for state in sorted(counts))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
