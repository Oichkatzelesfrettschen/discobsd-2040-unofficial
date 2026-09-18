#!/usr/bin/env python3
"""Inject a failure into each step of the production build recipes and
assert that the steps after it never run and the parent make fails.

lib/Makefile is copied into a scratch tree whose share/ is the real one,
so its install and clean loops run against a stub make that journals each
child and fails on request. A stub that is asked to fail says so on
stderr, and every negative case asserts that sentence, so a fixture that
never reached the intended step cannot pass.
"""

import argparse
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ENVIRONMENT = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS")}
INJECTED = "injected failure"

# Every stub appends "<name> <argv>" to $STUB_JOURNAL, then fails when
# $STUB_FAIL names it as "<name>" or "<name>:<n>" for its n-th call.
STUB_PROLOGUE = """#!/bin/sh
name=%(name)s
printf '%%s %%s\\n' "$name" "$*" >> "$STUB_JOURNAL"
calls=$(grep -c "^$name " "$STUB_JOURNAL")
for spec in $STUB_FAIL; do
    case $spec in
    "$name"|"$name:$calls") echo "$name: %(injected)s" >&2; exit 1 ;;
    esac
done
"""
STUB_BODIES = {
    "cc": 'for a in "$@"; do case $a in *.c) : > "${a%%.c}.o" ;; esac; done\n',
    "ld": 'while [ $# -gt 1 ]; do [ "$1" = -o ] && : > "$2"; shift; done\n',
    "size": "",
    "objcopy": 'for a in "$@"; do out=$a; done; : > "$out"\n',
    "objdump": 'echo "disassembly"\n',
    # picotool uf2 convert [--quiet] <in> [-t <type>] <out> [-t <type>]:
    # the last positional argument is the output.
    "picotool": 'skip=0; for a in "$@"; do [ $skip = 1 ] && { skip=0; continue; }; '
                'case $a in -t) skip=1 ;; --*) ;; *) out=$a ;; esac; done; : > "$out"\n',
    "make": "",
}
NEWVERS = STUB_PROLOGUE % {"name": "newvers", "injected": INJECTED} + \
    'echo "const char version[] = \\"stub\\";"\n'


class Failure(Exception):
    pass


def run(argv, cwd, env, stderr=subprocess.STDOUT):
    return subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=stderr, check=False, timeout=120,
                          env={**ENVIRONMENT, **env})


def write_stubs(directory):
    directory.mkdir()
    for name, body in STUB_BODIES.items():
        path = directory / name
        path.write_text(STUB_PROLOGUE % {"name": name, "injected": INJECTED} + body)
        path.chmod(0o755)
    return directory


def journal_of(path):
    return [line.split(None, 1) for line in path.read_text().splitlines()] \
        if path.exists() else []


def expect(condition, message, output):
    if not condition:
        raise Failure(f"{message}\n{output}")


# lib/Makefile

def lib_scratch(root, scratch):
    (scratch / "share").symlink_to(root / "share")
    lib = scratch / "lib"
    lib.mkdir()
    shutil.copy(root / "lib/Makefile", lib / "Makefile")
    for ld in (root / "lib").glob("elf32-*.ld"):
        (lib / ld.name).symlink_to(ld)
    return lib


def check_lib(root, make, scratch):
    lib = lib_scratch(root, scratch)
    stubs = write_stubs(scratch / "stubs")
    # share/mk/sys.mk asks git for a revision count, which the scratch tree
    # has no answer to; the warning goes to stderr and the value to stdout.
    query = run([*make, "MACHINE=rp2040", "-V", "${SUBDIR}"], lib, {},
                stderr=subprocess.DEVNULL)
    subdirs = query.stdout.split()
    expect(query.returncode == 0 and len(subdirs) >= 3, "lib: cannot read SUBDIR", query.stdout)
    positions = {"first": 0, "middle": len(subdirs) // 2, "last": len(subdirs) - 1}

    def invoke(target, fail):
        journal = scratch / f"journal-{target}-{'-'.join(fail) or 'none'}"
        journal.unlink(missing_ok=True)
        result = run([*make, "MACHINE=rp2040", f"MAKE={stubs / 'make'}", "DESTDIR=/nonexistent",
                      target], lib, {"STUB_JOURNAL": str(journal), "STUB_FAIL": " ".join(fail)})
        visited = []
        for entry in journal_of(journal):
            argv = shlex.split(entry[1]) if len(entry) > 1 else []
            expect(target in argv, f"lib {target}: child asked for {argv}", result.stdout)
            visited.append(argv[argv.index("-C") + 1])
        return result, visited

    result, visited = invoke("install", [])
    expect(result.returncode == 0 and visited == subdirs,
           f"lib install control: rc {result.returncode}, visited {visited}", result.stdout)
    for label, index in positions.items():
        result, visited = invoke("install", [f"make:{index + 1}"])
        expect(result.returncode != 0 and INJECTED in result.stdout,
               f"lib install, {label} child failing: rc {result.returncode}", result.stdout)
        expect(visited == subdirs[:index + 1],
               f"lib install, {label} child failing: visited {visited}", result.stdout)
    print(f"PASS lib install: first, middle and last of {len(subdirs)} children stop the loop")

    result, visited = invoke("clean", [])
    expect(result.returncode == 0 and visited == subdirs,
           f"lib clean control: rc {result.returncode}, visited {visited}", result.stdout)
    for label, fail in (("first", [1]), ("middle and last", [positions["middle"] + 1,
                                                              len(subdirs)])):
        result, visited = invoke("clean", [f"make:{n}" for n in fail])
        expect(result.returncode != 0 and INJECTED in result.stdout,
               f"lib clean, {label} failing: rc {result.returncode}", result.stdout)
        expect(visited == subdirs, f"lib clean, {label} failing: visited {visited}",
               result.stdout)
    print("PASS lib clean: every child is visited and a failure is kept")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--make", default=os.environ.get("MAKE", "bmake"))
    parser.add_argument("--only", choices=("lib",))
    args = parser.parse_args()
    make = shlex.split(args.make)
    root = args.root.resolve()
    failures = 0
    for name, check in (("lib", check_lib),):
        if args.only and args.only != name:
            continue
        with tempfile.TemporaryDirectory(prefix=f"discobsd-build-failure-{name}-") as tmp:
            try:
                check(root, make, Path(tmp))
            except Failure as error:
                failures += 1
                print(f"FAIL {name}: {error}")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f"build failure: {error}", file=sys.stderr)
        sys.exit(1)
