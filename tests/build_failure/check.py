#!/usr/bin/env python3
"""Inject a failure into each step of the production build recipes and
assert that the steps after it never run and the parent make fails.

Two recipes are exercised as the tree carries them. lib/Makefile is
copied into a scratch tree whose share/ is the real one, so its install
and clean loops run against a stub make that journals each child and
fails on request. The kernel link recipe is lifted verbatim out of the
generated PICO Makefile -- SYSTEM_LD_HEAD, SYSTEM_LD, SYSTEM_LD_TAIL, the
.elf.uf2 rule and the unix rule config emits -- into a driver Makefile
whose tools are stubs that journal their invocation and fail on request.
A stub that is asked to fail says so on stderr, and every negative case
asserts that sentence, so a fixture that never reached the intended step
cannot pass.
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
KERNEL_ROUTES = ("sys/arch/rp2040/compile/PICO", "sys/arch/rp2040/compile/PICO_UART")
KERNEL_BLOCKS = ("PICOTOOL?=", "SYSTEM_LD_HEAD=", "SYSTEM_LD=", "SYSTEM_LD_TAIL=",
                 ".SUFFIXES:", ".elf.uf2:", "unix:")
# The link recipe itself; the explicit uf2 rule is asserted by its own case,
# so a tree without it still runs the link cases and fails them by behavior.
KERNEL_REQUIRED = ("SYSTEM_LD_HEAD=", "SYSTEM_LD=", "SYSTEM_LD_TAIL=", "unix:")

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


# The kernel link recipe

def read_blocks(path, required=KERNEL_REQUIRED):
    """Return each named variable or rule block of a Makefile, continuation
    lines and recipe lines included, as it stands in the file."""
    lines = path.read_text().splitlines()
    blocks, i = {}, 0
    while i < len(lines):
        head = lines[i]
        key = next((k for k in KERNEL_BLOCKS if head.startswith(k)), None)
        if key is None:
            i += 1
            continue
        block = [head]
        i += 1
        while i < len(lines) and (block[-1].endswith("\\") or lines[i].startswith("\t")):
            block.append(lines[i])
            i += 1
        blocks[key] = "\n".join(block)
    missing = [k for k in required if k not in blocks]
    if missing:
        raise Failure(f"{path}: no block for {', '.join(missing)}")
    return blocks


def kernel_blocks(root):
    # config emits the unix rule from its load line; the template has none.
    template = read_blocks(root / "sys/arch/rp2040/conf/Makefile.rp2040",
                           required=[k for k in KERNEL_REQUIRED if k != "unix:"])
    for route in KERNEL_ROUTES:
        generated = read_blocks(root / route / "Makefile")
        for key in KERNEL_BLOCKS:
            if key != "unix:" and generated.get(key) != template.get(key):
                raise Failure(f"{route}: {key} differs from the config template; regenerate")
    return read_blocks(root / KERNEL_ROUTES[0] / "Makefile")


def kernel_driver(root, scratch):
    blocks = kernel_blocks(root)
    stubs = write_stubs(scratch / "stubs")
    source = scratch / "S"
    (source / "conf").mkdir(parents=True)
    (source / "conf/newvers.sh").write_text(NEWVERS)
    build = scratch / "build"
    build.mkdir()
    (build / "swapunix.o").touch()
    (build / "main.o").touch()
    driver = "\n".join([
        f"S=\t{source}",
        f"CC=\t{stubs / 'cc'}", "CFLAGS=\t-O",
        f"LD=\t{stubs / 'ld'}", "LDFLAGS=", "LDADD=",
        f"SIZE=\t{stubs / 'size'}", f"OBJCOPY=\t{stubs / 'objcopy'}",
        f"OBJDUMP=\t{stubs / 'objdump'}",
        "SYSTEM_OBJ=\tmain.o", "SYSTEM_DEP=",
        *(blocks[k] for k in KERNEL_BLOCKS if k in blocks),
        "",
    ])
    (build / "Makefile").write_text(driver)
    return build, stubs, blocks


ARTIFACTS = ("unix", "unix.elf", "unix.hex", "unix.bin", "unix.dis", "unix.uf2")
ORDER = ["newvers", "cc", "ld", "size", "objcopy", "objcopy", "objdump", "picotool"]


def check_kernel(root, make, scratch):
    build, stubs, blocks = kernel_driver(root, scratch)

    def invoke(target, fail, picotool=stubs / "picotool", stale=()):
        for name in (*ARTIFACTS, "vers.c", "vers.o", *(p.name for p in build.glob("*.tmp"))):
            (build / name).unlink(missing_ok=True)
        # A stale output predates swapunix.o, so the unix rule is out of
        # date and the link runs against it.
        for name in stale:
            (build / name).write_text("stale\n")
            os.utime(build / name, (1_000_000_000, 1_000_000_000))
        journal = scratch / "journal"
        journal.unlink(missing_ok=True)
        # The stubs lead PATH so a recipe naming a tool bare, as the
        # picotool call once did, still reaches a journaled stub.
        result = run([*make, f"PICOTOOL={picotool}", target], build,
                     {"STUB_JOURNAL": str(journal), "STUB_FAIL": " ".join(fail),
                      "PATH": f"{stubs}:{ENVIRONMENT.get('PATH', '/usr/bin:/bin')}"})
        return result, [entry[0] for entry in journal_of(journal)]

    def present():
        return sorted(p.name for p in build.iterdir()
                      if p.name in ARTIFACTS or p.suffix == ".tmp")

    result, calls = invoke("unix", [])
    expect(result.returncode == 0 and calls == ORDER,
           f"kernel control: rc {result.returncode}, calls {calls}", result.stdout)
    expect(present() == sorted(ARTIFACTS), f"kernel control: outputs {present()}",
           result.stdout)
    print("PASS kernel link control: " + " ".join(ORDER))

    # Each step in turn, with the artifacts of an earlier link in place so
    # a failed step is seen to remove them rather than leave them as new.
    # The steps after the link join when SYSTEM_LD_TAIL is one chain.
    # (label, failure, stubs that must not run, finished names that must
    # be absent). A failed step may leave its own .tmp behind, as objdump
    # does when the shell has already opened the redirect; the finished
    # name is what the invariant governs.
    cases = [
        ("newvers", ["newvers"], ["cc", "ld"], ("vers.o", "unix")),
        ("cc", ["cc"], ["ld"], ("vers.o", "unix")),
    ]
    for label, fail, forbidden, unpublished in cases:
        for stale in ((), ("vers.o", "vers.c", *ARTIFACTS)):
            result, calls = invoke("unix", fail, stale=stale)
            expect(result.returncode != 0 and INJECTED in result.stdout,
                   f"kernel, {label} failing: rc {result.returncode}", result.stdout)
            expect(not any(c in forbidden for c in calls),
                   f"kernel, {label} failing: later steps ran, calls {calls}", result.stdout)
            published = [name for name in unpublished if (build / name).exists()]
            expect(not published, f"kernel, {label} failing: {published} published, "
                   f"outputs {present()}", result.stdout)
            if label == "newvers":
                vers = build / "vers.c"
                expect(vers.read_text() == "stale\n" if stale else not vers.exists(),
                       "kernel, newvers failing: vers.c was published", result.stdout)
    print("PASS kernel link: a failed newvers.sh or vers.c compile links nothing, with and "
          "without stale outputs")



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--make", default=os.environ.get("MAKE", "bmake"))
    parser.add_argument("--only", choices=("lib", "kernel"))
    args = parser.parse_args()
    make = shlex.split(args.make)
    root = args.root.resolve()
    failures = 0
    for name, check in (("lib", check_lib), ("kernel", check_kernel)):
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
