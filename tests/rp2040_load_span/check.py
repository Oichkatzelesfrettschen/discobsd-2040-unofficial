"""Calibrate the linker assertion for the final RAM-function flash payload."""

import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ASSERTION = ('ASSERT(LOADADDR(.ramfunc) + SIZEOF(.ramfunc) <=\n'
             '\t    ORIGIN(FLASH) + LENGTH(FLASH),\n'
             '\t    "kernel flash load image exceeds FLASH")')
SCRIPT = '''\
MEMORY { FLASH (rx) : ORIGIN = 0x10000100, LENGTH = 256
         RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 4096 }
SECTIONS {
 .text : { *(.text) } > FLASH
 .data : { *(.data) } > RAM AT > FLASH
 .ramfunc : AT(LOADADDR(.data) + SIZEOF(.data)) { *(.ramfunc) } > RAM
 ASSERTION_SLOT
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool-prefix", default="arm-none-eabi")
    args = parser.parse_args()
    script = (ROOT / "sys/arch/rp2040/conf/kern.ldscript").read_text(encoding="utf-8")
    if ASSERTION not in script:
        raise SystemExit("FAIL load-span: production assertion absent")
    with tempfile.TemporaryDirectory(prefix="discobsd-load-span-") as temporary:
        directory = Path(temporary)
        for ramfunc_size in (16, 32, 33):
            source = directory / "fixture.s"
            source.write_text(f".text\n.space 208\n.data\n.space 16\n"
                              f".section .ramfunc,\"ax\"\n.space {ramfunc_size}\n")
            obj = directory / "fixture.o"
            subprocess.run([args.tool_prefix + "-as", "-mcpu=cortex-m0plus", "-mthumb",
                            "-o", str(obj), str(source)], check=True)
            for enforce in (False, True):
                linker_script = directory / "link.ld"
                linker_script.write_text(SCRIPT.replace("ASSERTION_SLOT",
                                         ASSERTION if enforce else ""))
                result = subprocess.run([args.tool_prefix + "-ld", "-T", str(linker_script),
                                         "-o", str(directory / "fixture.elf"), str(obj)],
                                        capture_output=True, text=True, check=False)
                should_fail = enforce and ramfunc_size > 32
                if (result.returncode != 0) != should_fail:
                    raise SystemExit(f"FAIL load-span: size={ramfunc_size}, enforce={enforce}: "
                                     f"{result.stderr.strip()}")
                if should_fail and "kernel flash load image exceeds FLASH" not in result.stderr:
                    raise SystemExit("FAIL load-span: expected assertion diagnostic absent")
    print("PASS load-span: fitting, exact-boundary, and ramfunc-only overflow controls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
