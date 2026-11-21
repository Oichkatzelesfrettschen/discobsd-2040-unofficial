"""Calibration of tools/check_changed_text.py on throwaway repositories.

Each case commits a base and a head revision and requires the checker's exit
status and finding codes: a clean range passes, and a range that adds each
rejected form fails naming its rule.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

CHECKER = Path(__file__).resolve().parent / "check_changed_text.py"


def git(root, *args):
    subprocess.run(
        ["git", "-C", str(root), *args],
        check=True,
        capture_output=True,
        text=True,
    )


def run_case(files, base_files=None):
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        git(root, "init", "-q")
        git(root, "config", "user.email", "test@example.invalid")
        git(root, "config", "user.name", "test")
        (root / "base.md").write_text("Base text.\n")
        for name, text in (base_files or {}).items():
            (root / name).write_text(text)
        git(root, "add", ".")
        git(root, "commit", "-q", "-m", "base")
        for name, text in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        git(root, "add", ".")
        git(root, "commit", "-q", "-m", "head")
        result = subprocess.run(
            [sys.executable, str(CHECKER), "--root", str(root),
             "--base", "HEAD~1", "--revision", "HEAD"],
            capture_output=True,
            text=True,
            check=False,
        )
        return result.returncode, result.stdout


CASES = (
    ("clean prose and code", {"a.md": "The color table.\n", "b.c": "int x;\n"}, 0, ()),
    ("upstream name in a code span", {"a.md": "The `LICENCE` file.\n"}, 0, ()),
    ("upstream message in quotes", {"a.md": 'It prints "not initialised".\n'}, 0, ()),
    ("copy review quotes upstream",
     {"docs/research/copy-reviews/r.md": "colour table\n"}, 0, ()),
    ("British word in a new fenced block",
     {"a.md": "Text.\n\n```c\nint colour;\n```\n"}, 0, ()),
    ("British word after a closed fence",
     {"a.md": "```\ncode\n```\nThe colour table.\n"}, 1, ("TX002_BRITISH_SPELLING a.md:4",)),
    ("British spelling in prose", {"a.md": "The colour table.\n"}, 1,
     ("TX002_BRITISH_SPELLING a.md:1",)),
    ("British spelling, capitalized", {"a.md": "Behaviour is fixed.\n"}, 1,
     ("TX002_BRITISH_SPELLING a.md:1",)),
    ("trailing blank", {"b.c": "int x; \n"}, 1, ("TX001_WHITESPACE",)),
    (
        "patch context line",
        {"p/0001-fix.patch": "--- a/x\n+++ b/x\n@@ -1 +1 @@\n \n-a\n+b\n"},
        0,
        (),
    ),
    ("space before tab", {"a.md": "text\n \tindented\n"}, 1, ("TX001_WHITESPACE",)),
    (
        "imported file keeps upstream bytes",
        {
            "tools/netbsd-imports.manifest": "revision 0\n" + "0" * 64 + "  up/x.c\n",
            "up/x.c": "int x; \n",
        },
        0,
        (),
    ),
    (
        "patched imported file keeps upstream bytes",
        {
            "tools/netbsd-imports.manifest": "revision 0\n" + "0" * 64
            + "  up/p.c  patched " + "1" * 64 + " tools/netbsd-patches/p.patch\n",
            "up/p.c": "int p; \n",
        },
        0,
        (),
    ),
    (
        "unlisted file beside an imported one",
        {
            "tools/netbsd-imports.manifest": "revision 0\n" + "0" * 64 + "  up/x.c\n",
            "up/y.c": "int y; \n",
        },
        1,
        ("TX001_WHITESPACE up/y.c",),
    ),
)


# Determiner negation: a phrase the base already holds stays out of the verdict.
DETERMINER_CASES = (
    ("determiner added to prose", {"a.md": "The image carries no V6 pack.\n"}, 1,
     ("TX003_DETERMINER_NEGATION a.md:1",), None),
    ("determiner split across a line break",
     {"a.md": "Text.\nApple's Python has\nno pyserial.\n"}, 1,
     ("TX003_DETERMINER_NEGATION a.md:3",), None),
    ("determiner in a code span and a fence",
     {"a.md": "The `returns nothing` stub.\n\n```\nit has no flag\n```\n"}, 0, (), None),
    ("bound and preposition stay", {"a.md": "It is no longer valid with no board.\n"}, 0, (),
     None),
    ("inherited phrase kept beside an edit",
     {"a.md": "The board carries no V6 pack.\nA new line.\n"}, 0, (),
     {"a.md": "The board carries no V6 pack.\n"}),
    ("inherited phrase moved within the file",
     {"a.md": "Top.\nThe board carries no V6 pack.\n"}, 0, (),
     {"a.md": "The board carries no V6 pack.\nTop.\n"}),
    ("copy added ahead of an inherited phrase",
     {"a.md": "It prints nothing.\nText.\nIt prints nothing.\n"}, 1,
     ("TX003_DETERMINER_NEGATION a.md:1",), {"a.md": "Text.\nIt prints nothing.\n"}),
    ("heading verb above a paragraph that opens with No",
     {"a.md": "## Not run\n\nNo board execution.\n"}, 0, (), None),
    ("capitalized determiner", {"a.md": "## The Handler Returns Nothing\n"}, 1,
     ("TX003_DETERMINER_NEGATION a.md:1",), None),
    ("second copy of an inherited phrase",
     {"a.md": "It prints nothing.\nThen it prints nothing.\n"}, 1,
     ("TX003_DETERMINER_NEGATION a.md:2",), {"a.md": "It prints nothing.\n"}),
    ("positive rewrite of an inherited phrase",
     {"a.md": "The board omits the V6 pack.\n"}, 0, (),
     {"a.md": "The board carries no V6 pack.\n"}),
    ("copy review keeps the reviewed wording",
     {"docs/research/copy-reviews/r.md": "It has no effect.\n"}, 0, (), None),
)


# An edit inside a fence the base already holds: the diff carries no fence line.
EDIT_IN_FENCE = (
    "British word edited inside an existing fence",
    {"a.md": "Text.\n\n```\nint colour = 2;\n```\n"},
    0,
    (),
    {"a.md": "Text.\n\n```\nint color = 1;\n```\n"},
)


def main():
    failures = 0
    cases = (*CASES, EDIT_IN_FENCE, *DETERMINER_CASES)
    for label, files, status, codes, *base in cases:
        got, out = run_case(files, base[0] if base else None)
        missing = [code for code in codes if code not in out]
        if got != status or missing:
            failures += 1
            print(f"FAIL {label}: status {got} (want {status}), missing {missing}\n{out}")
        else:
            print(f"ok {label}")
    if failures:
        print(f"changed_text_test: {failures} of {len(cases)} cases failed")
        return 1
    print(f"changed_text_test: {len(cases)} cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
