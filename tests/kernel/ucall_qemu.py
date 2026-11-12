"""Execute both architecture ucall dispatch bodies as ARMv6-M code."""

import argparse
import subprocess
import tempfile
from pathlib import Path

import ucall_contract as contract

START = """\
.syntax unified
.thumb
.text
.globl _start
.thumb_func
_start:
    bl main
    movs r7, #1
    svc #0
"""
FLAGS = ("-std=gnu17", "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-builtin",
         "-fno-strict-aliasing", "-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", action="append", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--qemu", default="qemu-arm")
    args = parser.parse_args()
    compilers = [(gcc,) for gcc in args.gcc] + [(args.clang, "--target=arm-none-eabi")]
    with tempfile.TemporaryDirectory(prefix="discobsd-ucall-qemu-") as temporary:
        directory = Path(temporary)
        startup = directory / "start.S"
        startup.write_text(START, encoding="utf-8")
        start_obj = directory / "start.o"
        subprocess.run([args.gcc[0], "-mcpu=cortex-m0plus", "-mthumb", "-c", str(startup),
                        "-o", str(start_obj)], check=True)
        for relative in contract.SOURCES:
            source = (contract.ROOT / relative).read_text(encoding="utf-8")
            start = source.index("void\nucall(void)\n")
            end = source.index("\n/* The ABI reserves ufetch", start)
            body = source[start:end]
            mutations = (("none", None, None, 0),
                         ("authorization", "if (!suser())", "if (0 && !suser())", 1),
                         ("restore", "splx(s);", "splx(s ^ 1);", 2),
                         ("arguments", "uap->arg1, uap->arg2", "uap->arg2, uap->arg1", 2))
            for compiler in compilers:
                for level in ("O0", "O", "O1", "O2", "O3", "Og", "Os", "Oz"):
                    for name, old, new, expected in mutations:
                        if old and body.count(old) != 1:
                            raise SystemExit(f"FAIL ucall-qemu: {relative}: {name} boundary")
                        selected = body.replace(old, new, 1) if old else body
                        fixture = directory / "ucall.c"
                        fixture.write_text(contract.PREFIX + selected + contract.SUFFIX,
                                           encoding="utf-8")
                        obj = directory / "ucall.o"
                        binary = directory / "ucall"
                        subprocess.run([*compiler, *FLAGS, f"-{level}", "-c", str(fixture),
                                        "-o", str(obj)], check=True)
                        subprocess.run([args.gcc[0], "-mcpu=cortex-m0plus", "-mthumb",
                                        "-nostdlib", "-static", "-Wl,--build-id=none",
                                        "-Wl,-z,noexecstack", "-Wl,--fatal-warnings",
                                        "-Wl,-Ttext=0x20000", str(start_obj), str(obj),
                                        "-o", str(binary)], check=True)
                        result = subprocess.run([args.qemu, "-cpu", "cortex-m0", str(binary)],
                                                check=False, timeout=30)
                        if result.returncode != expected:
                            raise SystemExit(f"FAIL ucall-qemu: {relative} {compiler} -{level} "
                                             f"{name} exit={result.returncode} expected={expected}")
                print(f"PASS ucall-qemu: {relative} {compiler} controls and mutants")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
