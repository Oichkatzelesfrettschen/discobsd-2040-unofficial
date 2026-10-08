"""Two-oracle inventory of K&R function interfaces, checked against a ledger.

The compiler oracle compiles each requested source a kernel configuration
builds, with that configuration's evaluated CC, CFLAGS, INCLUDES and PARAM,
and records -Wold-style-definition and -Wstrict-prototypes diagnostics. It
parses every form the target compiler accepts but sees only active code. The
scan oracle (kr_scan.py) reads every conditional branch but recognizes only
the shapes its fixtures calibrate. Neither alone is complete: a parser that
cannot read "dev_t dev __unused;" in a declaration list misses a definition
the compiler reports, and a compiler misses a definition under an inactive
#ifdef.

The ledger is the union, keyed by path, kind and name so that edits elsewhere
in a file do not churn it. Each row names the oracles that found it. The gate
fails when a compiler finding is absent from the scan, which is a calibration
gap in kr_scan.py, when a requested source cannot be compiled, or when the
union differs from the committed ledger. --update rewrites the ledger.
"""

import argparse
import os
import re
import shlex
import subprocess
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import kr_scan  # noqa: E402

# The severity label is translated under a localized LANGUAGE or
# LC_MESSAGES, so only the position and the option tag are matched; a
# diagnostic that carries a -W option tag is a warning under -Wno-error.
DIAGNOSTIC = re.compile(
    r"^(?P<path>[^:\s]+):(?P<line>\d+):(?P<column>\d+): [^:\[]+: (?P<text>.*)"
    r"\[-W(?P<option>[\w-]+)(?:,[^\]]*)?\]\s*$"
)


def classify(option, text):
    """The finding a diagnostic reports, from its option name, or None.

    The option tag is the same in every locale, unlike the message: GCC
    under a UTF-8 locale spells "isn’t" with a typographic apostrophe. GCC
    reports an old-style definition under -Wold-style-definition and Clang
    under -Wdeprecated-non-prototype, whose other messages concern calls.
    """
    if option == "old-style-definition":
        return "old-style"
    if option == "deprecated-non-prototype":
        return "old-style" if "definition" in text else None
    if option == "strict-prototypes":
        return "not-prototype"
    return None


def make_value(make, build, name):
    env = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS")}
    result = subprocess.run(
        [make, "-C", str(build), "-V", "${" + name + "}"],
        env=env,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def compiler_findings(make, root, build, requested):
    """Map (path, line) to kind for one configuration's diagnostics."""
    env = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS")}
    subprocess.run(
        [make, "-C", str(build), "machine", "sys"],
        env=env,
        check=True,
        capture_output=True,
        text=True,
    )
    compiler = shlex.split(make_value(make, build, "CC"))
    flags = []
    for name in ("CFLAGS", "INCLUDES", "PARAM"):
        flags += shlex.split(make_value(make, build, name))
    built = set()
    for item in shlex.split(make_value(make, build, "CFILES")):
        built.add(os.path.relpath((build / item).resolve(), root))
    found = {}
    compiled = []
    for source in sorted(requested & built):
        result = subprocess.run(
            compiler
            + flags
            + [
                "-DKERNEL",
                "-Wno-error",
                "-Wold-style-definition",
                "-Wstrict-prototypes",
                "-fsyntax-only",
                str(root / source),
            ],
            cwd=build,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            sys.stderr.write(result.stderr)
            raise SystemExit(f"FAIL compiler oracle cannot compile {source} for {build.name}")
        compiled.append(source)
        # Each translation unit reports a header it includes again; a source
        # position counts once per configuration, at its largest per-unit count.
        unit = {}
        parse_diagnostics(result.stderr, build, root, unit)
        for key, counts in unit.items():
            found[key] = found.get(key, Counter()) | counts
    return {key: interfaces(counts) for key, counts in found.items()}, compiled


def parse_diagnostics(stderr, build, root, found):
    """Add each K&R diagnostic in stderr to found, keyed by source position."""
    for line in stderr.splitlines():
        match = DIAGNOSTIC.match(line)
        if not match:
            continue
        path = os.path.relpath((build / match["path"]).resolve(), root)
        key = (path, int(match["line"]), int(match["column"]))
        kind = classify(match["option"], match["text"])
        if kind:
            found.setdefault(key, Counter())[kind] += 1


def interfaces(counts):
    """Kinds and counts the diagnostics at one source position denote.

    GCC reports an old-style definition twice at the same position, once as
    old-style and once as not a prototype, and reports each declarator of a
    multi-declarator declaration at the declaration's first column.
    """
    definitions = counts["old-style"]
    declarations = max(counts["not-prototype"] - definitions, 0)
    return Counter({"definition": definitions, "declaration": declarations}) + Counter()


HEADER = (
    "# K&R function-interface ledger: path kind name oracle",
    "# oracle: both = scan and compiler; scan = inactive code or a file no",
    "# configuration compiles. Regenerate with c17_inventory.py --update.",
)


def reconcile(scanned, compiler):
    """Return (gaps, rows): compiler findings the scan lacks, and ledger rows.

    scanned maps (path, line) to a list of (kind, name), one entry per
    interface, so two declarators on one line stay two findings. compiler
    maps (path, line, column) to a Counter of kinds (see interfaces()). A
    line where the compiler reports more definitions, or more definitions
    and declarations together, than the scan is a gap. Clang reports an
    empty-parenthesis definition only as a declaration without a prototype,
    so a surplus scan definition may account for a compiler declaration.
    """
    compiled = {}
    for (path, line, _column), counts in compiler.items():
        compiled[(path, line)] = compiled.get((path, line), Counter()) + counts
    gaps = []
    for (path, line), counts in compiled.items():
        found = Counter(kind for kind, _ in scanned.get((path, line), ()))
        if counts["definition"] > found["definition"]:
            gaps.append(
                f"{path}:{line}: compiler reports {counts['definition']} definition, "
                f"scan {found['definition']}"
            )
        elif counts["declaration"] > found["declaration"] + (
            found["definition"] - counts["definition"]
        ):
            gaps.append(
                f"{path}:{line}: compiler reports {counts['declaration']} declaration, "
                f"scan {found['declaration']}"
            )
    rows = Counter()
    for (path, line), entries in scanned.items():
        counts = compiled.get((path, line), Counter())
        for kind, name in entries:
            seen = counts[kind] or (kind == "definition" and counts["declaration"])
            oracle = "both" if seen else "scan"
            rows[f"{path} {kind} {name} {oracle}"] += 1
    return sorted(gaps), dict(rows)


def render(rows):
    lines = [f"{row} x{count}" if count > 1 else row for row, count in sorted(rows.items())]
    return "\n".join(HEADER + tuple(lines)) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--make", default=os.environ.get("MAKE", "bmake"))
    parser.add_argument("--build", action="append", default=[], required=True)
    parser.add_argument("--ledger", required=True)
    parser.add_argument("--update", action="store_true")
    parser.add_argument("sources", nargs="+")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    requested = {os.path.relpath(Path(s).resolve(), root) for s in args.sources}

    scanned = {}
    for source in sorted(requested):
        for finding in kr_scan.scan_file(str(root / source)):
            scanned.setdefault((source, finding.line), []).append((finding.kind, finding.name))

    compiled_by = {}
    compiler = {}
    for build_dir in args.build:
        build = (root / build_dir).resolve()
        found, compiled = compiler_findings(args.make, root, build, requested)
        for source in compiled:
            compiled_by.setdefault(source, []).append(build.name)
        for key, counts in found.items():
            if key[0] in requested:
                compiler[key] = compiler.get(key, Counter()) | counts

    gaps, rows = reconcile(scanned, compiler)
    if gaps:
        print("FAIL kr_scan misses compiler findings (calibration gap):", file=sys.stderr)
        for gap in gaps:
            print("  " + gap, file=sys.stderr)
        return 1

    ledger = Path(args.ledger)
    text = render(rows)
    definitions = sum(c for r, c in rows.items() if " definition " in r)
    declarations = sum(c for r, c in rows.items() if " declaration " in r)
    files = len({r.split()[0] for r in rows})
    summary = (
        f"{definitions} definitions, {declarations} declarations in {files} files; "
        f"compiler oracle covered {len(compiled_by)} of {len(requested)} sources"
    )
    if args.update:
        ledger.write_text(text)
        print(f"ledger updated: {summary}")
        return 0
    current = ledger.read_text() if ledger.exists() else ""
    if current != text:
        old = set(current.splitlines())
        new = set(text.splitlines())
        print("FAIL ledger differs from the two-oracle inventory:", file=sys.stderr)
        for line in sorted(old - new):
            print("  - " + line, file=sys.stderr)
        for line in sorted(new - old):
            print("  + " + line, file=sys.stderr)
        return 1
    print(f"PASS c17 inventory: {summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
