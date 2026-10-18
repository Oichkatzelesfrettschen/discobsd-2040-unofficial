"""Verify the finite Git-tracked C and header source denominator."""

import argparse
import hashlib
import json
import os
import re
import stat
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath

import kr_scan

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
COUNT_REPORT = Path("docs/research/c17-semantic-migration-plan.md")
COUNT_REPORT_PATTERN = re.compile(
    r"Those classifications preserve all (?P<accepted>[\d,]+) accepted,\s+"
    r"(?P<migration_open>[\d,]+) migration-open,\s+"
    r"(?P<excluded>[\d,]+)\s+excluded and (?P<unreviewed>[\d,]+) "
    r"unreviewed language states\."
)
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
    "classification",
}
REQUIRED_QUARANTINE_FIELDS = {
    "path",
    "scope",
    "sha256",
    "provenance",
    "reason",
}
CLASSIFICATION_FIELDS = {
    "source_kind",
    "build_surface",
    "required_mode",
    "extension_policy",
    "extension_signals",
    "risk_family",
    "risk_tier",
    "risk_tier_basis",
    "hazard_basis",
    "hazards",
}
HAZARD_FAMILIES = {
    "integer",
    "pointer",
    "prototype",
    "variadic",
    "aliasing",
    "object_lifetime",
    "inline_assembly",
}
BUILD_SURFACES = {
    "rp2040-kernel",
    "rp2040-userland",
    "host-only",
    "test-only",
    "host-test",
    "rp2040-board-probe",
    "native-smaller-c",
    "maintained-arm",
    "legacy-isolated",
    "unresolved",
}
REQUIRED_MODES = {
    "strict-c17-and-gnu17",
    "gnu17-freestanding",
    "c17-migration",
    "gnu17-hosted",
    "pico-sdk-c11",
    "not-built",
    "unresolved",
}
EXTENSION_POLICIES = {
    "portable-only",
    "localized-gnu",
    "gnu-extension-review",
    "native-subset-separated",
    "pico-sdk-boundary",
    "not-applicable",
    "unresolved",
}
RISK_FAMILIES = {
    "portable-leaf",
    "libc-public-header",
    "shell-ilp32",
    "kernel-syscall",
    "hardware-seam",
    "native-compiler",
    "host-only",
    "host-test",
    "legacy-isolated",
    "unresolved",
}
RISK_TIERS = {"T1", "T2", "T3", "Q", "unreviewed"}
RISK_TIER_BASES = {
    "boundary-family-conservative",
    "caller-closure-required",
    "native-compiler-input",
    "quarantine",
}
EXTENSION_PATTERNS = {
    "gnu-attribute": re.compile(r"\b__attribute__\s*\("),
    "gnu-typeof": re.compile(r"\b(?:__typeof__|typeof)\s*\("),
    "inline-assembly": re.compile(
        r"\b(?:__asm__|__asm|asm)\s*"
        r"(?:(?:volatile|__volatile(?:__)?)\s*)?\("
    ),
    "statement-expression": re.compile(r"\(\s*\{"),
    "target-builtin": re.compile(r"\b__builtin_[A-Za-z0-9_]+"),
}

DEFINE_PREFIX = re.compile(
    r"(?m)^(?P<prefix>[ \t]*\#[ \t]*define[ \t]+[A-Za-z_]\w*"
    r"(?:\([^\n]*?\))?)"
)
HAZARD_PATTERNS = {
    "integer": {
        "integer-cast": re.compile(
            r"\(\s*(?:(?:un)?signed\s+)?(?:char|short|int|long)(?:\s+long)?\s*\)"
        ),
        "shift-expression": re.compile(r"(?:<<|>>)"),
        "width-sensitive-sizeof": re.compile(r"\bsizeof\s*\([^)]*\)\s*[*+\-/]"),
    },
    "pointer": {
        "integer-pointer-cast": re.compile(
            r"\(\s*(?:u?intptr_t|u?int(?:8|16|32|64)_t|long|int)\s*\)\s*[A-Za-z_(]"
        ),
        "pointer-integer-cast": re.compile(
            r"\(\s*(?:const\s+)?(?:void|char|short|int|long|[A-Za-z_]\w*)\s*\*\s*\)\s*[-+0-9]"
        ),
    },
    "variadic": {
        "variadic-interface": re.compile(r"\.\.\.|\bva_(?:list|start|arg|copy|end)\b"),
    },
    "aliasing": {
        "union-access": re.compile(r"\bunion\s+[A-Za-z_]"),
        "cast-dereference": re.compile(r"\*\s*\([^)]*\*[^)]*\)"),
    },
    "object_lifetime": {
        "dynamic-storage": re.compile(r"\b(?:malloc|calloc|realloc|free|alloca)\s*\("),
        "compound-literal": re.compile(
            r"\(\s*(?:(?:struct|union|enum)\s+[A-Za-z_]\w*|"
            r"(?:const\s+)?(?:char|short|int|long|float|double|[A-Za-z_]\w*_t)"
            r"(?:\s*\*)?)\s*\)\s*\{"
        ),
    },
    "inline_assembly": {
        "inline-assembly": EXTENSION_PATTERNS["inline-assembly"],
    },
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


def atomic_write_text(
    path,
    text,
    *,
    temporary_file_factory=tempfile.NamedTemporaryFile,
    replace_file=os.replace,
):
    """Durably stage text beside path and atomically replace the destination."""
    temporary_path = None
    try:
        with temporary_file_factory(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary_file:
            temporary_path = Path(temporary_file.name)
            if path.exists():
                existing_mode = stat.S_IMODE(path.stat().st_mode)
                os.fchmod(temporary_file.fileno(), existing_mode)
            temporary_file.write(text)
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        replace_file(temporary_path, path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            try:
                temporary_path.unlink()
            except FileNotFoundError:
                pass


def source_text(root, path):
    try:
        return (root / path).read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise DenominatorError(f"cannot classify {path}: {error}") from error


def lexical_candidate_text(text):
    """Blank comments and literals while retaining macro replacement tokens."""
    joined = kr_scan.join_splices(text)

    def blank_define_prefix(match):
        return " " * len(match.group("prefix"))

    candidate_source = DEFINE_PREFIX.sub(blank_define_prefix, joined)
    return kr_scan.strip_source(candidate_source)


def path_classification(row):
    path = row["path"]
    if row["language_state"] == "excluded":
        return ("legacy-isolated", "not-built", "not-applicable", "legacy-isolated")
    if path.startswith("usr.bin/smlrc/"):
        return (
            "native-smaller-c",
            "c17-migration",
            "native-subset-separated",
            "native-compiler",
        )
    if path.startswith(("sys/arch/arm/", "sys/arch/stm32/")):
        return ("maintained-arm", "gnu17-freestanding", "localized-gnu", "hardware-seam")
    if path.startswith("legacy/unsupported-platforms/"):
        return ("legacy-isolated", "not-built", "not-applicable", "legacy-isolated")
    if path.startswith("sys/arch/rp2040/test/"):
        return ("host-test", "gnu17-hosted", "gnu-extension-review", "host-test")
    if path.startswith("sys/arch/rp2040/"):
        return ("rp2040-kernel", "gnu17-freestanding", "localized-gnu", "hardware-seam")
    if path.startswith(("sys/kern/", "sys/sys/", "sys/ufs/", "sys/dev/", "sys/net/")):
        return ("rp2040-kernel", "gnu17-freestanding", "localized-gnu", "kernel-syscall")
    if path.startswith("bin/sh/"):
        return ("rp2040-userland", "c17-migration", "gnu-extension-review", "shell-ilp32")
    if path.startswith(("lib/libc/", "include/")):
        return (
            "rp2040-userland",
            "strict-c17-and-gnu17",
            "portable-only",
            "libc-public-header",
        )
    if path.startswith(
        ("bin/", "sbin/", "usr.bin/", "usr.sbin/", "games/", "lib/", "libexec/", "share/")
    ):
        return (
            "rp2040-userland",
            "strict-c17-and-gnu17",
            "portable-only",
            "portable-leaf",
        )
    if path.startswith("tests/"):
        return ("test-only", "strict-c17-and-gnu17", "portable-only", "portable-leaf")
    if path.startswith("tools/flash-id/"):
        return ("rp2040-board-probe", "pico-sdk-c11", "pico-sdk-boundary", "hardware-seam")
    if path.startswith(("tools/", "docs/", "benchmarks/", "distrib/")):
        return ("host-only", "strict-c17-and-gnu17", "portable-only", "host-only")
    return ("unresolved", "unresolved", "unresolved", "unresolved")


def risk_tier_classification(risk_family):
    if risk_family == "legacy-isolated":
        return ("Q", "quarantine")
    if risk_family == "native-compiler":
        return ("T1", "native-compiler-input")
    if risk_family in {
        "hardware-seam",
        "kernel-syscall",
        "libc-public-header",
        "shell-ilp32",
    }:
        return ("T1", "boundary-family-conservative")
    return ("unreviewed", "caller-closure-required")


def derive_classification(root, row):
    path = row["path"]
    source_kind = "translation-unit" if path.endswith(".c") else "header"
    build_surface, required_mode, extension_policy, risk_family = path_classification(row)
    risk_tier, risk_tier_basis = risk_tier_classification(risk_family)
    hazards = {family: [] for family in sorted(HAZARD_FAMILIES)}
    if build_surface == "legacy-isolated":
        return {
            "source_kind": source_kind,
            "build_surface": build_surface,
            "required_mode": required_mode,
            "extension_policy": extension_policy,
            "extension_signals": [],
            "risk_family": risk_family,
            "risk_tier": risk_tier,
            "risk_tier_basis": risk_tier_basis,
            "hazard_basis": "not-applicable",
            "hazards": hazards,
        }

    text = source_text(root, path)
    stripped = lexical_candidate_text(text)
    extension_signals = sorted(
        name for name, pattern in EXTENSION_PATTERNS.items() if pattern.search(stripped)
    )
    for family, patterns in HAZARD_PATTERNS.items():
        hazards[family] = sorted(
            name for name, pattern in patterns.items() if pattern.search(stripped)
        )
    prototype_signals = {
        f"old-style-{finding.kind}"
        for finding in kr_scan.scan_text(path, text)
    }
    hazards["prototype"] = sorted(prototype_signals)
    if extension_signals and extension_policy == "portable-only":
        extension_policy = "gnu-extension-review"
    return {
        "source_kind": source_kind,
        "build_surface": build_surface,
        "required_mode": required_mode,
        "extension_policy": extension_policy,
        "extension_signals": extension_signals,
        "risk_family": risk_family,
        "risk_tier": risk_tier,
        "risk_tier_basis": risk_tier_basis,
        "hazard_basis": "lexical-candidates-v1",
        "hazards": hazards,
    }


def validate_classification(root, row):
    path = row["path"]
    classification = row["classification"]
    require(isinstance(classification, dict), f"invalid classification for {path}")
    require(
        set(classification) == CLASSIFICATION_FIELDS,
        f"malformed classification for {path}",
    )
    require(
        classification["source_kind"] in {"translation-unit", "header"},
        f"unknown source kind for {path}",
    )
    require(
        classification["build_surface"] in BUILD_SURFACES,
        f"unknown build surface for {path}",
    )
    require(
        classification["required_mode"] in REQUIRED_MODES,
        f"unknown required mode for {path}",
    )
    require(
        classification["extension_policy"] in EXTENSION_POLICIES,
        f"unknown extension policy for {path}",
    )
    require(
        classification["risk_family"] in RISK_FAMILIES,
        f"unknown risk family for {path}",
    )
    require(classification["risk_tier"] in RISK_TIERS, f"unknown risk tier for {path}")
    require(
        classification["risk_tier_basis"] in RISK_TIER_BASES,
        f"unknown risk tier basis for {path}",
    )
    require(
        classification["hazard_basis"] in {"lexical-candidates-v1", "not-applicable"},
        f"unknown hazard basis for {path}",
    )
    signals = classification["extension_signals"]
    require(isinstance(signals, list), f"invalid extension signals for {path}")
    require(signals == sorted(set(signals)), f"unsorted extension signals for {path}")
    require(
        set(signals) <= set(EXTENSION_PATTERNS),
        f"unknown extension signal for {path}",
    )
    hazards = classification["hazards"]
    require(isinstance(hazards, dict), f"invalid hazards for {path}")
    require(set(hazards) == HAZARD_FAMILIES, f"malformed hazards for {path}")
    allowed_hazards = {
        family: set(patterns) for family, patterns in HAZARD_PATTERNS.items()
    }
    allowed_hazards["prototype"] = {"old-style-definition", "old-style-declaration"}
    for family, family_signals in hazards.items():
        require(isinstance(family_signals, list), f"invalid {family} hazards for {path}")
        require(
            family_signals == sorted(set(family_signals)),
            f"unsorted {family} hazards for {path}",
        )
        require(
            set(family_signals) <= allowed_hazards[family],
            f"unknown {family} hazard for {path}",
        )
    require(
        classification == derive_classification(root, row),
        f"classification drift for {path}",
    )


def validate_documents(root, discovered, ledger, quarantine, closure=False):
    require(ledger.get("schema_version") == 2, "unsupported ledger schema")
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
        validate_classification(root, row)
        if row["language_state"] == "accepted":
            require(row["class"] != "unreviewed", f"accepted row has no class: {path}")
            require(row["scope"] != "unreviewed", f"accepted row has no scope: {path}")
            require(row["profile"] != "unreviewed", f"accepted row has no profile: {path}")
            require(
                row["native_input"] != "unreviewed", f"accepted row has no native decision: {path}"
            )
            require(row["evidence"], f"accepted row has no evidence: {path}")
            require(not row["blocker"], f"accepted row retains a blocker: {path}")
            require(
                row["classification"]["build_surface"] != "unresolved",
                f"accepted row has unresolved build surface: {path}",
            )
            require(
                row["classification"]["required_mode"] != "unresolved",
                f"accepted row has unresolved language mode: {path}",
            )
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


def validate_count_report(root, counts):
    report_path = root / COUNT_REPORT
    require(report_path.is_file(), f"missing denominator count report: {COUNT_REPORT}")
    report_text = report_path.read_text(encoding="utf-8")
    matches = list(COUNT_REPORT_PATTERN.finditer(report_text))
    require(len(matches) == 1, f"expected one denominator count report in {COUNT_REPORT}")
    match = matches[0]
    reported_counts = {
        "accepted": int(match.group("accepted").replace(",", "")),
        "migration-open": int(match.group("migration_open").replace(",", "")),
        "excluded": int(match.group("excluded").replace(",", "")),
        "unreviewed": int(match.group("unreviewed").replace(",", "")),
    }
    require(
        reported_counts == counts,
        f"denominator count report drift in {COUNT_REPORT}: "
        f"reported={reported_counts}, ledger={counts}",
    )


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
    for row in rows:
        row["classification"] = derive_classification(root, row)
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
    ledger = {"schema_version": 2, "rows": rows}
    quarantine = {"schema_version": 1, "entries": entries}
    discovered = [row["path"] for row in rows]
    counts = validate_documents(root, discovered, ledger, quarantine)
    validate_count_report(root, counts)
    ledger_path.write_text(json.dumps(ledger, indent=2) + "\n", encoding="utf-8")
    quarantine_path.write_text(json.dumps(quarantine, indent=2) + "\n", encoding="utf-8")
    return counts


def refresh_classification(root, ledger_path, discovered, quarantine, closure=False):
    ledger = load_json(ledger_path)
    require(ledger.get("schema_version") in {1, 2}, "unsupported ledger schema")
    rows = ledger.get("rows")
    require(isinstance(rows, list), "ledger rows must be a list")
    for row in rows:
        require(isinstance(row, dict), "each ledger row must be an object")
        row["classification"] = derive_classification(root, row)
    ledger["schema_version"] = 2
    counts = validate_documents(root, discovered, ledger, quarantine, closure)
    validate_count_report(root, counts)
    atomic_write_text(ledger_path, json.dumps(ledger, indent=2) + "\n")
    return counts


def sync_paths(
    root,
    ledger_path,
    discovered,
    quarantine,
    expected_added,
    expected_removed,
    closure=False,
):
    """Apply one explicitly declared tracked-source set transition."""
    ledger = load_json(ledger_path)
    require(ledger.get("schema_version") == 2, "unsupported ledger schema")
    rows = ledger.get("rows")
    require(isinstance(rows, list), "ledger rows must be a list")
    rows_by_path = {
        row.get("path"): row for row in rows if isinstance(row, dict)
    }
    require(len(rows_by_path) == len(rows), "duplicate or malformed ledger row")

    discovered_set = set(discovered)
    row_set = set(rows_by_path)
    actual_added = discovered_set - row_set
    actual_removed = row_set - discovered_set
    require(
        actual_added == set(expected_added),
        "tracked source additions differ from declared delta: "
        + ", ".join(sorted(actual_added)),
    )
    require(
        actual_removed == set(expected_removed),
        "tracked source removals differ from declared delta: "
        + ", ".join(sorted(actual_removed)),
    )

    retained_rows = [row for row in rows if row["path"] not in actual_removed]
    for row in retained_rows:
        row["classification"] = derive_classification(root, row)
    added_rows = []
    for path in sorted(actual_added):
        row = initial_row(path)
        row["classification"] = derive_classification(root, row)
        added_rows.append(row)
    ledger["rows"] = sorted(retained_rows + added_rows, key=lambda row: row["path"])
    counts = validate_documents(root, discovered, ledger, quarantine, closure)
    validate_count_report(root, counts)
    atomic_write_text(ledger_path, json.dumps(ledger, indent=2) + "\n")
    return counts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--ledger", default="tools/c17/source-denominator.json")
    parser.add_argument("--quarantine", default="tools/c17/quarantine-manifest.json")
    parser.add_argument("--bootstrap", action="store_true")
    parser.add_argument("--refresh-classification", action="store_true")
    parser.add_argument("--sync-paths", action="store_true")
    parser.add_argument("--add-path", action="append", default=[])
    parser.add_argument("--remove-path", action="append", default=[])
    parser.add_argument("--closure", action="store_true")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    ledger_path = root / args.ledger
    quarantine_path = root / args.quarantine
    try:
        require(
            sum((args.bootstrap, args.refresh_classification, args.sync_paths)) <= 1,
            "select only one ledger mutation mode",
        )
        if args.bootstrap:
            counts = bootstrap(root, ledger_path, quarantine_path)
        elif args.sync_paths:
            discovered = tracked_sources(root)
            quarantine = load_json(quarantine_path)
            counts = sync_paths(
                root,
                ledger_path,
                discovered,
                quarantine,
                args.add_path,
                args.remove_path,
                args.closure,
            )
        elif args.refresh_classification:
            discovered = tracked_sources(root)
            quarantine = load_json(quarantine_path)
            counts = refresh_classification(
                root, ledger_path, discovered, quarantine, args.closure
            )
        else:
            discovered = tracked_sources(root)
            quarantine = load_json(quarantine_path)
            counts = validate_documents(
                root, discovered, load_json(ledger_path), quarantine, args.closure
            )
            validate_count_report(root, counts)
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
