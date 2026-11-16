"""Exercise RP2040 ARM EABI copy entries and their source/destination order."""

import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PREFIX = """\
#include <stddef.h>
static void bcopy(const void *source, void *destination, size_t count)
{
    const unsigned char *input = source;
    unsigned char *output = destination;
    while (count--) *output++ = *input++;
}
"""
SUFFIX = """\
int main(void)
{
    _Alignas(4) unsigned char source[20], destination[20];
    for (unsigned count = 0; count <= 16; count++) {
        for (unsigned index = 0; index < 20; index++) {
            source[index] = (unsigned char)(index + 17);
            destination[index] = 0xa5;
        }
        __aeabi_memcpy4(destination, source, count);
        for (unsigned index = 0; index < 20; index++) {
            unsigned char expected = index < count ? source[index] : 0xa5;
            if (destination[index] != expected || source[index] != index + 17) return 1;
        }
        for (unsigned index = 0; index < 20; index++) destination[index] = 0xa5;
        __aeabi_memcpy(destination + 1, source + 1, count);
        for (unsigned index = 0; index < 20; index++) {
            unsigned char expected = index > 0 && index <= count ? source[index] : 0xa5;
            if (destination[index] != expected || source[index] != index + 17) return 2;
        }
    }
    return 0;
}
"""


def main():
    source = (ROOT / "sys/arch/rp2040/rp2040/machdep.c").read_text(encoding="utf-8")
    start = source.index("void\n__aeabi_memcpy(")
    end = source.index("\n}\n", source.index("void\n__aeabi_memcpy4(", start)) + 3
    body = source[start:end]
    if body.count("bcopy(src, dst, nbytes);") != 2:
        raise SystemExit("FAIL aeabi-memcpy4: copy dispatch changed")
    with tempfile.TemporaryDirectory(prefix="discobsd-aeabi-copy-") as temporary:
        directory = Path(temporary)
        for compiler in ("cc", "clang"):
            for mutant in ("none", "generic", "aligned"):
                selected = body
                if mutant != "none":
                    offset = 0 if mutant == "generic" else body.index("void\n__aeabi_memcpy4(")
                    before, after = body[:offset], body[offset:]
                    selected = before + after.replace("bcopy(src, dst, nbytes);",
                                                      "bcopy(dst, (void *)src, nbytes);", 1)
                fixture = directory / "copy.c"
                fixture.write_text(PREFIX + selected + SUFFIX, encoding="utf-8")
                binary = directory / "copy"
                subprocess.run([compiler, "-std=c17", "-Wall", "-Wextra", "-Werror",
                                "-ffreestanding", "-fno-builtin", str(fixture), "-o", str(binary)],
                               check=True)
                status = subprocess.run([str(binary)], check=False).returncode
                if (status == 0) != (mutant == "none"):
                    raise SystemExit(f"FAIL aeabi-memcpy4: {compiler} "
                                     f"mutant={mutant} exit={status}")
    print("PASS aeabi-memcpy4: copy lengths and reversed-argument mutant")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
