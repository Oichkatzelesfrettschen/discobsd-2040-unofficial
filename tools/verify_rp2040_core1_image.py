"""Hold the code RP2040 core 1 executes in a linked kernel to SRAM.

A flash erase or program takes execute-in-place down for both cores, so one
instruction or literal core 1 fetched from flash during that window would be
undefined. Starting from the three functions core 1 enters by address
(sys/arch/rp2040/rp2040/core1.c: its entry, its exception handler and its
ring handler), the verifier follows every direct call and requires that

  - each function reached lies in SRAM;
  - each direct branch and call lands in SRAM;
  - no literal-pool word of those functions is an address in the
    execute-in-place window;
  - the only indirect call is the ring consumer's call of its handler, whose
    address is one of those literals.

A kernel configured without the core 1 option has none of the entry
symbols and is reported as carrying no image. The exit status is 0 when
every image named holds, 1 on a finding, 2 on a malformed input.
"""

from __future__ import annotations

import argparse
import bisect
import struct
import sys
from pathlib import Path

try:
    from capstone import (  # type: ignore[import-not-found]
        CS_ARCH_ARM,
        CS_MODE_LITTLE_ENDIAN,
        CS_MODE_MCLASS,
        CS_MODE_THUMB,
        Cs,
    )
    from capstone.arm import ARM_OP_IMM  # type: ignore[import-not-found]
    from elftools.elf.elffile import ELFFile  # type: ignore[import-not-found]
except ImportError as import_error:
    print("core 1 image verifier requires Capstone and pyelftools", file=sys.stderr)
    raise SystemExit(2) from import_error

SRAM_FIRST = 0x20000000
SRAM_LIMIT = 0x20042000
XIP_FIRST = 0x10000000
XIP_LIMIT = 0x20000000
ROOTS = ("core1_main", "core1_fault", "core1_op")
INDIRECT_CALL_OWNER = "cring_service"


class ImageError(Exception):
    """The ELF cannot be read as a linked kernel."""


def in_sram(address: int) -> bool:
    return SRAM_FIRST <= address < SRAM_LIMIT


def in_xip(address: int) -> bool:
    return XIP_FIRST <= address < XIP_LIMIT


class Image:
    def __init__(self, path: Path) -> None:
        self.path = path
        with path.open("rb") as stream:
            elf = ELFFile(stream)
            symtab = elf.get_section_by_name(".symtab")
            if symtab is None:
                raise ImageError(f"{path}: no symbol table")
            self.functions: dict[str, tuple[int, int]] = {}
            self.by_address: dict[int, str] = {}
            mapping: list[tuple[int, str]] = []
            for symbol in symtab.iter_symbols():
                name = symbol.name
                value = symbol["st_value"]
                if name.startswith(("$t", "$d")):
                    mapping.append((value, name[:2]))
                elif symbol["st_info"]["type"] == "STT_FUNC":
                    start = value & ~1
                    self.functions[name] = (start, symbol["st_size"])
                    self.by_address.setdefault(start, name)
            mapping.sort()
            self.map_addresses = [address for address, _ in mapping]
            self.map_kinds = [kind for _, kind in mapping]
            self.segments = [
                (segment["p_vaddr"], segment.data())
                for segment in elf.iter_segments()
                if segment["p_type"] == "PT_LOAD" and segment["p_filesz"]
            ]

    def read(self, address: int, size: int) -> bytes:
        for base, data in self.segments:
            if base <= address and address + size <= base + len(data):
                return data[address - base : address - base + size]
        raise ImageError(f"{self.path}: {address:#x} is in no loaded segment")

    def is_code(self, address: int) -> bool:
        index = bisect.bisect_right(self.map_addresses, address) - 1
        return index >= 0 and self.map_kinds[index] == "$t"

    def find(self, prefix: str) -> str | None:
        """The symbol named prefix, or its compiler-specialized clone."""
        if prefix in self.functions:
            return prefix
        clones = sorted(name for name in self.functions if name.startswith(prefix + "."))
        return clones[0] if clones else None


def verify(image: Image) -> tuple[list[str], list[str]]:
    """Return (functions reached, findings)."""
    roots = [image.find(root) for root in ROOTS]
    if all(root is None for root in roots):
        return [], []
    findings: list[str] = []
    for wanted, found in zip(ROOTS, roots):
        if found is None:
            findings.append(f"entry {wanted} is missing")
    decoder = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS | CS_MODE_LITTLE_ENDIAN)
    decoder.detail = True
    pending = [root for root in roots if root is not None]
    reached: list[str] = []
    while pending:
        name = pending.pop()
        if name in reached:
            continue
        reached.append(name)
        start, size = image.functions[name]
        if not in_sram(start) or not in_sram(start + size - 1):
            findings.append(f"{name} at {start:#x} lies outside SRAM")
            continue
        if size == 0:
            findings.append(f"{name} has no size")
            continue
        body = image.read(start, size)
        offset = 0
        while offset < size:
            address = start + offset
            if not image.is_code(address):
                if address % 4 == 0 and offset + 4 <= size:
                    (word,) = struct.unpack_from("<I", body, offset)
                    if in_xip(word & ~1):
                        findings.append(
                            f"{name}: literal at {address:#x} is the flash "
                            f"address {word:#x}"
                        )
                    offset += 4
                else:
                    offset += 2
                continue
            decoded = list(decoder.disasm(body[offset : offset + 4], address, 1))
            if not decoded:
                findings.append(f"{name}: undecodable instruction at {address:#x}")
                break
            instruction = decoded[0]
            offset += instruction.size
            mnemonic = instruction.mnemonic.split(".")[0]
            if mnemonic in ("blx", "bx"):
                if instruction.op_str == "lr":
                    continue
                if not name.startswith(INDIRECT_CALL_OWNER):
                    findings.append(
                        f"{name}: indirect {mnemonic} {instruction.op_str} "
                        f"at {address:#x}"
                    )
                continue
            if mnemonic != "bl" and not (mnemonic.startswith("b") and
                    instruction.operands and
                    instruction.operands[-1].type == ARM_OP_IMM and
                    mnemonic not in ("bics", "bkpt", "bic")):
                if mnemonic in ("svc", "bkpt", "udf"):
                    findings.append(f"{name}: {mnemonic} at {address:#x}")
                continue
            target = instruction.operands[-1].imm & 0xFFFFFFFE
            if not in_sram(target):
                callee = image.by_address.get(target, "?")
                findings.append(
                    f"{name}: {mnemonic} at {address:#x} leaves SRAM for "
                    f"{target:#x} <{callee}>"
                )
                continue
            if mnemonic == "bl":
                callee_name = image.by_address.get(target)
                if callee_name is None:
                    findings.append(
                        f"{name}: bl at {address:#x} to {target:#x}, "
                        f"which no function symbol names"
                    )
                else:
                    pending.append(callee_name)
    return sorted(reached), findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("elf", nargs="+", type=Path)
    parser.add_argument(
        "--require-image",
        action="store_true",
        help="fail when an ELF carries no core 1 image",
    )
    arguments = parser.parse_args()
    status = 0
    for path in arguments.elf:
        try:
            image = Image(path)
            reached, findings = verify(image)
        except (ImageError, OSError, ValueError) as error:
            print(f"core1-image: {error}", file=sys.stderr)
            return 2
        if not reached and not findings:
            if arguments.require_image:
                print(f"FAIL core1-image {path}: no core 1 image")
                status = 1
            else:
                print(f"core1-image {path}: no core 1 image")
            continue
        if findings:
            status = 1
            for finding in findings:
                print(f"FAIL core1-image {path}: {finding}")
            continue
        span = sum(image.functions[name][1] for name in reached)
        print(
            f"PASS core1-image {path}: {len(reached)} functions, {span} bytes, "
            f"all in SRAM: {' '.join(reached)}"
        )
    return status


if __name__ == "__main__":
    raise SystemExit(main())
