"""Link the shipped a.out stdio closure and reject a missing dependency."""

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path


def symbols(nm: Path, object_path: Path) -> str:
    """Return the tree nm output for an a.out object or archive."""
    return subprocess.run(
        [str(nm), "-p", str(object_path)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout

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
    label_fixture = work / "labels.s"
    labels_default = work / "labels-default.o"
    labels_stripped = work / "labels-stripped.o"
    label_fixture.write_text(
        ".text\n.global exported_label\nexported_label:\n.Llocal_label:\n"
        "\tbx lr\n",
        encoding="ascii",
    )
    subprocess.run(
        [str(tools / "as"), str(label_fixture), "-o", str(labels_default)],
        check=True,
    )
    subprocess.run(
        [str(tools / "as"), "-X", str(label_fixture), "-o", str(labels_stripped)],
        check=True,
    )
    default_symbols = symbols(tools / "nm", labels_default)
    stripped_symbols = symbols(tools / "nm", labels_stripped)
    if ".Llocal_label" not in default_symbols:
        raise SystemExit("board archive local-symbol control did not retain its label")
    if ".Llocal_label" in stripped_symbols or "exported_label" not in stripped_symbols:
        raise SystemExit("board archive -X control removed the wrong symbols")
    archive_symbols = symbols(tools / "nm", archive)
    if any(line.split()[-1].startswith(".L") for line in archive_symbols.splitlines()
           if line.split()):
        raise SystemExit("board archive retains compiler-local labels")
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
print("PASS stripped board archive output link and missing-vfprintf control")
