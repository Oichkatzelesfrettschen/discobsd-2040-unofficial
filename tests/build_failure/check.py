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
import time
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
    "kernsum": "",
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
    tools = scratch / "tools"
    tools.mkdir()
    (tools / "check-build-machine.sh").symlink_to(
        root / "tools/check-build-machine.sh"
    )
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

    def invoke(target, fail, child_target=None):
        journal = scratch / f"journal-{target}-{'-'.join(fail) or 'none'}"
        journal.unlink(missing_ok=True)
        result = run([*make, "MACHINE=rp2040", f"MAKE={stubs / 'make'}", "DESTDIR=/nonexistent",
                      target], lib, {"STUB_JOURNAL": str(journal), "STUB_FAIL": " ".join(fail)})
        visited = []
        for entry in journal_of(journal):
            argv = shlex.split(entry[1]) if len(entry) > 1 else []
            if child_target is not None:
                expect(child_target in argv, f"lib {target}: child asked for {argv}",
                       result.stdout)
            visited.append(argv[argv.index("-C") + 1])
        return result, visited

    # The all target reaches a child through "$(MAKE) -C child", with no
    # target word, and each child is a directory that exists. bmake judges
    # an existing target by mtime unless it is phony, against FRC, which is
    # stamped with the second the make began; a directory whose mtime is
    # not older than that stamp, as a clean's rm in the same second leaves
    # it, is skipped. The directories are dated an hour ahead so the case
    # does not depend on winning that second, and every child must still
    # be entered.
    ahead = time.time() + 3600
    for name in subdirs:
        child = lib / name
        child.mkdir()
        os.utime(child, (ahead, ahead))
    result, visited = invoke("all", [])
    expect(result.returncode == 0 and visited == subdirs,
           f"lib all with subdirectories newer than the make: rc {result.returncode}, "
           f"visited {visited}", result.stdout)
    print(f"PASS lib all: every one of {len(subdirs)} children is entered whatever "
          "its directory mtime")

    result, visited = invoke("install", [], "install")
    expect(result.returncode == 0 and visited == subdirs,
           f"lib install control: rc {result.returncode}, visited {visited}", result.stdout)
    for label, index in positions.items():
        result, visited = invoke("install", [f"make:{index + 1}"], "install")
        expect(result.returncode != 0 and INJECTED in result.stdout,
               f"lib install, {label} child failing: rc {result.returncode}", result.stdout)
        expect(visited == subdirs[:index + 1],
               f"lib install, {label} child failing: visited {visited}", result.stdout)
    print(f"PASS lib install: first, middle and last of {len(subdirs)} children stop the loop")

    result, visited = invoke("clean", [], "clean")
    expect(result.returncode == 0 and visited == subdirs,
           f"lib clean control: rc {result.returncode}, visited {visited}", result.stdout)
    for label, fail in (("first", [1]), ("middle and last", [positions["middle"] + 1,
                                                              len(subdirs)])):
        result, visited = invoke("clean", [f"make:{n}" for n in fail], "clean")
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
        f"OBJDUMP=\t{stubs / 'objdump'}", f"KERNSUM=\t{stubs / 'kernsum'}",
        "SYSTEM_OBJ=\tmain.o", "SYSTEM_DEP=",
        *(blocks[k] for k in KERNEL_BLOCKS if k in blocks),
        "",
    ])
    (build / "Makefile").write_text(driver)
    return build, stubs, blocks


ARTIFACTS = ("unix", "unix.elf", "unix.hex", "unix.bin", "unix.dis", "unix.uf2")
ORDER = ["newvers", "cc", "ld", "size", "kernsum", "objcopy", "objcopy", "objdump",
         "picotool"]


def absent_line(picotool):
    """The recipe's own report, which bmake's echo of the recipe text also
    contains inside a longer line; the report stands as a line of its own."""
    return f"{picotool} absent: unix.uf2 not written"


def check_kernel(root, make, scratch):
    build, stubs, blocks = kernel_driver(root, scratch)
    absent = scratch / "absent/picotool"

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
    # (label, failure, stubs that must not run, finished names that must
    # be absent). A failed step may leave its own .tmp behind, as objdump
    # does when the shell has already opened the redirect; the finished
    # name is what the invariant governs.
    cases = [
        ("newvers", ["newvers"], ["cc", "ld"], ("vers.o", *ARTIFACTS)),
        ("cc", ["cc"], ["ld"], ("vers.o", *ARTIFACTS)),
        ("size", ["size"], ["kernsum", "objcopy", "objdump", "picotool"], ARTIFACTS[1:]),
        ("kernsum", ["kernsum"], ["objcopy", "objdump", "picotool"], ARTIFACTS[1:]),
        ("objcopy ihex", ["objcopy:1"], ["objdump", "picotool"], ARTIFACTS[2:]),
        ("objcopy binary", ["objcopy:2"], ["objdump", "picotool"], ARTIFACTS[3:]),
        ("objdump", ["objdump"], ["picotool"], ARTIFACTS[4:]),
        ("picotool", ["picotool"], [], ARTIFACTS[5:]),
    ]
    for label, fail, forbidden, unpublished in cases:
        for stale in ((), ("vers.o", "vers.c", *ARTIFACTS)):
            result, calls = invoke("unix", fail, stale=stale)
            expect(result.returncode != 0 and INJECTED in result.stdout,
                   f"kernel, {label} failing: rc {result.returncode}", result.stdout)
            expect(not any(c in forbidden for c in calls),
                   f"kernel, {label} failing: later steps ran, calls {calls}", result.stdout)
            expect(absent_line(stubs / "picotool") not in result.stdout.splitlines(),
                   f"kernel, {label} failing: reported as tool absence", result.stdout)
            published = [name for name in unpublished if (build / name).exists()]
            expect(not published, f"kernel, {label} failing: {published} published, "
                   f"outputs {present()}", result.stdout)
            if label == "newvers":
                vers = build / "vers.c"
                expect(vers.read_text() == "stale\n" if stale else not vers.exists(),
                       "kernel, newvers failing: vers.c was published", result.stdout)
    print("PASS kernel link: each of the eight steps fails the link, with and without "
          "stale outputs")

    result, calls = invoke("unix", [], picotool=absent)
    expect(result.returncode == 0 and calls == ORDER[:-1],
           f"kernel, picotool absent: rc {result.returncode}, calls {calls}", result.stdout)
    expect(absent_line(absent) in result.stdout.splitlines() and "unix.uf2" not in present()
           and all(a in present() for a in ARTIFACTS[:-1]),
           f"kernel, picotool absent: outputs {present()}", result.stdout)
    print("PASS kernel link: an absent picotool is reported and the other outputs stand")

    expect(all(k in blocks for k in (".SUFFIXES:", ".elf.uf2:", "PICOTOOL?=")),
           "explicit uf2: the template carries no .elf.uf2 rule", "")
    result, calls = invoke("unix.uf2", [], stale=("unix.elf",))
    expect(result.returncode == 0 and calls == ["picotool"] and (build / "unix.uf2").exists(),
           f"explicit uf2: rc {result.returncode}, calls {calls}", result.stdout)
    result, calls = invoke("unix.uf2", ["picotool"], stale=("unix.elf",))
    expect(result.returncode != 0 and INJECTED in result.stdout
           and not (build / "unix.uf2").exists() and not list(build.glob("*.tmp")),
           f"explicit uf2, picotool failing: rc {result.returncode}", result.stdout)
    result, calls = invoke("unix.uf2", [], picotool=absent, stale=("unix.elf",))
    expect(result.returncode != 0 and not (build / "unix.uf2").exists(),
           f"explicit uf2, picotool absent: rc {result.returncode}", result.stdout)
    print("PASS explicit unix.uf2: a failed or absent picotool fails the request")


def check_params(root, make, scratch):
    template = (root / "sys/arch/rp2040/conf/Makefile.rp2040").read_text()
    start = template.index("PARAMSTAMP=")
    stamp = template[start:template.index("\n\n", start)]

    def exercise_default(source, directory):
        directory.mkdir()
        directive = next((line for line in source.splitlines()
                          if line.strip() == ".MAIN: unix"), "")
        (directory / "Makefile").write_text(
            directive + "\nPARAM=${OPTION}\n" + stamp
            + "\nunix: main.o\n\t@echo linked > unix\n"
            + "main.o: ${PARAMSTAMP}\n\t@echo ${OPTION} > main.o\n"
        )
        for option in ("one", "two"):
            (directory / "unix").unlink(missing_ok=True)
            result = run([*make, f"OPTION={option}"], directory, {})
            expect(result.returncode == 0 and (directory / "unix").exists()
                   and (directory / "main.o").read_text().strip() == option,
                   "default target failed to build unix", result.stdout)

    exercise_default(template, scratch / "default-control")
    for route in ("PICO", "PICO_UART", "PICOCALC", "PICOCALC_PSRAM"):
        generated = (root / f"sys/arch/rp2040/compile/{route}/Makefile").read_text()
        exercise_default(generated, scratch / f"default-{route}")
    try:
        exercise_default(template.replace(".MAIN: unix", ""), scratch / "default-mutant")
    except Failure as error:
        expect("default target failed" in str(error),
               "default target mutant failed for an unrelated reason", str(error))
    else:
        raise Failure("default target mutant was not rejected")
    print("PASS kernel default target links unix after option changes; mutant rejected")

    def exercise(block, directory):
        directory.mkdir()
        (directory / "Makefile").write_text(
            "all: main.o late.o\nPARAM= ${OPTION}\n" + block
            + "\nmain.o late.o: ${PARAMSTAMP}\n\t@echo ${PARAM} > $@\n"
        )
        objects = [directory / "main.o", directory / "late.o"]
        report = directory / "main.su"
        for option in ("no", "yes", "no"):
            # Objects newer than the stamp model the whole-second mtime tie
            # without depending on the host clock; two objects make one of
            # them examined after the stamp target, as most kernel objects are.
            ahead = time.time() + 3600
            for obj in objects:
                obj.write_text("stale\n")
                os.utime(obj, (ahead, ahead))
            report.write_text("stale\n")
            result = run([*make, f"OPTION={option}", "all"], directory, {})
            expect(result.returncode == 0
                   and all(obj.read_text().strip() == option for obj in objects),
                   f"changed PARAM {option}: stale object reused", result.stdout)
            expect(not report.exists(), "changed PARAM: stale stack report retained", result.stdout)
            report.write_text("current\n")
            before = [obj.stat().st_mtime_ns for obj in objects]
            result = run([*make, f"OPTION={option}", "all"], directory, {})
            expect(result.returncode == 0
                   and [obj.stat().st_mtime_ns for obj in objects] == before,
                   "unchanged PARAM rebuilt an object", result.stdout)
            expect(report.read_text() == "current\n", "unchanged PARAM removed stack report",
                   result.stdout)
            # Inspecting another option is read-only: -n and -V leave the
            # objects and the stamp as the last build wrote them.
            other = "yes" if option == "no" else "no"
            stamp_text = (directory / ".params").read_text()
            for inspect in (["-n", f"OPTION={other}", "all"],
                            [f"OPTION={other}", "-V", "PARAM"]):
                result = run([*make, *inspect], directory, {})
                expect(result.returncode == 0
                       and all(obj.exists() for obj in objects)
                       and [obj.stat().st_mtime_ns for obj in objects] == before
                       and (directory / ".params").read_text() == stamp_text,
                       f"{inspect[0]} with another PARAM changed the build",
                       result.stdout)

    exercise(stamp, scratch / "control")
    stack_mutant = stamp.replace("*.o *.su", "*.o")
    expect(stack_mutant != stamp, "stack report calibration did not change stamp", "")
    try:
        exercise(stack_mutant, scratch / "stack-mutant")
    except Failure as error:
        expect("stale stack report retained" in str(error),
               "stack report mutant failed for an unrelated reason", str(error))
    else:
        raise Failure("stack report mutant retained a stale report without rejection")

    def exercise_flags(block, directory):
        directory.mkdir()
        (directory / "Makefile").write_text(
            "all: main.o\nPARAM=stable\nCFLAGS=${OPT}\nCC=${COMPILER}\n"
            "LDFLAGS=-Map=$@.map\n"
            + block
            + "\nmain.o: ${PARAMSTAMP}\n\t@echo ${OPT} ${COMPILER} > $@\n"
        )
        obj = directory / "main.o"
        for opt, compiler in (("-Os", "gcc"), ("-O0", "gcc"),
                              ("-O0", "clang")):
            if obj.exists():
                ahead = time.time() + 3600
                os.utime(obj, (ahead, ahead))
            result = run([*make, f"OPT={opt}", f"COMPILER={compiler}", "all"],
                         directory, {})
            expect(result.returncode == 0 and obj.read_text().strip() == f"{opt} {compiler}",
                   f"changed compiler options {opt} {compiler}: stale object reused",
                   result.stdout)
            before = obj.stat().st_mtime_ns
            result = run([*make, f"OPT={opt}", f"COMPILER={compiler}", "all"],
                         directory, {})
            expect(result.returncode == 0 and obj.stat().st_mtime_ns == before,
                   "unchanged compiler options rebuilt an object", result.stdout)

    exercise_flags(stamp, scratch / "flags-control")
    flags_mutant = stamp.replace("${CC:Q} ${CFLAGS:Q}", "")
    expect(flags_mutant != stamp, "compiler-options calibration did not change stamp", "")
    try:
        exercise_flags(flags_mutant, scratch / "flags-mutant")
    except Failure as error:
        expect("stale object reused" in str(error),
               "compiler-options mutant failed for an unrelated reason", str(error))
    else:
        raise Failure("compiler-options mutant reused stale objects without rejection")

    def exercise_boot2(block, directory):
        directory.mkdir()
        (directory / "Makefile").write_text(
            "all: boot2.o\nPARAM=stable\nAFLAGS=${BOOTFLAG}\n" + block
            + "\nboot2.o: ${PARAMSTAMP}\n"
            + "boot2.o: boot2.S\n\tcp boot2.S boot2.o\n"
            + "boot2.S: boot2.bin\n\tcp boot2.bin boot2.S\n"
            + "boot2.bin: boot2.elf\n\tcp boot2.elf boot2.bin\n"
            + "boot2.elf: boot2.src\n\t@echo ${AFLAGS} > boot2.elf\n"
        )
        (directory / "boot2.src").write_text("fixture\n")
        for flag in ("divisor-2", "divisor-4"):
            result = run([*make, f"BOOTFLAG={flag}", "all"], directory, {})
            expect(result.returncode == 0
                   and (directory / "boot2.o").read_text().strip() == flag,
                   f"changed assembler options {flag}: stale boot2 reused", result.stdout)

    exercise_boot2(stamp, scratch / "boot2-control")
    boot2_mutant = stamp.replace("*.o *.su boot2.elf boot2.bin boot2.S", "*.o *.su")
    expect(boot2_mutant != stamp, "boot2 calibration did not remove invalidation", "")
    try:
        exercise_boot2(boot2_mutant, scratch / "boot2-mutant")
    except Failure as error:
        expect("stale boot2 reused" in str(error),
               "boot2 mutant failed for an unrelated reason", str(error))
    else:
        raise Failure("boot2 mutant reused stale intermediates without rejection")

    mutant = stamp.replace("rm -f -- *.o *.su boot2.elf boot2.bin boot2.S && ", "")
    expect(mutant != stamp, "PARAM calibration did not remove the force", "")
    try:
        exercise(mutant, scratch / "mutant")
    except Failure as error:
        expect("stale object reused" in str(error),
               "PARAM mutant failed for an unrelated reason", str(error))
    else:
        raise Failure("PARAM mutant reused stale objects without rejection")
    print("PASS configuration and compiler-option changes rebuild newer objects; "
          "unchanged options reuse; missing dependencies and force rejected")


def check_version_identity(root, make, scratch):
    del make
    command_directory = scratch / "commands"
    command_directory.mkdir()
    git_command = command_directory / "git"
    git_command.write_text("#!/bin/sh\nprintf '123\\n'\n")
    git_command.chmod(0o755)
    hostname_command = command_directory / "hostname"
    hostname_command.write_text("#!/bin/sh\nprintf 'private-host-fixture\\n'\n")
    hostname_command.chmod(0o755)
    environment = {"PATH": str(command_directory) + os.pathsep + os.environ["PATH"],
                   "USER": "private-user-fixture"}
    source = (root / "sys/conf/newvers.sh").read_text()

    def exercise(script, directory):
        directory.mkdir()
        fixture = directory / "newvers.sh"
        fixture.write_text(script)
        result = run(["sh", str(fixture)], directory, environment)
        expect(result.returncode == 0, "kernel version generation failed", result.stdout)
        for private in ("private-host-fixture", "private-user-fixture", str(directory)):
            expect(private not in result.stdout, "kernel version exposes build identity", "")
        expect('DiscoBSD 2.7' in result.stdout and ' 123:' in result.stdout and
               f'({directory.name.upper()})' in result.stdout,
               "kernel version omits release, configuration or revision", result.stdout)
        return result.stdout

    exercise(source, scratch / "control")
    environment["SOURCE_DATE_EPOCH"] = "946684800"
    reproducible = exercise(source, scratch / "reproducible")
    expect("Sat Jan  1 00:00:00 UTC 2000" in reproducible,
           "SOURCE_DATE_EPOCH is ignored", reproducible)
    environment["SOURCE_DATE_EPOCH"] = "not-a-timestamp"
    invalid = scratch / "invalid"
    invalid.mkdir()
    fixture = invalid / "newvers.sh"
    fixture.write_text(source)
    result = run(["sh", str(fixture)], invalid, environment)
    expect(result.returncode != 0 and not (invalid / ".compileversion").exists(),
           "invalid SOURCE_DATE_EPOCH changed version state", result.stdout)
    date_directory = scratch / "date-commands"
    date_directory.mkdir()
    date_command = date_directory / "date"
    date_command.write_text(
        "#!/bin/sh\n"
        "if [ \"$FIXTURE_DATE_STYLE\" = bsd ] && [ \"${2-}\" = -r ]; then\n"
        "  printf 'Sat Jan  1 00:00:00 UTC 2000\\n'; exit 0\n"
        "fi\n"
        "if [ \"$FIXTURE_DATE_STYLE\" = gnu ] && [ \"${2-}\" = -d ] "
        "&& [ \"${3-}\" = @0 ]; then exit 0; fi\n"
        "if [ \"$FIXTURE_DATE_STYLE\" = gnu ] && [ \"${2-}\" = -r ]; then\n"
        "  printf 'incorrect file timestamp\\n'; exit 0\n"
        "fi\n"
        "exit 1\n"
    )
    date_command.chmod(0o755)
    original_path = environment["PATH"]
    environment["PATH"] = str(date_directory) + os.pathsep + original_path
    environment["FIXTURE_DATE_STYLE"] = "bsd"
    environment["SOURCE_DATE_EPOCH"] = "946684800"
    expect("Sat Jan  1 00:00:00 UTC 2000" in exercise(source, scratch / "bsd-date"),
           "BSD epoch conversion failed", "")
    environment["FIXTURE_DATE_STYLE"] = "gnu"
    environment["SOURCE_DATE_EPOCH"] = "99999999999999999999999999"
    out_of_range = scratch / "out-of-range"
    out_of_range.mkdir()
    fixture = out_of_range / "newvers.sh"
    fixture.write_text(source)
    result = run(["sh", str(fixture)], out_of_range, environment)
    expect(result.returncode != 0 and not (out_of_range / ".compileversion").exists(),
           "out-of-range GNU epoch fell back to a file timestamp", result.stdout)
    environment["PATH"] = original_path
    environment.pop("FIXTURE_DATE_STYLE")
    environment.pop("SOURCE_DATE_EPOCH")
    try:
        exercise(source + "\nprintf 'private-host-fixture\\n'\n", scratch / "mutant")
    except Failure as error:
        expect("exposes build identity" in str(error),
               "version identity mutant failed for an unrelated reason", str(error))
    else:
        raise Failure("version identity mutant was not rejected")
    print("PASS kernel version omits build identity; identity mutant rejected")


def check_icode_toolchain_selector(root, make, scratch):
    del scratch
    for variable, selected in (("GCCPREFIX", "/fixture/selected arm-toolchain"),
                               ("ARM_GCC_PREFIX", "/fixture/kernel arm-toolchain"),
                               ("BOARDLIBC_GCCPREFIX", "/fixture/default arm-toolchain")):
        result = run([*make, "-n", "MACHINE=rp2040", f"PYTHON={sys.executable}",
                      f"{variable}={selected}", "check-icode-relocation"], root, {})
        expect(result.returncode == 0, "icode toolchain dry run failed", result.stdout)
        command = next((line for line in result.stdout.splitlines()
                        if "tools/check_icode_relocation.py" in line), "")
        arguments = shlex.split(command)
        expect("--gcc" in arguments and arguments[arguments.index("--gcc") + 1]
               == f"{selected}-gcc", f"icode gate ignored {variable}", result.stdout)
    print("PASS icode relocation gate uses the selected root compiler prefix")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--make", default=os.environ.get("MAKE", "bmake"))
    parser.add_argument("--only", choices=("lib", "kernel", "params", "version", "icode"))
    args = parser.parse_args()
    make = shlex.split(args.make)
    root = args.root.resolve()
    failures = 0
    for name, check in (("lib", check_lib), ("kernel", check_kernel),
                        ("params", check_params), ("version", check_version_identity),
                        ("icode", check_icode_toolchain_selector)):
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
