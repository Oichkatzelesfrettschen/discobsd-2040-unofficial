"""Calibrate linked callout callback literal checks."""

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
import check_rp2040_callout_roots as checker  # noqa: E402

SOURCE_COUNTS = {"schedcpu": 1, "endtsleep": 1, "wakeup": 1, "kbd_poll": 3,
                 "kbdreq_timeout": 1, "render_resume": 1, "bell_stop": 1}


def check_source(source, callback):
    pattern = rf"\btimeout\s*\(\s*{callback}\s*,"
    if len(re.findall(pattern, source)) != SOURCE_COUNTS[callback]:
        raise ValueError(f"timeout source count differs for {callback}")
    return pattern


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    build = args.build_dir
    data = (build / "unix.elf").read_bytes()
    ioconf = (build / "ioconf.o").read_bytes()
    objects = {name: (build / f"{name}.o").read_bytes()
               for name in ("kern_synch", "tty", "picokbd", "lcdcons", "picoaudio")}
    roots = checker.check_data(data, ioconf, objects)
    elf = ELFFile(io.BytesIO(data))
    symbols = elf.get_section_by_name(".symtab")
    text_section = elf.get_section_by_name(".text")
    for component, owner, callback, literal in roots:
        source_dir = "sys/kern" if component in ("kern_synch", "tty") else "sys/arch/rp2040/dev"
        source = (ROOT / source_dir / f"{component}.c").read_text(encoding="utf-8")
        pattern = check_source(source, callback)
        changed = re.sub(pattern, "timeout(wrong_callback,", source, count=1)
        try:
            check_source(changed, callback)
        except ValueError:
            pass
        else:
            raise AssertionError(f"{callback} source mutant survived")
        position = text_section.header.sh_offset + literal - text_section.header.sh_addr
        current = struct.unpack_from("<I", data, position)[0]
        alternatives = ("wakeup", "schedcpu")
        other = next(name for name in alternatives if name != callback)
        for value in (current ^ 4, current & ~1, checker.symbol(symbols, other, "STT_FUNC")):
            mutant = bytearray(data)
            struct.pack_into("<I", mutant, position, value)
            reject(mutant, ioconf, objects, f"{owner} callout literal differs")
    with tempfile.TemporaryDirectory(prefix="rp2040-callout-roots-") as directory:
        mutant = Path(directory) / "truncated.elf"
        mutant.write_bytes(data[:80])
        result = subprocess.run([sys.executable, str(ROOT / "tools/check_rp2040_callout_roots.py"),
                                 "--build-dir", str(build), "--kernel", str(mutant)],
                                capture_output=True, text=True, check=False)
        if result.returncode != 1:
            raise AssertionError(f"truncated ELF exit {result.returncode}")
    print(f"PASS RP2040 callout roots: {len(roots)} references and literal mutants")


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
