"""Exercise exact sysctl source in fresh ILP32 processes with injected responses."""

import argparse
import os
import shlex
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path)
parser.add_argument("--compiler", default="cc")
args = parser.parse_args()
root = args.root.resolve()
here = root / "tests/sysctl_status"
out = here / "out"
out.mkdir(exist_ok=True)
(out / "machine").mkdir(exist_ok=True)
for header in (root / "include/rp2040").glob("*.h"):
    (out / "machine" / header.name).write_text(f"#include <rp2040/{header.name}>\n")
compiler = [*shlex.split(args.compiler), "-m32", "-std=c17", "-Wall", "-Wextra", "-Werror"]
subprocess.run([*compiler, "-c", str(here / "shim.c"), "-o", str(out / "shim.o")], check=True)
flags = [*compiler, "-fno-builtin", "-nostdinc", "-I", str(out), "-isystem", str(root / "include")]
subprocess.run([*flags, "-c", str(here / "fixture.c"), "-o", str(out / "fixture.o")], check=True)
renames = [
    "-Derrno=status_errno",
    "-Dfprintf=status_fprintf",
    "-Dperror=status_perror",
    "-Dsysctl=status_sysctl",
    "-Dgetloadavg=status_getloadavg",
    "-Ddevname=status_devname",
]
source = root / "sbin/sysctl/sysctl.c"


def build(path, output):
    subprocess.run(
        [
            *flags,
            *renames,
            str(path),
            str(root / "lib/libc/gen/ctype_.c"),
            str(out / "fixture.o"),
            str(out / "shim.o"),
            "-o",
            str(output),
        ],
        check=True,
    )


def verify(binary):
    cases = [
        (["kern.capacity"], "", True),
        (["machdep.storage_stats"], "", True),
        (["hw.ncpu", "kern.capacity"], "", True),
        (["unknown", "hw.ncpu"], "", False),
        (["hw.ncpu", "unknown"], "", False),
        (["hw.ncpu.extra"], "", False),
        (["net"], "", False),
        (["kern.proc"], "", False),
        (["machdep.mpu"], "", True),
        (["machdep.mpu.unknown"], "", False),
        (["vm.loadavg"], "load-failure", False),
        (["-a"], "unavailable", True),
        (["-A"], "unavailable", True),
        (["-w", "kern.maxproc=3", "kern.maxfiles=4"], "", True),
    ]
    for node in ("kern.capacity", "machdep.storage_stats"):
        for scenario in ("unavailable", "version", "record-size", "length"):
            cases.append(([node], scenario, False))
    for operands, scenario, success in cases:
        result = subprocess.run(
            [str(binary), *operands],
            text=True,
            capture_output=True,
            env=dict(os.environ, SCENARIO=scenario),
        )
        if result.returncode != (0 if success else 1):
            raise AssertionError((operands, scenario, result.returncode, result.stderr))
        visits = [line for line in result.stderr.splitlines() if line.startswith("visit ")]
        if operands == ["hw.ncpu", "kern.capacity"] or operands[0] == "-w":
            assert len(visits) == 2 and visits[0] != visits[1], (operands, visits)
        if "unknown" in operands:
            assert len(visits) == 1, (operands, visits)
    return len(cases)


binary = out / "sysctl"
build(source, binary)
count = verify(binary)
text = source.read_text()
for name, anchor, replacement in (
    ("status", "return (exit_status);", "(void)exit_status; return (0);"),
    ("operand", "parse(*argv++, 1)", "parse(*argv, 1)"),
):
    assert text.count(anchor) == 1, (name, "mutation anchor")
    mutant = out / (name + ".c")
    mutant.write_text(text.replace(anchor, replacement))
    mutated_binary = out / name
    build(mutant, mutated_binary)
    try:
        verify(mutated_binary)
    except AssertionError:
        continue
    raise AssertionError((name, "mutant escapes"))
print(f"sysctl: {count} behavior cases pass; status and operand mutants fail")
