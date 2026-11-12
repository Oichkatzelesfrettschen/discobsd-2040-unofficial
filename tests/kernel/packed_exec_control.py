"""Exercise the production packed-loader transition with injected failures."""

import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "sys/arch/rp2040/rp2040/exec_hsaout.c"
FIXTURE = ROOT / "tests/kernel/packed_exec_control.c"
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
    source = SOURCE.read_text(encoding="utf-8")
    begin = source.index("int\nexec_hsaout_check (")
    end = source.index("\n/*\n * The clean text for swapin", begin)
    body = source[begin:end]
    begin_check = body.index("    if (hsx_load_buf (bp, epp->ip, toff")
    end_check = body.index(") {", begin_check) + len(") {")
    preflight = body[begin_check:end_check]
    flag = "epp->flags |= EXEC_COMMITTED_FAILURE;"
    unhold = "exec_text_unhold (epp->ip);"
    if body.count(flag) != 1 or body.count(unhold) != 3:
        raise SystemExit("FAIL packed-exec-control: mutation boundary changed")
    mutations = (("none", body, 0),
                 ("skip-preflight", body.replace(preflight, "    if (0) {", 1), 10),
                 ("drop-committed-failure", body.replace(flag, "(void)0;", 1), 47),
                 ("drop-preflight-unhold", body.replace(unhold, "(void)0;", 1), 63))
    template = FIXTURE.read_text(encoding="utf-8")
    marker = "/* PACKED_LOADER_FUNCTION */"
    if template.count(marker) != 1:
        raise SystemExit("FAIL packed-exec-control: fixture marker changed")
    with tempfile.TemporaryDirectory(prefix="discobsd-packed-exec-control-") as temporary:
        directory = Path(temporary)
        startup = directory / "start.S"
        startup.write_text(START, encoding="utf-8")
        start_obj = directory / "start.o"
        subprocess.run([args.gcc[0], "-mcpu=cortex-m0plus", "-mthumb", "-c", str(startup),
                        "-o", str(start_obj)], check=True)
        compilers = [(gcc,) for gcc in args.gcc] + [(args.clang, "--target=arm-none-eabi")]
        for compiler in compilers:
            for level in ("O0", "O", "O1", "O2", "O3", "Og", "Os", "Oz"):
                for name, selected, expected in mutations:
                    fixture = directory / "fixture.c"
                    fixture.write_text(template.replace(marker, selected), encoding="utf-8")
                    obj, binary = directory / "fixture.o", directory / "fixture"
                    subprocess.run([*compiler, *FLAGS, f"-{level}", "-I", str(ROOT / "sys"),
                                    "-c", str(fixture), "-o", str(obj)], check=True)
                    subprocess.run([args.gcc[0], "-mcpu=cortex-m0plus", "-mthumb", "-nostdlib",
                                    "-static", "-Wl,--build-id=none", "-Wl,-z,noexecstack",
                                    "-Wl,--fatal-warnings", "-Wl,-Ttext=0x20000",
                                    str(start_obj), str(obj), "-o", str(binary)], check=True)
                    result = subprocess.run([args.qemu, "-cpu", "cortex-m0", str(binary)],
                                            check=False, timeout=30)
                    if result.returncode != expected:
                        raise SystemExit(f"FAIL packed-exec-control: {compiler} -{level} {name} "
                                         f"exit={result.returncode} expected={expected}")
            print(f"PASS packed-exec-control: {compiler} controls and mutations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
