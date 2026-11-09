"""Check the text a revision range adds for whitespace errors and British spellings.

Two rules apply to the lines that BASE..HEAD adds:

- TX001_WHITESPACE: `git diff --check` reports a whitespace error (trailing
  blanks, a space before a tab in the indent, a blank line at end of file).
- TX002_BRITISH_SPELLING: an added prose line of a Markdown file uses a word
  from BRITISH, outside a fenced code block, a backtick code span and a
  double-quoted string.
  The tree spells American English; an upstream identifier, filename or
  message keeps its owner's spelling inside a code span or quotation.

Markdown under docs/research/copy-reviews is exempt from the spelling rule:
those files quote the reviewed sources verbatim. Patch files (*.patch,
*.diff) are exempt from the whitespace rule: a unified diff's blank context
line is a single space, and the patch must apply byte for byte. Files that
tools/netbsd-imports.manifest lists at HEAD are exempt from both rules: they
hold NetBSD's bytes verbatim, and check-netbsd-imports binds each to its
recorded SHA-256.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXEMPT_PREFIXES = ("docs/research/copy-reviews/",)
PATCH_EXCLUDES = (":(exclude)*.patch", ":(exclude)*.diff")
IMPORT_MANIFEST = "tools/netbsd-imports.manifest"
BRITISH = (
    "analyse", "analysed", "analysing", "artefact", "artefacts", "behaviour",
    "behaviours", "catalogue", "catalogues", "centre", "centred", "centres",
    "colour", "coloured", "colours", "favour", "favours", "flavour", "grey",
    "honour", "initialise", "initialised", "labelled", "labelling", "licence",
    "licences", "modelled", "modelling", "neighbour", "neighbours", "normalise",
    "normalised", "optimise", "optimised", "organise", "organised", "recognise",
    "recognised", "serialise", "serialised", "travelled",
)
WORD = re.compile(r"\b(" + "|".join(BRITISH) + r")\b", re.IGNORECASE)
QUOTED = re.compile(r"`[^`]*`|\"[^\"]*\"")
HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@")


class InfrastructureError(Exception):
    """Git cannot supply the revision range."""


def git(root, *args):
    result = subprocess.run(
        ["git", "-C", str(root), *args],
        capture_output=True,
        text=True,
        check=False,
    )
    return result


def imported_paths(root, head):
    """Return the paths HEAD's import manifest pins, or none without one."""
    result = git(root, "show", f"{head}:{IMPORT_MANIFEST}")
    if result.returncode:
        return frozenset()
    paths = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) == 2 and len(fields[0]) == 64:
            paths.add(fields[1])
    return frozenset(paths)


def whitespace_findings(root, base, head):
    excludes = [f":(exclude,literal){path}" for path in sorted(imported_paths(root, head))]
    result = git(root, "diff", "--check", base, head, "--", ".", *PATCH_EXCLUDES, *excludes)
    if result.returncode not in (0, 2):
        raise InfrastructureError(result.stderr.strip() or "git diff --check failed")
    findings = []
    for line in result.stdout.splitlines():
        if line.startswith(("+", "-")) or not line.strip():
            continue
        findings.append(f"TX001_WHITESPACE {line}")
    return findings


def added_markdown_lines(root, base, head):
    result = git(root, "diff", "--unified=0", "--no-color", base, head, "--", "*.md")
    if result.returncode != 0:
        raise InfrastructureError(result.stderr.strip() or "git diff failed")
    path = None
    number = 0
    for line in result.stdout.splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
            continue
        match = HUNK.match(line)
        if match:
            number = int(match.group(1))
            continue
        if path is None or line.startswith(("---", "-", "\\")):
            continue
        if line.startswith("+"):
            yield path, number, line[1:]
            number += 1


FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})")


def fenced_lines(root, head, path):
    """Line numbers of path at head that lie inside a fenced code block.

    The zero-context diff omits an unchanged fence, so membership comes from
    the whole file at the revision, not from the added lines alone.
    """
    result = git(root, "show", f"{head}:{path}")
    if result.returncode != 0:
        raise InfrastructureError(result.stderr.strip() or f"cannot read {path}")
    inside = set()
    opener = None
    for number, line in enumerate(result.stdout.splitlines(), start=1):
        match = FENCE.match(line)
        if opener is None:
            if match:
                opener = match.group(1)
                inside.add(number)
        else:
            inside.add(number)
            if match and match.group(1)[0] == opener[0] and len(match.group(1)) >= len(opener):
                opener = None
    return inside


def spelling_findings(root, base, head):
    findings = []
    fences = {}
    imported = imported_paths(root, head)
    for path, number, text in added_markdown_lines(root, base, head):
        if path.startswith(EXEMPT_PREFIXES) or path in imported:
            continue
        if path not in fences:
            fences[path] = fenced_lines(root, head, path)
        if number in fences[path]:
            continue
        for match in WORD.finditer(QUOTED.sub("", text)):
            findings.append(
                f"TX002_BRITISH_SPELLING {path}:{number}: {match.group(1)!r}"
            )
    return findings


def check(root, base, head):
    for revision in (base, head):
        if git(root, "rev-parse", "--verify", f"{revision}^{{commit}}").returncode:
            raise InfrastructureError(f"{revision} does not name a commit")
    return whitespace_findings(root, base, head) + spelling_findings(root, base, head)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--base", required=True)
    parser.add_argument("--revision", required=True)
    args = parser.parse_args()
    try:
        findings = check(args.root, args.base, args.revision)
    except InfrastructureError as error:
        print(f"check-changed-text: {error}", file=sys.stderr)
        return 2
    for finding in findings:
        print(finding)
    if findings:
        print(f"check-changed-text: {len(findings)} finding(s)", file=sys.stderr)
        return 1
    print(f"check-changed-text: {args.base}..{args.revision} clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
