"""Reject a cross-compiled program object bound to a 32-bit time entry point.

sys/sys/_time_t.h gives every unit the cross compiler builds a 64-bit
time_t and binds each function that passes one, through __TIME64_NAME(), to
a second symbol, __NAME64. The existing names (stat, time, ctime and the
others) keep the kernel's 32-bit layouts for libc's own members and for
programs Smaller C compiles on the board. An object that reaches one of
them with the 64-bit layout -- a unit built with _TIME_BITS defined as 32,
or a call through a hand-written declaration such as `long time();` that
no header binds -- links without a diagnostic and hands the narrow entry a
wider object. The linked program cannot show the mistake: tools/elf2aout
writes a_syms = 0, and a program with a correct wide caller holds both
symbol sets anyway, since each wide wrapper calls its narrow entry. The
check therefore reads the undefined symbols of each relocatable object
before the link.

The names come from the __TIME64_NAME() declarations of the headers, so a
function that gains a second symbol is checked without an edit here. Each
scanned object is a 32-bit little-endian ARM ELF relocatable; host objects
and the a.out members of lib/libc_aout are skipped and counted. lib/libc is
excluded whole, since its members other than the 64-bit ones are compiled
with the 32-bit view on purpose (lib/libc/Makefile), and so is
usr.bin/smlrc/tests, whose companion objects call the narrow names by hand
beside a Smaller C object. The verdict fails closed: no header names, no
scanned object, or an nm failure is an error rather than a pass.

usage: check_time_binding.py [--root DIR] [--nm PROGRAM] [DIR ...]
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROGRAM_DIRS = ("bin", "sbin", "usr.bin", "usr.sbin", "libexec", "games",
                "benchmarks", "lib")
EXCLUDED_DIRS = ("lib/libc", "lib/libc_aout", "usr.bin/smlrc/tests")
HEADER_DIRS = ("include", "sys/sys")
DECLARATION = re.compile(r"__TIME64_NAME\(\s*(\w+)\s*\)")
ELF_REL = 1
ELF_ARM = 40
BATCH = 200


class BindingError(Exception):
    """The check could not reach a verdict."""


def narrow_names(root):
    """The function names a __TIME64_NAME() declaration binds."""
    names = set()
    for relative in HEADER_DIRS:
        top = root / relative
        for directory, subdirectories, files in os.walk(top):
            subdirectories.sort()
            for name in sorted(files):
                if not name.endswith(".h"):
                    continue
                text = (Path(directory) / name).read_text(errors="replace")
                for line in text.splitlines():
                    if line.lstrip().startswith("#define"):
                        continue
                    names.update(DECLARATION.findall(line))
    if not names:
        raise BindingError("no __TIME64_NAME() declaration under %s"
                           % ", ".join(HEADER_DIRS))
    return names


def is_arm_relocatable(path):
    """True for a 32-bit little-endian ARM ELF relocatable object."""
    with open(path, "rb") as stream:
        header = stream.read(20)
    if len(header) < 20 or header[:4] != b"\x7fELF":
        return False
    if header[4] != 1 or header[5] != 1:
        return False
    e_type = int.from_bytes(header[16:18], "little")
    e_machine = int.from_bytes(header[18:20], "little")
    return e_type == ELF_REL and e_machine == ELF_ARM


def object_files(root, directories):
    """Every .o under the directories, outside the excluded trees."""
    excluded = [root / relative for relative in EXCLUDED_DIRS]
    found = []
    for top in directories:
        for directory, subdirectories, files in os.walk(top):
            here = Path(directory)
            subdirectories[:] = sorted(
                name for name in subdirectories
                if here / name not in excluded)
            if here in excluded:
                continue
            found.extend(here / name for name in sorted(files)
                         if name.endswith(".o"))
    return found


def undefined_symbols(nm, paths):
    """Map each path to its undefined symbols, one nm run per batch."""
    result = {path: set() for path in paths}
    for start in range(0, len(paths), BATCH):
        batch = paths[start:start + BATCH]
        completed = subprocess.run(
            [nm, "-A", "-P", "-u"] + [str(path) for path in batch],
            capture_output=True, text=True, check=False)
        if completed.returncode != 0:
            raise BindingError("%s failed (%d): %s" % (
                nm, completed.returncode, completed.stderr.strip()))
        for line in completed.stdout.splitlines():
            # "path: symbol U" with -A -P; the path itself may hold ':'.
            location, _, rest = line.rpartition(": ")
            fields = rest.split()
            if not location or not fields:
                raise BindingError("unreadable nm line: %r" % line)
            result[Path(location)].add(fields[0])
    return result


def check(root, nm, directories):
    """Return (violations, scanned, skipped, names)."""
    names = narrow_names(root)
    candidates = object_files(root, directories)
    scanned = [path for path in candidates if is_arm_relocatable(path)]
    skipped = len(candidates) - len(scanned)
    if not scanned:
        raise BindingError("no ARM relocatable object under %s; build first"
                           % ", ".join(str(top) for top in directories))
    violations = []
    for path, symbols in sorted(undefined_symbols(nm, scanned).items()):
        for symbol in sorted(symbols & names):
            violations.append((path, symbol))
    return violations, len(scanned), skipped, names


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=ROOT,
                        help="tree whose headers name the bound functions")
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("directories", nargs="*", type=Path,
                        help="directories to scan (default: the program "
                        "and library trees of --root)")
    arguments = parser.parse_args(argv)
    root = arguments.root.resolve()
    directories = [path.resolve() for path in arguments.directories] or [
        root / relative for relative in PROGRAM_DIRS]
    try:
        violations, scanned, skipped, names = check(
            root, arguments.nm, directories)
    except (BindingError, OSError) as error:
        print("check-time-binding: ERROR: %s" % error, file=sys.stderr)
        return 2
    for path, symbol in violations:
        try:
            shown = path.relative_to(root)
        except ValueError:
            shown = path
        print("check-time-binding: %s references the 32-bit entry %s; "
              "its caller has the 64-bit view, so it needs __%s64"
              % (shown, symbol, symbol), file=sys.stderr)
    verdict = "FAIL" if violations else "PASS"
    print("check-time-binding: %s: %d objects scanned, %d skipped (not ARM "
          "relocatable), %d names, %d narrow references"
          % (verdict, scanned, skipped, len(names), len(violations)))
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
