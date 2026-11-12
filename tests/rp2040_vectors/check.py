"""Calibrate linked RP2040 exception-vector pointer verification."""

import argparse
import io
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "tools/check_rp2040_vectors.py"
sys.path.insert(0, str(ROOT / "tools"))
from check_rp2040_vectors import SLOTS  # noqa: E402


def check_source(text):
    table = text.split("g_pfnVectors:", 1)[1].split(".size\tg_pfnVectors", 1)[0]
    names = re.findall(r"^\s*\.word\s+(\w+)", table, re.MULTILINE)
    if tuple(None if name == "0" else name for name in names) != SLOTS:
        raise ValueError("source vector slots differ from the verifier contract")


def run(path, expected, diagnostic=None):
    result = subprocess.run([sys.executable, str(CHECKER), str(path)],
                            text=True, capture_output=True, check=False)
    if result.returncode != expected:
        raise AssertionError(f"vector checker exit {result.returncode}: {result.stderr}")
    if diagnostic is not None and diagnostic not in result.stderr:
        raise AssertionError(f"vector checker diagnostic differs: {result.stderr}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, type=Path)
    args = parser.parse_args()
    source = (ROOT / "sys/arch/rp2040/rp2040/locore0.S").read_text(encoding="utf-8")
    check_source(source)
    mutant_source = source.replace(".word\tUSBCTRL_IRQ_Handler", ".word\tXIP_IRQ_Handler", 1)
    if mutant_source == source:
        raise AssertionError("source mutation anchor is absent")
    try:
        check_source(mutant_source)
    except ValueError:
        pass
    else:
        raise AssertionError("wrong source handler was accepted")
    original = args.kernel.read_bytes()
    elf = ELFFile(io.BytesIO(original))
    symbol = elf.get_section_by_name(".symtab").get_symbol_by_name("g_pfnVectors")[0]
    section = elf.get_section(symbol.entry.st_shndx)
    offset = section.header.sh_offset + symbol.entry.st_value - section.header.sh_addr
    words = struct.unpack_from("<48I", original, offset)
    targets = {value for value in words[1:] if value}
    if len(targets) < 2:
        raise AssertionError("fixture needs two distinct linked interrupt targets")
    run(args.kernel, 0)
    with tempfile.TemporaryDirectory(prefix="rp2040-vector-fixture-") as directory:
        mutant = Path(directory) / "mutant.elf"
        for index, value in enumerate(words):
            if index == 0:
                replacement = value ^ 4
            else:
                replacement = next(target for target in targets if target != value)
            modified = bytearray(original)
            struct.pack_into("<I", modified, offset + index * 4, replacement)
            mutant.write_bytes(modified)
            diagnostic = f"reserved vector {index} is nonzero" if SLOTS[index] is None else (
                f"vector {index} differs from {SLOTS[index]}")
            run(mutant, 1, diagnostic)
        for index, value in enumerate(words[1:], 1):
            if value:
                modified = bytearray(original)
                struct.pack_into("<I", modified, offset + index * 4, value & ~1)
                mutant.write_bytes(modified)
                run(mutant, 1, f"vector {index} differs from {SLOTS[index]}")
        mutant.write_bytes(original[:80])
        run(mutant, 1)
    print("PASS RP2040 vectors: linked control, all slots and Thumb-bit mutants")


if __name__ == "__main__":
    main()
