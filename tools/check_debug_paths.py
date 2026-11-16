"""Check that RP2040 compiler routes keep checkout paths out of their objects.

The C routes (two userland programs and the PICO kernel) compile a probe
with -g twice, with the route's DEBUG_PATH_MAP and with it emptied: the
mapped object must carry DWARF without the checkout path, the unmapped one
must carry the path (the control that shows the probe can see it), and the
two must match once their debug sections are stripped, so the mapping
changes no instruction.

The assembler route is lib/libc_aout/libc's ${CC} ${ASFLAGS}, which puts
the tree's Thumb assembler ahead of the cross toolchain's with -B. GCC
forwards --debug-prefix-map to the assembler for every .S file, so the
route must assemble a .S probe with the mapping, with it emptied and with
-g, giving three byte-identical a.out objects without the checkout path. A
stand-in assembler that opens the forwarded operand as a second source
file, as the assembler did before it consumed the operand, must make the
mapped route fail; that calibrates the route check.
"""

import os
import shlex
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
C_ROUTES = ("bin/echo", "usr.bin/smlrc", "sys/arch/rp2040/compile/PICO")
ASM_ROUTE = "lib/libc_aout/libc"
ENVIRONMENT = {
    key: value
    for key, value in os.environ.items()
    if key not in ("MAKEFLAGS", "MFLAGS", "C17_CAPTURE_DIR", "DEBUG_PATH_MAP")
}
# Two source operands make this assembler fail the way the tree's did
# when it read the forwarded mapping as an input file.
STAND_IN = """#!/bin/sh
sources=0
while [ $# -gt 0 ]; do
	case $1 in
	-o|-I) shift ;;
	-*) ;;
	*) sources=$((sources + 1)) ;;
	esac
	shift
done
[ "$sources" -le 1 ] || { echo "as: too many input files" >&2; exit 1; }
exit 0
"""


class RouteError(Exception):
    """A route failed its control or its claim."""


def invoke(arguments, directory):
    result = subprocess.run(
        arguments,
        cwd=directory,
        env=ENVIRONMENT,
        capture_output=True,
        text=True,
        check=False,
        timeout=120,
    )
    if result.returncode:
        raise RouteError(
            result.stdout.replace(str(ROOT), ".") + result.stderr.replace(str(ROOT), ".")
        )
    return result.stdout


def command(make, directory, variables, overrides):
    text = invoke([*make, "-k", "MACHINE=rp2040", *overrides, "-V", variables], ROOT / directory)
    words = shlex.split(text.strip())
    if not words:
        raise RouteError(f"{directory}: empty compiler command")
    return words


def check_c_route(make, directory, temporary):
    cwd = ROOT / directory
    source = temporary / "debug_path_probe.c"
    source.write_text("int debug_path_probe(int value) { return value + 1; }\n")
    stripped = []
    for mapped in (True, False):
        overrides = [] if mapped else ["DEBUG_PATH_MAP="]
        compiler = command(make, directory, "${CC} ${CFLAGS}", overrides)
        output = temporary / ("mapped.o" if mapped else "unmapped.o")
        invoke([*compiler, "-g", "-c", str(source), "-o", str(output)], cwd)
        contents = output.read_bytes()
        if b".debug_info" not in contents:
            raise RouteError(f"{directory}: object has no debug information")
        if (str(ROOT).encode() in contents) == mapped:
            raise RouteError(f"{directory}: debug-prefix mapping control failed")
        bare = temporary / ("mapped-stripped.o" if mapped else "unmapped-stripped.o")
        invoke(["arm-none-eabi-objcopy", "--strip-debug", str(output), str(bare)], cwd)
        stripped.append(bare.read_bytes())
    if stripped[0] != stripped[1]:
        raise RouteError(f"{directory}: prefix mapping changed the stripped object")
    print(f"PASS {directory}: mapped and unmapped debug controls; stripped objects match")


def assemble(make, temporary, name, overrides, extra, assembler_dir=None):
    """Assemble the .S probe through the route; ASSEMBLER_DIR replaces -B."""
    cwd = ROOT / ASM_ROUTE
    source = temporary / "debug_path_probe.S"
    source.write_text("\t.text\n\t.globl\tprobe\nprobe:\n\tbx\tlr\n")
    words = command(make, ASM_ROUTE, "${CC} ${ASFLAGS}", overrides)
    mapped = any(word.startswith("-fdebug-prefix-map=") for word in words)
    if mapped == ("DEBUG_PATH_MAP=" in overrides):
        raise RouteError(f"{ASM_ROUTE}: DEBUG_PATH_MAP did not control the route")
    selectors = [i for i, word in enumerate(words) if word.startswith("-B")]
    if len(selectors) != 1:
        raise RouteError(f"{ASM_ROUTE}: expected one -B assembler selector, found {len(selectors)}")
    if assembler_dir is not None:
        words[selectors[0]] = f"-B{assembler_dir}/"
    output = temporary / f"{name}.o"
    invoke([*words, *extra, "-c", str(source), "-o", str(output)], cwd)
    return output.read_bytes() if output.exists() else b""


def check_asm_route(make, temporary):
    stand_in = temporary / "stand-in"
    stand_in.mkdir()
    script = stand_in / "as"
    script.write_text(STAND_IN)
    script.chmod(script.stat().st_mode | stat.S_IXUSR)
    # The stand-in runs, and fails only once the mapping operand reaches it.
    assemble(make, temporary, "stand-in-unmapped", ["DEBUG_PATH_MAP="], [], stand_in)
    try:
        assemble(make, temporary, "stand-in", [], [], stand_in)
    except RouteError as error:
        if "too many input files" not in str(error):
            raise
    else:
        raise RouteError(f"{ASM_ROUTE}: stand-in assembler accepted the mapping operand")
    objects = [
        assemble(make, temporary, "mapped", [], []),
        assemble(make, temporary, "unmapped", ["DEBUG_PATH_MAP="], []),
        assemble(make, temporary, "debug", [], ["-g"]),
    ]
    if not objects[0] or any(o != objects[0] for o in objects):
        raise RouteError(f"{ASM_ROUTE}: mapping or -g changed the assembled object")
    if str(ROOT).encode() in objects[0]:
        raise RouteError(f"{ASM_ROUTE}: assembled object names the checkout")
    print(
        f"PASS {ASM_ROUTE}: .S probe assembles mapped, unmapped and with -g into one "
        "object; stand-in assembler rejected"
    )


def main():
    make = shlex.split(os.environ.get("MAKE", "bmake"))
    evidence = ROOT / ".build-logs"
    evidence.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="debug-path-check-", dir=evidence) as name:
        for directory in C_ROUTES:
            route_dir = Path(name) / directory.replace("/", "_")
            route_dir.mkdir()
            check_c_route(make, directory, route_dir)
        asm_dir = Path(name) / "asm"
        asm_dir.mkdir()
        check_asm_route(make, asm_dir)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RouteError, subprocess.TimeoutExpired) as error:
        print(f"debug paths: {error}", file=sys.stderr)
        sys.exit(1)
