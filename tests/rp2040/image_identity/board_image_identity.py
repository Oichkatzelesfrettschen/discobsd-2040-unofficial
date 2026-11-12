#!/usr/bin/env python3
"""Board test: the kernel a Pico runs is the kernel image a host file holds.

usage: DISCOBSD_BOARD_IMAGE_IDENTITY=run $PYTHON board_image_identity.py \
           LOG UNIX_BIN

The script touches the board only when DISCOBSD_BOARD_IMAGE_IDENTITY is
exactly "run"; unset, empty and every other value exit without opening a
port. It flashes nothing. With the variable set it reaches a root shell on
the USB console and reads machdep.image_length, machdep.image_crc and
machdep.image_crc_stored, the identity the kernel computed over its own
flash at boot (image_identify() in sys/arch/rp2040/rp2040/board.c), and
compares them with the same three values computed from UNIX_BIN by
image_identity() of tools/renode/check-boot.py. The sum leaves the board
descriptor out, so a board-specific copy of one build answers like the
build.

Checks: a root shell answers; the three lines equal the host's; the stored
sum equals the computed one. A board running another build fails the
comparison and the output names both identities, which is the reading the
operator wants before trusting any other measurement of that board.

Every console byte goes to LOG. The exit status is 0 when every check
passed, 1 when one failed and 2 when the console stopped answering or
UNIX_BIN carries no identity words.
"""

import importlib.util
import os
import struct
import sys

OPT_IN_VARIABLE = "DISCOBSD_BOARD_IMAGE_IDENTITY"
OPT_IN_VALUE = "run"
HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, "..", "..", "..")
NAMES = ("image_length", "image_crc", "image_crc_stored")


def load(name, path):
    """Import a script by path; neither file name is an importable module."""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    if os.environ.get(OPT_IN_VARIABLE) != OPT_IN_VALUE:
        print("board test closed: set %s=%s to run it on a board" % (OPT_IN_VARIABLE, OPT_IN_VALUE))
        return 0
    if len(sys.argv) != 3:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    sys.path.insert(0, os.path.join(TOP, "tools", "renode"))
    boot = load("check_boot", os.path.join(TOP, "tools", "renode", "check-boot.py"))
    cases = load(
        "board_capacity_cases",
        os.path.join(HERE, "..", "capacity_cases", "board_capacity_cases.py"),
    )
    try:
        want = boot.image_identity(sys.argv[2])
    except (OSError, ValueError, struct.error) as error:
        print("board_image_identity: %s" % error, file=sys.stderr)
        return 2
    console = cases.load_console()
    console.log = open(sys.argv[1], "ab")
    results = []
    console.open_port()
    try:
        console.check("a root shell answers", cases.reach_root(console), results)
        print("\n".join(console.run("cat /etc/release", 30)))
        command = "sysctl " + " ".join("machdep.%s" % name for name in NAMES)
        got = [line for line in console.run(command, 30) if line.startswith("machdep.image_")]
        print("board: %s" % ", ".join(got))
        print("host:  %s" % ", ".join(want))
        console.check("the board reports the host file's length and sums", got == want, results)
        sums = [line.split("=", 1)[1] for line in got[1:]]
        console.check(
            "the computed sum equals the stored one",
            len(sums) == 2 and sums[0] == sums[1],
            results,
        )
    except console.ConsoleSilent as silent:
        tail = console.read_for(5)
        print("console silent after %r; last output:\n%s" % (silent.args[0], tail[-600:]))
        return 2
    print("%d of %d checks passed" % (sum(results), len(results)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
