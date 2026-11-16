"""Calibrate divider alias extents without changing code or relocations."""

import hashlib
import io
import json
import struct
import subprocess
import sys
import uuid
from pathlib import Path
from typing import TypedDict

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
UNSIGNED = ("__aeabi_uidiv", "__aeabi_uidivmod")
SIGNED = ("__aeabi_idiv", "__aeabi_idivmod")


class Receipt(TypedDict):
    source_sha256: str
    status: str
    cases: list[dict[str, str]]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def main() -> None:
    require(len(sys.argv) == 3, "usage: helper_extent_mutants.py VERIFIER HELPER.o")
    verifier = Path(sys.argv[1]).resolve()
    source = Path(sys.argv[2]).resolve()
    original = source.read_bytes()
    elf = ELFFile(io.BytesIO(original))
    require(
        elf.elfclass == 32
        and elf.little_endian
        and elf["e_machine"] == "EM_ARM"
        and elf["e_type"] == "ET_REL",
        "extent controls require little-endian ARM ELF32 relocatable input",
    )
    table = elf.get_section_by_name(".symtab")
    text = elf.get_section_by_name(".text")
    if not isinstance(table, SymbolTableSection) or table["sh_entsize"] != 16 or text is None:
        raise ValueError("extent control object lacks its symbol table or text")
    symbols = {}
    for index, symbol in enumerate(table.iter_symbols()):
        if symbol.name in UNSIGNED + SIGNED:
            require(symbol.name not in symbols, "duplicate helper symbol")
            symbols[symbol.name] = (index, symbol)
    require(set(symbols) == set(UNSIGNED + SIGNED), "extent control helper inventory differs")
    unsigned_size = symbols[UNSIGNED[0]][1]["st_size"]
    signed_size = symbols[SIGNED[0]][1]["st_size"]
    require(unsigned_size > 2 and signed_size > 2, "positive helper extents are absent")
    other = symbols[SIGNED[0]][1]
    cases = (
        (
            "missing",
            {UNSIGNED[1]: {2: 0}},
            f"helper extent {UNSIGNED[1]} is 0, want {unsigned_size}",
        ),
        (
            "short",
            {UNSIGNED[1]: {2: unsigned_size - 2}},
            f"helper extent {UNSIGNED[1]} is {unsigned_size - 2}, want {unsigned_size}",
        ),
        (
            "overlap",
            {name: {2: unsigned_size + 2} for name in UNSIGNED},
            f"helper extent {UNSIGNED[0]} is {unsigned_size + 2}, want {unsigned_size}",
        ),
        (
            "escape",
            {name: {2: text["sh_size"] + 2} for name in SIGNED},
            f"helper extent {SIGNED[0]} is {text['sh_size'] + 2}, want {signed_size}",
        ),
        (
            "alias-owner",
            {UNSIGNED[1]: {1: other["st_value"], 2: signed_size}},
            f"helper alias owner differs: {UNSIGNED[0]}/{UNSIGNED[1]}",
        ),
        (
            "thumb-entry",
            {UNSIGNED[1]: {1: symbols[UNSIGNED[1]][1]["st_value"] & ~1}},
            f"helper Thumb entry differs: {UNSIGNED[1]}",
        ),
    )
    work = ROOT / ".build-logs" / ("divider-extent-controls-" + uuid.uuid4().hex)
    work.mkdir(parents=True)
    receipt: Receipt = {
        "source_sha256": hashlib.sha256(original).hexdigest(),
        "status": "failed",
        "cases": [],
    }
    try:
        for name, changes, expected in cases:
            data = bytearray(original)
            for symbol_name, fields in changes.items():
                index = symbols[symbol_name][0]
                offset = table["sh_offset"] + index * 16
                entry = list(struct.unpack_from("<IIIBBH", data, offset))
                for field, value in fields.items():
                    entry[field] = value
                struct.pack_into("<IIIBBH", data, offset, *entry)
            require(data != original, "extent control changes no metadata: " + name)
            changed = ELFFile(io.BytesIO(data))
            for before, after in zip(elf.iter_sections(), changed.iter_sections()):
                if before.name != ".symtab":
                    require(before.data() == after.data(), "control changes section bytes: " + name)
            path = work / (name + ".o")
            path.write_bytes(data)
            result = subprocess.run(
                [sys.executable, str(verifier), "--user-helper", str(path)],
                capture_output=True,
                timeout=30,
                check=False,
            )
            (work / (name + ".stdout")).write_bytes(result.stdout)
            (work / (name + ".stderr")).write_bytes(result.stderr)
            require(
                result.returncode == 1 and expected.encode() in result.stderr,
                "extent control rejection differs: " + name,
            )
            receipt["cases"].append(
                {"name": name, "expected": expected, "sha256": hashlib.sha256(data).hexdigest()}
            )
            print("helper extent control " + name + ": rejected", flush=True)
        receipt["status"] = "passed"
    finally:
        (work / "receipt.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    print(f"PASS divider extents: {len(cases)} metadata controls; {work.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
