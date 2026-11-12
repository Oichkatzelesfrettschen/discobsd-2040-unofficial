"""Calibrate linked terminal output callback references."""

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
import check_rp2040_tty_roots as checker  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    build = args.build_dir
    data = (build / "unix.elf").read_bytes()
    objects = {name: (build / f"{name}.o").read_bytes() for name in ("uart", "usb", "lcdcons")}
    ioconf = (build / "ioconf.o").read_bytes()
    roots = checker.check_data(data, ioconf, objects)
    elf = ELFFile(io.BytesIO(data))
    symbols = elf.get_section_by_name(".symtab")
    for _driver, component, opener, callback, literal in roots:
        source = (ROOT / "sys/arch/rp2040/dev" / f"{component}.c").read_text(encoding="utf-8")
        assignment = rf"\btp\s*->\s*t_oproc\s*=\s*{callback}\s*;"
        if len(re.findall(assignment, source)) != 1:
            raise AssertionError(f"{component} source assignment differs")
        changed_source = re.sub(assignment, "tp->t_oproc = wrongstart;", source, count=1)
        if changed_source == source or re.search(assignment, changed_source):
            raise AssertionError(f"{component} source assignment mutant survived")
        mutant = bytearray(data)
        section = elf.get_section_by_name(".text")
        file_offset = section.header.sh_offset + literal - section.header.sh_addr
        current = struct.unpack_from("<I", mutant, file_offset)[0]
        struct.pack_into("<I", mutant, file_offset, current ^ 4)
        reject(mutant, ioconf, objects, f"{opener} callback literal differs")
        mutant = bytearray(data)
        struct.pack_into("<I", mutant, file_offset, current & ~1)
        reject(mutant, ioconf, objects, f"{opener} callback literal differs")
        other_callback = next(item[3] for item in roots if item[3] != callback)
        alternate = checker.symbol(symbols, other_callback, "STT_FUNC")
        mutant = bytearray(data)
        struct.pack_into("<I", mutant, file_offset, alternate)
        reject(mutant, ioconf, objects, f"{opener} callback literal differs")
    with tempfile.TemporaryDirectory(prefix="rp2040-tty-roots-") as directory:
        mutant = Path(directory) / "truncated.elf"
        mutant.write_bytes(data[:80])
        result = subprocess.run([sys.executable, str(ROOT / "tools/check_rp2040_tty_roots.py"),
                                 "--build-dir", str(build), "--kernel", str(mutant)],
                                capture_output=True, text=True, check=False)
        if result.returncode != 1:
            raise AssertionError(f"truncated ELF exit {result.returncode}")
    print(f"PASS RP2040 tty roots: {len(roots)} callbacks and linked literal mutants")


def reject(data, ioconf, objects, diagnostic):
    try:
        checker.check_data(data, ioconf, objects)
    except ValueError as error:
        if diagnostic in str(error):
            return
        raise AssertionError(f"unexpected diagnostic: {error}") from error
    raise AssertionError(f"accepted mutant: {diagnostic}")


if __name__ == "__main__":
    main()
