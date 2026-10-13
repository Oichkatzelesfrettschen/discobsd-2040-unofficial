"""Require self-contained case comparison code with no table storage."""

import subprocess
import sys


def validate(symbols: str) -> None:
    exported = []
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) != 4 or fields[1] not in ("T", "t"):
            raise ValueError("casefold object contains storage, imports or malformed symbols")
        int(fields[2], 16)
        int(fields[3], 16)
        if fields[1] == "T":
            exported.append(fields[0])
    if sorted(exported) != ["strcasecmp", "strncasecmp"]:
        raise ValueError("casefold object must export both comparison functions")


def validate_sections(listing: str) -> None:
    sections = {}
    for line in listing.splitlines():
        fields = line.split()
        if line.rstrip().endswith(":"):
            continue
        if fields and fields[0].startswith("."):
            if len(fields) != 3:
                raise ValueError("malformed section accounting")
            if fields[0] in sections:
                raise ValueError("duplicate section accounting")
            sections[fields[0]] = int(fields[1])
    if not {".text", ".data", ".bss"}.issubset(sections):
        raise ValueError("missing code/data section accounting")
    for name, size in sections.items():
        if name.startswith((".data", ".bss", ".rodata", ".sdata", ".sbss")) and size:
            raise ValueError("casefold object retains data or read-only storage")


def inspect(inspector: str, object_path: str) -> None:
    result = subprocess.run(
        [inspector, "-P", object_path], capture_output=True, text=True, check=True
    )
    validate(result.stdout)


def self_test() -> None:
    good = "strcasecmp T 0 2e\nstrncasecmp T 2e 36\n"
    validate(good)
    validate(good + "fold_ascii t 64 c\n")
    for bad in (
        "", "strcasecmp T 0 2e\n", good + "charmap d 0 100\n",
        good + "charmap r 0 100\n", good + "tolower U         \n",
        good + "counter b 0 4\n", good + "malformed\n",
    ):
        try:
            validate(bad)
        except ValueError:
            continue
        raise ValueError("object checker accepts a known-bad symbol listing")
    try:
        inspect("false", "calibration")
    except subprocess.CalledProcessError:
        pass
    else:
        raise ValueError("object checker accepts a failed inspector")
    good_sections = ".text 100 0\n.data 0 0\n.bss 0 0\n"
    validate_sections(good_sections)
    for bad_sections in (
        "", ".text 100 0\n", good_sections + ".rodata 256 0\n",
        good_sections.replace(".data 0", ".data 256"),
        good_sections.replace(".bss 0", ".bss 4"),
        good_sections + ".data.invalid malformed 0\n",
        good_sections + ".data 256 0\n",
    ):
        try:
            validate_sections(bad_sections)
        except ValueError:
            continue
        raise ValueError("section checker accepts known-bad storage accounting")
    print("PASS casefold object inspection calibration")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    elif len(sys.argv) == 4:
        inspect(sys.argv[1], sys.argv[3])
        result = subprocess.run(
            [sys.argv[2], "-A", sys.argv[3]],
            capture_output=True, text=True, check=True
        )
        validate_sections(result.stdout)
    else:
        raise SystemExit("usage: check_object.py --self-test | NM SIZE OBJECT")
