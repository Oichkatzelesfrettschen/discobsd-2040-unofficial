"""Fail when a linked RP2040 kernel leaves too little room in the FLASH region.

Usage: check_kernel_headroom.py --self-test
       check_kernel_headroom.py [--linker-script LD] [--region NAME]
                                [--margin BYTES] ELF...

The FLASH region comes from the MEMORY block of the linker script
(sys/arch/rp2040/conf/RP2040.ld). The flash image of a kernel is the span from
the region origin to the end of the last PT_LOAD segment whose load address
lies in the QSPI flash window from that origin: .text, the .data initializers
and the .ramfunc initializers all load there, while .boot2 (BOOT2 region) and
the RAM-only segments do not. A segment loaded past the region end counts as
overflow. Headroom is the region length less that span. The tool exits 0 when
every kernel keeps at least MARGIN bytes, 1 when one falls below it, and 2 when
an input cannot be read or does not describe a kernel. ld bounds .text and
.data by the FLASH region, but the .ramfunc initializers load at an AT()
expression that names no region (conf/kern.ldscript), so a kernel can link with
its .ramfunc image past the end of FLASH; the span measured here includes them.
The gate also reports the kernels that link with less room than a further
feature or a different compiler needs.

--self-test accepts and rejects synthetic images at each boundary of the
comparison and of the ELF and linker-script parsers.
"""

from __future__ import annotations

import argparse
import functools
import re
import struct
import sys
from collections.abc import Callable
from pathlib import Path

DEFAULT_LINKER_SCRIPT = (
    Path(__file__).resolve().parent.parent / "sys/arch/rp2040/conf/RP2040.ld"
)
DEFAULT_REGION = "FLASH"

# The measured compiler spread of the PICOCALC_PSRAM kernel at -Os is 592 bytes
# between GCC 13.2 and GCC 16.2 (docs/research/picocalc-session-handoff.md);
# one KiB covers that spread and leaves room for a macOS toolchain that lands
# between them.
DEFAULT_MARGIN = 1024

# The XIP window maps the QSPI flash from 0x10000000 to 0x11000000
# (RP2040 datasheet 2.6.3); anything loaded there is flash content.
XIP_START = 0x10000000
XIP_END = 0x11000000

ELF_MAGIC = b"\x7fELF"
ELFCLASS32, ELFDATA2LSB, EM_ARM = 1, 1, 40
PT_LOAD = 1
EHDR = struct.Struct("<16sHHIIIIIHHHHHH")
PHDR = struct.Struct("<8I")

SUFFIX = {"": 1, "K": 1024, "M": 1024 * 1024}
TOKEN = re.compile(r"\s*(?:(0[xX][0-9a-fA-F]+|[0-9]+)([KM]?)|([+-]))")


class InputError(Exception):
    """An input is unreadable or does not describe the expected object."""


def evaluate(expression: str) -> int:
    """Evaluate the sums and differences of sized numbers an ld MEMORY uses."""
    text = expression.strip()
    if not text:
        raise InputError("empty linker expression")
    position, total, sign = 0, 0, 1
    expect_number, leading = True, True
    while position < len(text):
        match = TOKEN.match(text, position)
        if match is None:
            raise InputError(f"unsupported linker expression: {expression!r}")
        number, suffix, operator = match.groups()
        position = match.end()
        if number is not None:
            if not expect_number:
                raise InputError(f"missing operator in {expression!r}")
            total += sign * int(number, 0) * SUFFIX[suffix]
            expect_number = False
        elif expect_number:
            if operator != "-" or not leading:
                raise InputError(f"missing operand in {expression!r}")
            sign = -1
        else:
            sign = 1 if operator == "+" else -1
            expect_number = True
        leading = False
    if expect_number:
        raise InputError(f"trailing operator in {expression!r}")
    return total


def region_bounds(script: str, name: str) -> tuple[int, int]:
    """Return (origin, length) of the named MEMORY region."""
    stripped = re.sub(r"/\*.*?\*/", "", script, flags=re.DOTALL)
    pattern = re.compile(
        rf"^\s*{re.escape(name)}\s*(?:\([^)]*\))?\s*:\s*"
        r"ORIGIN\s*=\s*([^,]+),\s*LENGTH\s*=\s*([^\n]+)$",
        re.MULTILINE,
    )
    matches = pattern.findall(stripped)
    if len(matches) != 1:
        raise InputError(f"linker script defines {len(matches)} {name} regions")
    origin, length = evaluate(matches[0][0]), evaluate(matches[0][1])
    if origin < 0 or length <= 0:
        raise InputError(f"{name} origin {origin} or length {length} is invalid")
    return origin, length


def flash_image_end(data: bytes, origin: int) -> int:
    """Return the highest end address of a PT_LOAD segment loaded in flash."""
    if len(data) < EHDR.size or data[:4] != ELF_MAGIC:
        raise InputError("not an ELF file")
    (ident, _type, machine, _version, _entry, phoff, _shoff, _flags, _ehsize,
     phentsize, phnum, *_rest) = EHDR.unpack_from(data)
    if ident[4] != ELFCLASS32 or ident[5] != ELFDATA2LSB or machine != EM_ARM:
        raise InputError("not a 32-bit little-endian ARM ELF file")
    if phentsize != PHDR.size or phnum == 0:
        raise InputError("program header table is absent or has an odd entry size")
    if phoff + phnum * phentsize > len(data):
        raise InputError("program header table is truncated")
    spans: list[tuple[int, int]] = []
    for index in range(phnum):
        (kind, offset, _vaddr, paddr, filesz, memsz, _flags,
         _align) = PHDR.unpack_from(data, phoff + index * phentsize)
        if kind == PT_LOAD and filesz > memsz:
            raise InputError(f"segment {index} file size exceeds memory size")
        if kind == PT_LOAD and filesz != 0 and offset + filesz > len(data):
            raise InputError(f"segment {index} payload is truncated")
        if kind == PT_LOAD and filesz != 0 and XIP_START <= paddr < XIP_END:
            if paddr + filesz > XIP_END:
                raise InputError(f"segment {index} crosses the XIP window end")
            spans.append((paddr, paddr + filesz))
    if not any(end > origin for _start, end in spans):
        raise InputError("no loadable segment lies in flash")
    spans.sort()
    for previous, current in zip(spans, spans[1:]):
        if current[0] < previous[1]:
            raise InputError("overlapping flash load segments")
    return spans[-1][1]


def within_margin(room: int, margin: int) -> bool:
    """A kernel passes when its headroom is at least the margin."""
    return room >= margin


def failed_map_end(text: str) -> int:
    """Read the declared .ramfunc load end from one GNU ld map, for analysis."""
    rows = re.findall(
        r"^\.ramfunc\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)"
        r"\s+load address\s+0x([0-9a-fA-F]+)\s*$", text, re.MULTILINE,
    )
    if len(rows) != 1:
        raise InputError(f"expected one .ramfunc load row, found {len(rows)}")
    size, address = (int(value, 16) for value in rows[0])
    if not 0x10000000 <= address < XIP_END or address + size > XIP_END:
        raise InputError(".ramfunc load interval lies outside the XIP window")
    return address + size


def measure(path: Path, origin: int, length: int) -> tuple[int, int]:
    """Return (flash image bytes, headroom bytes) for one kernel ELF."""
    try:
        data = path.read_bytes()
    except OSError as error:
        raise InputError(f"{path}: {error.strerror}") from error
    try:
        used = flash_image_end(data, origin) - origin
    except InputError as error:
        raise InputError(f"{path}: {error}") from error
    return used, length - used


def synthetic_elf(segments: list[tuple[int, int, int]], machine: int = EM_ARM,
                  ident_class: int = ELFCLASS32, ident_data: int = ELFDATA2LSB,
                  truncate: int = 0, payload: bool = True) -> bytes:
    """Build an ELF header, program headers and, unless payload is false, the
    file bytes each segment declares: segments are (type, paddr, filesz)."""
    ident = ELF_MAGIC + bytes([ident_class, ident_data, 1]) + bytes(9)
    header = EHDR.pack(ident, 2, machine, 1, 0, EHDR.size, 0, 0, EHDR.size,
                       PHDR.size, len(segments), 0, 0, 0)
    offset = EHDR.size + PHDR.size * len(segments)
    entries = []
    for kind, paddr, filesz in segments:
        entries.append(PHDR.pack(kind, offset, paddr, paddr, filesz, filesz, 5, 4))
        offset += filesz
    image = header + b"".join(entries)
    if payload:
        image += bytes(sum(filesz for _kind, _paddr, filesz in segments))
    return image[:len(image) - truncate] if truncate else image


def self_test() -> None:
    origin, length = 0x10000100, 128 * 1024 - 256
    flash_end = origin + length
    cases = 0

    def expect(condition: bool, label: str) -> None:
        nonlocal cases
        if not condition:
            raise AssertionError(label)
        cases += 1

    def rejects(function: Callable[[], object], label: str) -> None:
        nonlocal cases
        try:
            function()
        except InputError:
            cases += 1
            return
        raise AssertionError(label)

    expect(evaluate("128K - 256") == 130816, "difference of a sized number")
    expect(evaluate("0x10000100") == 0x10000100, "hexadecimal origin")
    expect(evaluate("1M + 2K") == 1024 * 1024 + 2048, "sum of sized numbers")
    expect(evaluate("-256 + 1K") == 768, "leading minus")
    for bad in ("", "128K 256", "128K -", "128K * 2", "+ 4"):
        rejects(functools.partial(evaluate, bad), f"expression {bad!r}")

    script = (
        "MEMORY {\n"
        "    BOOT2   (rx)  : ORIGIN = 0x10000000, LENGTH = 256 /* stage 2 */\n"
        "    FLASH   (rx)  : ORIGIN = 0x10000100, LENGTH = 128K - 256\n"
        "    FSFLASH (r)   : ORIGIN = 0x10020000, LENGTH = 1536K\n"
        "}\n"
    )
    expect(region_bounds(script, "FLASH") == (origin, length), "region parse")
    expect(region_bounds(script, "BOOT2") == (0x10000000, 256), "comment in line")
    rejects(lambda: region_bounds(script, "RAM"), "absent region")
    rejects(lambda: region_bounds(script + script, "FLASH"), "duplicate region")
    expect(region_bounds(DEFAULT_LINKER_SCRIPT.read_text(), DEFAULT_REGION)[1] > 0,
           "the repository linker script defines the region")

    boot2 = (PT_LOAD, 0x10000000, 256)
    ram_only = (PT_LOAD, 0x20024000, 0x40000)
    exidx = (0x70000001, origin + 0x1000, 8)

    def headroom(segments: list[tuple[int, int, int]]) -> int:
        used = flash_image_end(synthetic_elf(segments), origin) - origin
        return length - used

    text = (PT_LOAD, origin, 100000)
    data = (PT_LOAD, origin + 100000, 424)
    ramfunc = (PT_LOAD, origin + 100424, 664)
    expect(headroom([boot2, text, data, ramfunc, ram_only, exidx]) ==
           length - 101088, "the span ends at the last flash-loaded segment")
    expect(headroom([ramfunc, data, text]) == length - 101088, "segment order")
    expect(headroom([(PT_LOAD, origin, length)]) == 0, "a full region leaves none")
    expect(headroom([(PT_LOAD, origin, length + 4)]) == -4, "overflow is negative")
    expect(headroom([text, (PT_LOAD, origin + 1000, 0)]) == length - 100000,
           "an empty segment adds nothing")
    expect(headroom([text, (PT_LOAD, flash_end, 16)]) == -16,
           "a segment loaded past the region end is overflow")
    expect(headroom([text, (PT_LOAD, XIP_END, 16)]) == length - 100000,
           "a load address beyond the flash window is not flash")
    expect(flash_image_end(synthetic_elf([text]) + bytes(1024 * 1024), origin) ==
           origin + 100000, "unloaded trailing debug data adds no flash bytes")
    nobits = bytearray(synthetic_elf([text]))
    header = list(PHDR.unpack_from(nobits, EHDR.size))
    header[5] += 4096
    PHDR.pack_into(nobits, EHDR.size, *header)
    expect(flash_image_end(nobits, origin) == origin + 100000,
           "memory-only segment tail adds no flash bytes")
    rejects(lambda: headroom([text, (PT_LOAD, origin + 99999, 2)]),
            "overlapping flash payloads")
    rejects(lambda: headroom([(PT_LOAD, origin - 4, 8), (PT_LOAD, origin, 8)]),
            "a segment crossing into FLASH overlaps the next segment")
    rejects(lambda: headroom([(PT_LOAD, origin - 256, 8),
                              (PT_LOAD, origin - 252, 8), text]),
            "overlapping boot-stage payloads")
    rejects(lambda: headroom([(PT_LOAD, XIP_END - 4, 8)]),
            "flash payload crossing the XIP window end")
    expect(headroom([(PT_LOAD, origin - 4, 8)]) == length - 4,
           "a load crossing into FLASH contributes its final bytes")
    bad_segment = bytearray(synthetic_elf([text]))
    header = list(PHDR.unpack_from(bad_segment, EHDR.size))
    header[5] = header[4] - 1
    PHDR.pack_into(bad_segment, EHDR.size, *header)
    rejects(lambda: flash_image_end(bad_segment, origin),
            "file bytes exceeding segment memory size")
    map_line = ".ramfunc 0x20030100 0x1a0 load address 0x1002556c\n"
    expect(failed_map_end(map_line) == 0x1002570c, "failed map final ramfunc end")
    rejects(lambda: failed_map_end(map_line + map_line), "duplicate ramfunc map row")
    rejects(lambda: failed_map_end(".ramfunc 0x20030100 0x10\n"),
            "missing ramfunc load address")

    for label, blob in (
        ("empty file", b""),
        ("bad magic", b"\x00" + synthetic_elf([text])[1:]),
        ("ELF64", synthetic_elf([text], ident_class=2)),
        ("big endian", synthetic_elf([text], ident_data=2)),
        ("x86 machine", synthetic_elf([text], machine=3)),
        ("no program headers", synthetic_elf([])),
        ("truncated table", synthetic_elf([text, data], truncate=8, payload=False)),
        ("headers without payload", synthetic_elf([text, data], payload=False)),
        ("truncated payload", synthetic_elf([text, data], truncate=1)),
        ("no flash segment", synthetic_elf([boot2, ram_only])),
    ):
        rejects(functools.partial(flash_image_end, blob, origin), label)

    margin = DEFAULT_MARGIN
    for used, expected in ((length - margin - 1, True), (length - margin, True),
                           (length - margin + 1, False), (length + 8, False)):
        room = length - used
        expect(within_margin(room, margin) is expected, f"comparison at headroom {room}")
    print(f"PASS kernel-headroom self-test: {cases} cases")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--linker-script", type=Path, default=DEFAULT_LINKER_SCRIPT)
    parser.add_argument("--region", default=DEFAULT_REGION)
    parser.add_argument("--margin", type=int, default=DEFAULT_MARGIN,
                        help="minimum headroom in bytes (default %(default)s)")
    parser.add_argument("--failed-map", type=Path,
                        help="analyze a failed GNU ld map without admitting an image")
    parser.add_argument("--snapshot", help="source identity label for failed-map analysis")
    parser.add_argument("elf", nargs="*", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.self_test:
            self_test()
            return 0
        if args.failed_map and (not args.snapshot or args.elf):
            parser.error("--failed-map requires --snapshot and excludes ELF inputs")
        if args.snapshot and not args.failed_map:
            parser.error("--snapshot requires --failed-map")
        if not args.elf and not args.failed_map:
            parser.error("name at least one kernel ELF file")
        try:
            script = args.linker_script.read_text()
        except OSError as error:
            raise InputError(f"{args.linker_script}: {error.strerror}") from error
        origin, length = region_bounds(script, args.region)
        if args.failed_map:
            try:
                map_text = args.failed_map.read_text(encoding="utf-8")
            except OSError as error:
                raise InputError(f"{args.failed_map}: {error.strerror}") from error
            end = failed_map_end(map_text)
            print(f"ANALYSIS failed-map snapshot={args.snapshot} end=0x{end:x} "
                  f"headroom={origin + length - end} bytes; ELF admission not performed")
            return 0
        failed = False
        for path in args.elf:
            used, room = measure(path, origin, length)
            verdict = "PASS" if within_margin(room, args.margin) else "FAIL"
            failed = failed or verdict == "FAIL"
            print(f"{verdict} kernel-headroom {path}: flash image {used} of "
                  f"{length} bytes, headroom {room} bytes, margin {args.margin}")
        return 1 if failed else 0
    except (InputError, AssertionError) as error:
        print(f"ERROR kernel-headroom: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
