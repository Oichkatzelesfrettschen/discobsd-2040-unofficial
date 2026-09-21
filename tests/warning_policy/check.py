#!/usr/bin/env python3
"""Exercise the evaluated compiler commands, not just warning-flag text.

The cross tier also proves the census lever: tools/warning-census.sh
widens the warning set through WARNERR, the variable share/mk/sys.mk puts
on the CC command, so the same routes are compiled with that override and
must both emit the -Wextra diagnostic and keep the object, since the
census demotes errors to keep every directory building. A route that
replaces CFLAGS outright, usr.bin/smlrc, is in the set for that reason.
"""

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
HOST_ROUTES = ("tools/binstall", "tools/config", "tools/fsutil", "share/zoneinfo")
# Full routes compile clean under -Wall -Wextra, so share/mk/warnings.mk
# makes both groups fatal there; legacy routes declare WARNLEVEL=legacy
# before their sys.mk include and are held to -Werror alone. bin/sh holds
# the largest open site count of the small programs, usr.bin/uucp the
# largest of all.
CROSS_ROUTES = ("bin/echo", "usr.bin/smlrc", "lib/libc")
LEGACY_ROUTES = ("bin/sh", "usr.bin/uucp")
KERNEL_ROUTES = ("sys/arch/rp2040/compile/PICO", "sys/arch/rp2040/compile/PICO_UART")
CENSUS_OVERRIDE = "WARNERR=-Wall -Wextra -Wno-error"


# A parent make running with -j exports its jobserver in MAKEFLAGS as
# "-j N -J fd,fd". subprocess closes inherited descriptors, so the child
# bmake finds the jobserver gone and prints "Invalid internal option -J"
# onto the output this gate parses, which turns a compiler command into the
# token bmake[1]:. The query answers a question about a Makefile's variables
# and never wants the parent's job control, so both variables are dropped.
ENVIRONMENT = {k: v for k, v in os.environ.items() if k not in ("MAKEFLAGS", "MFLAGS")}


def run(argv, cwd):
    return subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False, timeout=60,
                          env=ENVIRONMENT)


def check_route(root, make, directory, overrides, kinds):
    cwd = root / directory
    if not (cwd / "Makefile").is_file():
        raise RuntimeError(f"{directory}: route has no Makefile")
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


def check_legacy_route(root, make, directory):
    """A legacy route rejects the fatal probe and accepts the -Wextra probe."""
    check_route(root, make, directory, [], ("fatal",))
    cwd = root / directory
    query = run([*make, "MACHINE=rp2040", "-V", "${CC} ${CFLAGS}"], cwd)
    if query.returncode:
        raise RuntimeError(f"{directory}: cannot evaluate compiler command:\n{query.stdout}")
    command = shlex.split(query.stdout.strip())
    with tempfile.TemporaryDirectory(prefix="discobsd-legacy-") as name:
        temporary = Path(name)
        source, output = temporary / "probe.c", temporary / "probe.o"
        argv = [*command, "-c", str(source), "-o", str(output)]
        text, _ = PROBES["extra"]
        source.write_text(text)
        result = run(argv, cwd)
        if result.returncode or not output.is_file():
            raise RuntimeError(
                f"{directory}: extra probe was fatal on a legacy route, so WARNLEVEL "
                f"did not reach CC\ncommand: {shlex.join(argv)}\n{result.stdout}"
            )
    print(f"PASS {directory} (legacy level): fatal rejected, extra accepted")


def check_level_declarations(root):
    """Every WARNLEVEL assignment precedes the include that composes CC.

    sys.mk, and tools/Makefile.inc for the host tools, compose CC when
    they are included, so an assignment after the include line selects
    nothing and the directory silently builds at the default level; the
    value is also held to the two warnings.mk accepts.
    """
    listing = subprocess.run(["git", "ls-files", "--", "*Makefile", "*.mk"], cwd=root,
                             text=True, stdout=subprocess.PIPE, check=True).stdout
    declared = 0
    for name in listing.split():
        if name == "share/mk/warnings.mk":
            continue
        lines = (root / name).read_text().splitlines()
        include_at = next((i for i, line in enumerate(lines)
                           if ("share/mk/sys.mk" in line or "Makefile.inc" in line)
                           and line.lstrip().startswith(("include", ".include", "-include"))), None)
        for i, line in enumerate(lines):
            match = re.match(r"^WARNLEVEL\s*[?:]?=\s*(\S+)", line)
            if match is None:
                continue
            declared += 1
            if match.group(1) not in ("full", "legacy"):
                raise RuntimeError(f"{name}:{i + 1}: WARNLEVEL is {match.group(1)}, "
                                   "not full or legacy")
            if include_at is None or i > include_at:
                raise RuntimeError(f"{name}:{i + 1}: WARNLEVEL is assigned after the sys.mk "
                                   "include, where CC has already been composed")
    print(f"PASS {declared} WARNLEVEL declarations precede their sys.mk include")


def check_census_route(root, make, directory):
    """The census override reaches the route and demotes the error."""
    cwd = root / directory
    query = run([*make, "MACHINE=rp2040", CENSUS_OVERRIDE, "-V", "${CC} ${CFLAGS}"], cwd)
    if query.returncode:
        raise RuntimeError(f"{directory}: cannot evaluate census command:\n{query.stdout}")
    command = shlex.split(query.stdout.strip())
    with tempfile.TemporaryDirectory(prefix="discobsd-census-") as name:
        temporary = Path(name)
        source, output = temporary / "probe.c", temporary / "probe.o"
        argv = [*command, "-c", str(source), "-o", str(output)]
        for kind in ("wall", "extra"):
            output.unlink(missing_ok=True)
            text, diagnostic = PROBES[kind]
            source.write_text(text)
            result = run(argv, cwd)
            if diagnostic not in result.stdout:
                raise RuntimeError(
                    f"{directory}: census {kind} probe raised no {diagnostic}\n"
                    f"command: {shlex.join(argv)}\n{result.stdout}"
                )
            if result.returncode or not output.is_file():
                raise RuntimeError(
                    f"{directory}: census {kind} probe was fatal, so -Wno-error did not land\n"
                    f"command: {shlex.join(argv)}\n{result.stdout}"
                )
    print(f"PASS {directory} (census lever): wall, extra reach and stay non-fatal")


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
    if args.tier == "host":
        check_level_declarations(args.root)
    if args.tier == "cross":
        for directory in routes:
            check_route(args.root, make, directory, [], ("wall", "extra"))
        for directory in LEGACY_ROUTES:
            check_legacy_route(args.root, make, directory)
        for directory in (*routes, *LEGACY_ROUTES):
            check_census_route(args.root, make, directory)
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
