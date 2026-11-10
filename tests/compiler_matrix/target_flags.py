"""Check ARMv6-M target spellings and reject wrong architecture or enum ABI."""

import argparse
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/compiler_matrix/target_flags.c"
FLAGS = ("-std=c17", "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-builtin",
         "-mthumb", "-mfloat-abi=soft", "-mabi=aapcs", "-fshort-enums", "-Os")
VARIANTS = (("cpu", ("-mcpu=cortex-m0plus",)),
            ("architecture-tune", ("-march=armv6-m", "-mtune=cortex-m0plus")))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gcc", action="append", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--readelf", default="arm-none-eabi-readelf")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="discobsd-target-flags-") as temporary:
        output = Path(temporary) / "probe.o"
        compilers = [(compiler,) for compiler in args.gcc]
        compilers.append((args.clang, "--target=arm-none-eabi"))
        for compiler in compilers:
            for label, target in VARIANTS:
                command = [*compiler, *FLAGS, *target, "-c", str(SOURCE),
                           "-o", str(output)]
                subprocess.run(command, check=True)
                attrs = subprocess.run([args.readelf, "-A", str(output)], check=True,
                                       capture_output=True, text=True).stdout
                for expected in ("Tag_THUMB_ISA_use: Thumb-1", "Tag_ABI_enum_size: small",
                                 "Tag_CPU_arch_profile: Microcontroller"):
                    if expected not in attrs:
                        raise SystemExit(f"FAIL target-flags: {compiler} {label}: {expected}")
                if "Tag_ABI_VFP_args: VFP registers" in attrs:
                    raise SystemExit(f"FAIL target-flags: {compiler} {label}: hard-float PCS")
                if not re.search(r"Tag_CPU_arch: v6S?-M\b", attrs):
                    raise SystemExit(f"FAIL target-flags: {compiler} {label}: CPU architecture")
            for extra in (("-mcpu=cortex-m3",), ("-mcpu=cortex-m0plus", "-fno-short-enums")):
                result = subprocess.run([*compiler, *FLAGS, *extra, "-c",
                                         str(SOURCE), "-o", str(output)],
                                        capture_output=True, text=True, check=False)
                if result.returncode == 0:
                    raise SystemExit(f"FAIL target-flags: {compiler} accepted {extra}")
            print(f"PASS target-flags: {compiler}: target variants and negative controls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
