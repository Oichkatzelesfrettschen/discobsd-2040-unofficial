"""Execute RP2040 bcopy and EABI entry points as Thumb-1 code under qemu-user."""

import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
START = """\
.syntax unified
.thumb
.text
.globl _start
.thumb_func
_start:
    bl test_copy
    movs r7, #1
    svc #0
"""
PREFIX = """\
#include <stddef.h>
typedef unsigned int u_int;
typedef unsigned char u_char;
"""
TEST = """\
int test_copy(void)
{
    _Alignas(4) unsigned char source[72], destination[72];
    for (unsigned src_offset = 0; src_offset < 4; src_offset++) {
        for (unsigned dst_offset = 0; dst_offset < 4; dst_offset++) {
            for (unsigned count = 0; count <= 64; count++) {
                for (unsigned index = 0; index < 72; index++) {
                    source[index] = (unsigned char)(index + 17);
                    destination[index] = 0xa5;
                }
                if (src_offset == 0 && dst_offset == 0)
                    __aeabi_memcpy4(destination, source, count);
                else
                    __aeabi_memcpy(destination + dst_offset, source + src_offset, count);
                for (unsigned index = 0; index < 72; index++) {
                    unsigned char expected = 0xa5;
                    if (index >= dst_offset && index - dst_offset < count)
                        expected = source[src_offset + index - dst_offset];
                    if (destination[index] != expected || source[index] != index + 17)
                        return 1;
                }
            }
        }
    }
    return 0;
}
"""
FLAGS = ("-std=gnu17", "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-builtin",
         "-fno-strict-aliasing", "-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", action="append", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--qemu", default="qemu-arm")
    args = parser.parse_args()
    source = (ROOT / "sys/arch/rp2040/rp2040/machdep.c").read_text(encoding="utf-8")
    start = source.index("#define UNALIGNED(")
    finish = source.index("\n}\n", source.index("void\n__aeabi_memcpy4(", start)) + 3
    body = source[start:finish]
    if body.count("bcopy(src, dst, nbytes);") != 3:
        raise SystemExit("FAIL aeabi-copy-qemu: copy entry boundary changed")
    with tempfile.TemporaryDirectory(prefix="discobsd-aeabi-qemu-") as temporary:
        directory = Path(temporary)
        startup = directory / "start.S"
        startup.write_text(START, encoding="utf-8")
        start_obj = directory / "start.o"
        subprocess.run([args.gcc[0], "-mcpu=cortex-m0plus", "-mthumb", "-c", str(startup),
                        "-o", str(start_obj)], check=True)
        compilers = [(gcc,) for gcc in args.gcc] + [(args.clang, "--target=arm-none-eabi")]
        for compiler in compilers:
            for level in ("O0", "O", "O1", "O2", "O3", "Og", "Os", "Oz"):
                for mutant in ("none", "generic", "aligned"):
                    selected = body
                    if mutant != "none":
                        marker = "void\n__aeabi_memcpy(" if mutant == "generic" else (
                            "void\n__aeabi_memcpy4("
                        )
                        offset = body.index(marker)
                        selected = body[:offset] + body[offset:].replace(
                            "bcopy(src, dst, nbytes);", "bcopy(dst, (void *)src, nbytes);", 1
                        )
                    fixture = directory / "copy.c"
                    fixture.write_text(PREFIX + selected + TEST, encoding="utf-8")
                    obj = directory / "copy.o"
                    binary = directory / "copy"
                    subprocess.run([*compiler, *FLAGS, f"-{level}", "-c", str(fixture),
                                    "-o", str(obj)], check=True)
                    subprocess.run([args.gcc[0], "-mcpu=cortex-m0plus", "-mthumb", "-nostdlib",
                                    "-static", "-Wl,--build-id=none", "-Wl,-z,noexecstack",
                                    "-Wl,--fatal-warnings", "-Wl,-Ttext=0x20000",
                                    str(start_obj), str(obj), "-o", str(binary)], check=True)
                    result = subprocess.run([args.qemu, "-cpu", "cortex-m0", str(binary)],
                                            check=False, timeout=30)
                    expected = 0 if mutant == "none" else 1
                    if result.returncode != expected:
                        raise SystemExit(f"FAIL aeabi-copy-qemu: {compiler} -{level} {mutant} "
                                         f"exit={result.returncode}")
            print(f"PASS aeabi-copy-qemu: {compiler} controls and mutants")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
