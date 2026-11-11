"""Prove the getgrent contract and bss bound reject static-record regressions.

usage: getgrent_contract_mutate.py ROOT HOSTCC ARMCC TEST

The tree's getgrent.c must pass both oracles: the host contract test, and a
Cortex-M0+ object whose .bss stays under BSS_LIMIT bytes, which holds the
file pointer, the struct group and the record pointer but not the record.
Each mutant must fail at least one oracle.
"""

import pathlib
import shlex
import subprocess
import sys
import tempfile

BSS_LIMIT = 64
RENAMES = [
    "-Dgetgrent=db_getgrent",
    "-Dsetgrent=db_setgrent",
    "-Dendgrent=db_endgrent",
    "-Dmalloc=grent_test_malloc",
    "-Dfree=grent_test_free",
    "-Dfopen=grent_test_fopen",
]
HOST_FLAGS = ["-std=gnu17", "-Wall", "-Wextra", "-Werror"]
TARGET_FLAGS = [
    "-std=gnu17",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-fno-common",
    "-mcpu=cortex-m0plus",
    "-mabi=aapcs",
    "-mlittle-endian",
    "-mthumb",
    "-mfloat-abi=soft",
    "-Os",
    "-nostdinc",
]

ALLOCATE = "\tif (!grbuf && !(grbuf = malloc(sizeof(*grbuf))))\n\t\treturn(NULL);\n"
RECORD = "} *grbuf;\n"


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def host_contract(compiler, source, test, directory):
    """Return True when the host contract test passes against source."""
    unit = directory / "getgrent_unit.o"
    program = directory / "getgrent_contract"
    result = subprocess.run(
        compiler + HOST_FLAGS + RENAMES + ["-c", str(source), "-o", str(unit)],
        capture_output=True,
        text=True,
    )
    require(result.returncode == 0, result.stdout + result.stderr)
    result = subprocess.run(
        compiler + HOST_FLAGS + [str(test), str(unit), "-o", str(program)],
        capture_output=True,
        text=True,
    )
    require(result.returncode == 0, result.stdout + result.stderr)
    return subprocess.run([str(program)], capture_output=True).returncode == 0


def target_bss(root, compiler, source, directory):
    """Return the .bss size of source compiled for Cortex-M0+."""
    unit = directory / "getgrent_target.o"
    result = subprocess.run(
        compiler
        + TARGET_FLAGS
        + ["-isystem", str(root / "include"), "-c", str(source), "-o", str(unit)],
        capture_output=True,
        text=True,
    )
    require(result.returncode == 0, result.stdout + result.stderr)
    size = compiler[0].removesuffix("gcc") + "size"
    result = subprocess.run(
        [size, "-A", str(unit)],
        capture_output=True,
        text=True,
        check=True,
    )
    for line in result.stdout.splitlines():
        fields = line.split()
        if fields[:1] == [".bss"]:
            return int(fields[1])
    return 0


def main():
    require(
        len(sys.argv) == 5,
        "usage: getgrent_contract_mutate.py ROOT HOSTCC ARMCC TEST",
    )
    root = pathlib.Path(sys.argv[1]).resolve()
    host = shlex.split(sys.argv[2])
    target = shlex.split(sys.argv[3])
    test = pathlib.Path(sys.argv[4]).resolve()
    tree = root / "lib/libc/gen/getgrent.c"
    source = tree.read_text(encoding="utf-8")
    require(source.count(ALLOCATE) == 1, "getgrent allocation anchor differs")
    require(source.count(RECORD) == 1, "getgrent record anchor differs")

    mutants = {
        # The record as a static object: correct entries, 1,057 bytes of bss.
        "static record": source.replace(RECORD, "} grbuf_store, *grbuf = &grbuf_store;\n"),
        # A fresh record on every call: entries move and memory leaks.
        "allocation per call": source.replace(
            ALLOCATE,
            "\tif (!(grbuf = malloc(sizeof(*grbuf))))\n\t\treturn(NULL);\n",
        ),
        # The allocation failure ignored: the next store goes through NULL.
        "unchecked allocation": source.replace(
            ALLOCATE, "\tif (!grbuf)\n\t\tgrbuf = malloc(sizeof(*grbuf));\n"
        ),
        # endgrent() releases the record that getgrgid() still returns.
        "release at endgrent": source.replace(
            "\t\tfclose(grf);\n",
            "\t\tfclose(grf);\n\t\tfree(grbuf);\n\t\tgrbuf = NULL;\n",
        ),
    }

    with tempfile.TemporaryDirectory(prefix="getgrent-contract-") as name:
        directory = pathlib.Path(name)
        require(
            host_contract(host, tree, test, directory), "tree getgrent.c fails the host contract"
        )
        bss = target_bss(root, target, tree, directory)
        require(bss < BSS_LIMIT, f"tree getgrent.o carries {bss} bytes of bss")
        for label, text in mutants.items():
            require(text != source, f"{label} mutant did not apply")
            mutant = directory / "getgrent_mutant.c"
            mutant.write_text(text, encoding="utf-8")
            passes = host_contract(host, mutant, test, directory)
            small = target_bss(root, target, mutant, directory) < BSS_LIMIT
            require(not (passes and small), f"{label} mutant was accepted")
            print(f"getgrent mutant rejected: {label}")
    print(f"getgrent tree object: {bss} bytes of bss, limit {BSS_LIMIT}")


if __name__ == "__main__":
    main()
