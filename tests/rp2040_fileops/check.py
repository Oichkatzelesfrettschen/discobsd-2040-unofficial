"""Calibrate linked file-operation tables and their indirect roots."""

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
sys.path.insert(0, str(ROOT / "tools"))
import check_rp2040_fileops as checker  # noqa: E402


def offset(elf, name):
    entries = elf.get_section_by_name(".symtab").get_symbol_by_name(name)
    if entries is None or len(entries) != 1:
        raise AssertionError(f"missing or duplicate {name}")
    item = entries[0].entry
    section = elf.get_section(item.st_shndx)
    return section.header.sh_offset + item.st_value - section.header.sh_addr


def check_source(text, name, expected):
    match = re.search(r"\b" + name + r"\[\]\s*=\s*\{([^}]*)\}" if name == "Fops"
                      else r"\b" + name + r"\s*=\s*\{([^}]*)\}", text, re.DOTALL)
    if match is None:
        raise ValueError(f"missing {name} initializer")
    values = tuple(value.strip().removeprefix("&") for value in match.group(1).split(","))
    if values != expected:
        raise ValueError(f"{name} initializer differs")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", required=True, type=Path)
    args = parser.parse_args()
    sources = {"Fops": "sys/kern/sys_generic.c", "socketops": "sys/kern/sys_generic.c",
               "inodeops": "sys/kern/sys_inode.c", "pipeops": "sys/kern/sys_pipe.c"}
    for name, expected in checker.TABLES.items():
        source = (ROOT / sources[name]).read_text(encoding="utf-8")
        check_source(source, name, expected)
        changed = re.sub(r"(\b" + name + r"(?:\[\])?\s*=\s*\{[^}]*?)" +
                         re.escape(expected[-1]) + r"\b", r"\1unexpected_handler", source, count=1)
        if changed == source:
            raise AssertionError(f"{name} mutation anchor is absent")
        try:
            check_source(changed, name, expected)
        except ValueError:
            pass
        else:
            raise AssertionError(f"{name} source mutation survived")
    original = args.kernel.read_bytes()
    checker.check_data(original)
    elf = ELFFile(io.BytesIO(original))
    function_slots = []
    for name in ("inodeops", "socketops", "pipeops"):
        start = offset(elf, name)
        function_slots.append(struct.unpack_from("<4I", original, start))
    object_slots = struct.unpack_from("<4I", original, offset(elf, "Fops"))
    for name, expected in checker.TABLES.items():
        start = offset(elf, name)
        for index in range(len(expected)):
            changed = bytearray(original)
            current = struct.unpack_from("<I", changed, start + index * 4)[0]
            alternatives = (object_slots[1:] if name == "Fops" else
                            (slots[index] for slots in function_slots))
            alternate = next(value for value in alternatives if value != current)
            struct.pack_into("<I", changed, start + index * 4, alternate)
            try:
                checker.check_data(changed)
            except ValueError as error:
                if f"{name} field {index} differs" not in str(error):
                    raise AssertionError(f"unexpected diagnostic: {error}") from error
            else:
                raise AssertionError(f"{name} field {index} mutant survived")
            if name != "Fops":
                changed = bytearray(original)
                struct.pack_into("<I", changed, start + index * 4, current & ~1)
                try:
                    checker.check_data(changed)
                except ValueError as error:
                    if f"{name} field {index} differs" not in str(error):
                        raise AssertionError(f"unexpected diagnostic: {error}") from error
                else:
                    raise AssertionError(f"{name} field {index} Thumb mutant survived")
    with tempfile.TemporaryDirectory(prefix="rp2040-fileops-") as directory:
        mutant = Path(directory) / "truncated.elf"
        mutant.write_bytes(original[:80])
        result = subprocess.run([sys.executable, str(ROOT / "tools/check_rp2040_fileops.py"),
                                 str(mutant)], capture_output=True, text=True, check=False)
        if result.returncode != 1:
            raise AssertionError(f"truncated ELF exit {result.returncode}")
    print("PASS RP2040 fileops: source, 16 linked fields and truncation controls")


if __name__ == "__main__":
    main()
