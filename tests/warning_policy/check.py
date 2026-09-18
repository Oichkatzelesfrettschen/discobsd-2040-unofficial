#!/usr/bin/env python3
"""Exercise the evaluated compiler commands, not just warning-flag text."""

import argparse
import os
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

CONTROL = "int warning_probe(int value) { return value; }\n"
PROBES = {
    "fatal": ("#warning warning_policy_sentinel\n" + CONTROL, "warning_policy_sentinel"),
    "wall": (
        "int warning_probe(int value) { int unused_local; return value; }\n",
        "unused-variable",
    ),
    "extra": ("int warning_probe(int unused_arg) { return 0; }\n", "unused-parameter"),
}
HOST_ROUTES = ("tools/binstall", "tools/config", "tools/fsutil", "share/zoneinfo",
               "usr.bin/smux/linux")
CROSS_ROUTES = ("bin/cat", "usr.bin/smlrc", "lib/libc")
KERNEL_ROUTES = ("sys/arch/rp2040/compile/PICO", "sys/arch/rp2040/compile/PICO_UART")


def run(argv, cwd):
    return subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False, timeout=60)


def check_route(root, make, directory, overrides, kinds):
    cwd = root / directory
    query = run([*make, "MACHINE=rp2040", *overrides, "-V", "${CC} ${CFLAGS}"], cwd)
    if query.returncode:
        raise RuntimeError(f"{directory}: cannot evaluate compiler command:\n{query.stdout}")
    command = shlex.split(query.stdout.strip())
    if not command:
        raise RuntimeError(f"{directory}: empty compiler command")
    with tempfile.TemporaryDirectory(prefix="discobsd-warnings-") as name:
        temporary = Path(name)
        source, output = temporary / "probe.c", temporary / "probe.o"
        argv = [*command, "-c", str(source), "-o", str(output)]
        source.write_text(CONTROL)
        control = run(argv, cwd)
        if control.returncode or not output.is_file():
            raise RuntimeError(f"{directory}: clean control failed:\n{control.stdout}")
        for kind in kinds:
            output.unlink(missing_ok=True)
            text, diagnostic = PROBES[kind]
            source.write_text(text)
            result = run(argv, cwd)
            if result.returncode == 0 or diagnostic not in result.stdout:
                raise RuntimeError(
                    f"{directory}: {kind} probe was not rejected for {diagnostic}\n"
                    f"command: {shlex.join(argv)}\n{result.stdout}"
                )
    profile = ", ".join(overrides) if overrides else "defaults"
    print(f"PASS {directory} ({profile}): {', '.join(kinds)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tier", choices=("host", "cross"))
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--make", default=os.environ.get("MAKE", "bmake"))
    args = parser.parse_args()
    make = shlex.split(args.make)
    routes = HOST_ROUTES if args.tier == "host" else CROSS_ROUTES
    for directory in routes:
        for overrides in ([], ["CFLAGS=-O0"]):
            check_route(args.root, make, directory, overrides, ("fatal",))
    if args.tier == "cross":
        template = args.root / "sys/arch/rp2040/conf/Makefile.rp2040"
        expected = re.search(r"^CWARNFLAGS=.*$", template.read_text(), re.MULTILINE)
        if expected is None:
            raise RuntimeError("kernel template has no CWARNFLAGS assignment")
        for directory in KERNEL_ROUTES:
            generated = re.search(r"^CWARNFLAGS=.*$",
                                  (args.root / directory / "Makefile").read_text(), re.MULTILINE)
            if generated is None or generated.group().split() != expected.group().split():
                raise RuntimeError(f"{directory}: warning policy differs from config template")
        for directory in KERNEL_ROUTES:
            check_route(args.root, make, directory, [], ("fatal", "wall", "extra"))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"warning policy: {error}", file=sys.stderr)
        sys.exit(1)
