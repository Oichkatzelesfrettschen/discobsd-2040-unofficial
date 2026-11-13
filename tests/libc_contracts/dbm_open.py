"""Exercise database-open admission with injected allocation and I/O failures."""

import argparse
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
         "-D_DEFAULT_SOURCE", "-O1", "-ffunction-sections", "-fdata-sections"]
ARM = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]
MUTATIONS = [
    ("basename-overrun", 'length > sizeof(db->dbm_pagbuf) - sizeof(".pag")',
     'length > sizeof(db->dbm_pagbuf) - sizeof(".pag") + 1', "long-name"),
    ("unchecked-stat", "fstat(db->dbm_dirf, &statb) < 0",
     "fstat(db->dbm_dirf, &statb) < -1", "directory-stat"),
    ("negative-extent", "statb.st_size < 0", "statb.st_size < -1", "directory-extent"),
    ("cleanup-errno", "errno = saved_errno;",
     "errno = saved_errno == 0 ? 0 : EINVAL;", "directory-extent"),
    ("descriptor-zero-leak", "if (db->dbm_pagf >= 0)",
     "if (db->dbm_pagf > 0)", "directory-extent"),
    ("last-bit-omission", "* BYTESIZ + (BYTESIZ - 1)",
     "* BYTESIZ + (BYTESIZ - 2)", "success"),
    ("directory-suffix", 'memcpy(db->dbm_pagbuf + length, ".dir", sizeof(".dir"));',
     'memcpy(db->dbm_pagbuf + length, ".pag", sizeof(".pag"));', "success"),
]


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def compile_command(arguments):
    result = subprocess.run(arguments, capture_output=True, text=True,
                            timeout=90, check=False)
    require(result.returncode == 0 and not result.stderr,
            "dbm-open compilation fails: " + repr(arguments) +
            "\nexit=" + str(result.returncode) + "\n" +
            result.stdout + result.stderr)


def header_contract(compiler, work):
    header = work / "ndbm.h"
    valid = work / "header-valid.c"
    valid.write_text(
        "#include <ndbm.h>\n"
        "#include <ndbm.h>\n"
        "static DBM *(*open_database)(const char *, int, mode_t) = dbm_open;\n"
        "int main(void) { return open_database(\"fixture\", 0, (mode_t)0) != 0; }\n"
    )
    strict = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
              "-Wstrict-prototypes", "-I" + str(work)]
    compile_command(compiler + strict + ["-c", str(valid), "-o", str(work / "header-valid.o")])

    original = header.read_text()
    guard = "#define _NDBM_H_"
    require(original.count(guard) == 1, "ndbm header guard mutation anchor differs")
    header.write_text(original.replace(guard, "#define _NDBM_H_BROKEN_"))
    result = subprocess.run(
        compiler + strict + ["-c", str(valid), "-o", str(work / "header-guard-mutant.o")],
        capture_output=True, text=True, timeout=90, check=False,
    )
    require(result.returncode != 0,
            "ndbm repeated inclusion accepts a removed header guard: " +
            result.stdout + result.stderr)
    print("dbm-open mutant rejected: header-guard")
    header.write_text(original)

    invalid = work / "header-two-arguments.c"
    invalid.write_text(
        "#include <ndbm.h>\n"
        "DBM *open_database(const char *path) { return dbm_open(path, 0); }\n"
    )
    result = subprocess.run(
        compiler + strict + ["-c", str(invalid), "-o", str(work / "header-invalid.o")],
        capture_output=True, text=True, timeout=90, check=False,
    )
    require(result.returncode != 0 and "too few arguments" in result.stderr,
            "dbm-open public header accepts the historical two-argument call: " +
            result.stdout + result.stderr)


def build(root, compiler, work, arm, source):
    # Unselected legacy record functions use the BSD spelling for SEEK_SET.
    flags = FLAGS + (ARM + ["-DL_SET=SEEK_SET"] if arm else []) + ["-I" + str(work)]
    renames = ["-D" + name + "=dbm_test_" + name
               for name in ("malloc", "free", "open", "close", "fstat")]
    obj = work / "ndbm.o"
    compile_command(compiler + flags + renames + ["-c", str(source), "-o", str(obj)])
    fixture = work / "fixture.o"
    compile_command(compiler + flags + ["-c", str(root / "tests/libc_contracts/dbm_open_test.c"),
                                       "-o", str(fixture)])
    objects = [str(obj), str(fixture)]
    link = []
    if arm:
        support = work / "support.o"
        compile_command(compiler + ARM + ["-std=gnu17", "-Wall", "-Wextra", "-Werror",
                        "-fomit-frame-pointer", "-c", str(root / "tests/libbn/qemu_support.c"),
                        "-o", str(support)])
        objects.append(str(support))
        link = ["-nostartfiles", "-Wl,-Ttext=0x20000", "-lc", "-lgcc"]
    program = work / "dbm_open"
    compile_command(compiler + (ARM if arm else []) + ["-Wl,--gc-sections"] + objects +
                    link + ["-o", str(program)])
    return program


def execute(program, arm):
    prefix = ["qemu-arm", "-cpu", "cortex-m0"] if arm else []
    return subprocess.run(prefix + [str(program)], capture_output=True, text=True,
                          timeout=90, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("compiler")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--arm", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    compiler = shlex.split(args.compiler)
    require(compiler, "compiler command is empty")
    source = root / "lib/libc/gen/ndbm.c"
    with tempfile.TemporaryDirectory(prefix="dbm-open-") as temporary:
        work = Path(temporary)
        shutil.copy2(root / "include/ndbm.h", work / "ndbm.h")
        header_contract(compiler, work)
        program = build(root, compiler, work, args.arm, source)
        result = execute(program, args.arm)
        require(result.returncode == 0 and not result.stderr and
                result.stdout == "dbm-open: 3180 cases pass\n",
                "dbm-open contract fails: exit=" + str(result.returncode) +
                "\n" + result.stdout + result.stderr)
        print(result.stdout, end="")
        accepted = work / "accepted"
        shutil.copy2(program, accepted)
        original = source.read_text()
        for name, anchor, replacement, rejection in MUTATIONS:
            require(original.count(anchor) == 1, name + " mutation anchor differs")
            mutant = work / "mutant.c"
            mutant.write_text(original.replace(anchor, replacement))
            program = build(root, compiler, work, args.arm, mutant)
            result = execute(program, args.arm)
            require(result.returncode == 1 and
                    result.stdout.startswith("FAIL dbm-open " + rejection),
                    name + " lacks its named rejection: " + result.stdout + result.stderr)
            print("dbm-open mutant rejected: " + name)
        if args.output:
            shutil.copy2(accepted, args.output)


if __name__ == "__main__":
    main()
