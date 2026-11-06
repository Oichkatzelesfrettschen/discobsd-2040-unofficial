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


def run_case(files):
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        git(root, "init", "-q")
        git(root, "config", "user.email", "test@example.invalid")
        git(root, "config", "user.name", "test")
        (root / "base.md").write_text("Base text.\n")
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
)


def main():
    failures = 0
    for label, files, status, codes in CASES:
        got, out = run_case(files)
        missing = [code for code in codes if code not in out]
        if got != status or missing:
            failures += 1
            print(f"FAIL {label}: status {got} (want {status}), missing {missing}\n{out}")
        else:
            print(f"ok {label}")
    if failures:
        print(f"changed_text_test: {failures} of {len(CASES)} cases failed")
        return 1
    print(f"changed_text_test: {len(CASES)} cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
