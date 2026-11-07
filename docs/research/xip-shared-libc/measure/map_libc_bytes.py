"""Sum GNU ld map input-section sizes by origin: libc.a members, crt0, other.

usage: map_libc_bytes.py MAP NAME MEMBERS_OUT

Prints one summary line for NAME and writes "member bytes" rows for every
libc.a member that contributes allocatable image bytes (text, rodata, data).
The figures are linker attribution: merged sections are charged to the input
file the map names, so they locate libc's share rather than measure it exactly.
"""

import collections
import re
import sys

ALLOC_PREFIXES = (".text", ".rodata", ".data", ".bss", ".sbss", ".app_bss")
SECTION_ONLY = re.compile(r"^ (\.[\w.$]+)\s*$")
INPUT_LINE = re.compile(r"^ (\.[\w.$]+)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)")
MEMBER = re.compile(r"\((.*)\)")


def origin_of(path: str) -> str:
    """Classify an input file named in the map."""
    if "libc.a(" in path:
        return "libc"
    if "crt0" in path:
        return "crt0"
    return "own"


def summarize(
    map_path: str,
) -> tuple[collections.Counter[tuple[str, str]], collections.Counter[str]]:
    """Return totals by (origin, kind) and libc image bytes by member."""
    totals: collections.Counter[tuple[str, str]] = collections.Counter()
    members: collections.Counter[str] = collections.Counter()
    pending = None
    with open(map_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            only = SECTION_ONLY.match(line)
            if only:
                pending = only.group(1)
                continue
            found = INPUT_LINE.match(line)
            if not found:
                pending = None
                continue
            section = found.group(1) or pending
            pending = None
            address = int(found.group(2), 16)
            size = int(found.group(3), 16)
            path = found.group(4)
            if not section or address == 0 or not section.startswith(ALLOC_PREFIXES):
                continue
            kind = "bss" if "bss" in section else "img"
            origin = origin_of(path)
            totals[(origin, kind)] += size
            if origin == "libc" and kind == "img":
                member = MEMBER.search(path)
                if member is None:
                    raise ValueError(f"libc input without a member name: {path}")
                members[member.group(1)] += size
    return totals, members


def main() -> int:
    """Print the summary line and write the member table."""
    if len(sys.argv) != 4:
        sys.stderr.write(__doc__ or "")
        return 2
    map_path, name, members_out = sys.argv[1:]
    totals, members = summarize(map_path)
    print(
        f"{name} own_img={totals[('own', 'img')]} libc_img={totals[('libc', 'img')]}"
        f" crt0={totals[('crt0', 'img')]} own_bss={totals[('own', 'bss')]}"
        f" libc_bss={totals[('libc', 'bss')]} members={len(members)}"
    )
    with open(members_out, "w", encoding="utf-8") as out:
        for member, size in sorted(members.items()):
            out.write(f"{member} {size}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
