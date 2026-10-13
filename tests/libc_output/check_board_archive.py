"""Link the shipped a.out stdio closure and reject a missing dependency."""

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path)
parser.add_argument("compiler")
args = parser.parse_args()
root = args.root.resolve()
tools = root / "tools/bin/rp2040"
archive = root / "distrib/obj/boardlibc.rp2040/libc.a"
startup = root / "lib/libc_aout/crt0.o"
with tempfile.TemporaryDirectory(prefix="board-output-") as directory:
    work = Path(directory)
    assembly = work / "consumer.s"
    object_path = work / "consumer.o"
    subprocess.run([args.compiler, "-std=c17", "-Wall", "-Wextra", "-Werror",
                    "-fno-builtin", "-Os", "-ffreestanding", "-mcpu=cortex-m0plus",
                    "-mthumb", "-mfloat-abi=soft", "-nostdinc", "-I" + str(root / "include"),
                    "-S", str(root / "tests/libc_output/board_consumer.c"),
                    "-o", str(assembly)], check=True)
    subprocess.run([str(tools / "as"), str(assembly), "-o", str(object_path)], check=True)
    command = [str(tools / "ld"), "-o", str(work / "consumer"), str(startup), str(object_path)]
    subprocess.run([*command, str(archive)], check=True)
    damaged = work / "libc.a"
    shutil.copyfile(archive, damaged)
    subprocess.run([str(tools / "ar"), "d", str(damaged), "vfprintf.o"], check=True)
    subprocess.run([str(tools / "ranlib"), str(damaged)], check=True)
    result = subprocess.run([*command, str(damaged)], capture_output=True, text=True)
    if result.returncode != 1 or "vfprintf" not in result.stdout + result.stderr:
        raise SystemExit("board output closure accepts missing vfprintf or fails unexpectedly")
print("PASS selected board archive output link and missing-vfprintf control")
