"""Execute callable byte I/O against the target FILE and byte backend."""

import argparse
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools/c17"))
from shipped_closure import source_snapshot  # noqa: E402

FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
         "-Wstrict-prototypes", "-Wmissing-prototypes", "-Wold-style-definition",
         "-fno-builtin", "-fvisibility=hidden", "-ffunction-sections", "-fdata-sections",
         "-D_TIME_BITS=32", "-nostdinc"]
ARM = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]
NAMES = ("fgetc", "fputc", "getchar", "putchar", "filbuf", "flsbuf")
RENAMES = ["-D" + name + "=character_" + name for name in
           ("read", "write", "fstat", "isatty", "malloc", "free", "close", "errno")]
SIGNATURES = {
    "fgetc": ("int", "FILE *"), "getc": ("int", "FILE *"),
    "getchar": ("int", "void"), "fputc": ("int", "int, FILE *"),
    "putc": ("int", "int, FILE *"), "putchar": ("int", "int"),
    "fflush": ("int", "FILE *"), "fclose": ("int", "FILE *"),
    "_filbuf": ("int", "FILE *"), "_flsbuf": ("int", "unsigned char, FILE *"),
    "_fwalk": ("int", "int (*)(FILE *)"),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compile_command(argv):
    result = subprocess.run(argv, capture_output=True, text=True, check=False, timeout=90,
                            env=dict(os.environ, LC_ALL="C"))
    require(result.returncode == 0 and not result.stdout and not result.stderr,
            "character I/O compilation fails: " + result.stdout + result.stderr)


def namespace_header(work):
    lines = ["#include <stdio.h>"]
    for name, (result, arguments) in SIGNATURES.items():
        lines.append(f'_Static_assert(_Generic(&({name}), {result} (*)({arguments}): '
                     f'1, default: 0), "public {name} signature");')
        lines.append(f'{result} ({name})({arguments}) __asm__("character_{name}");')
    lines.append('extern FILE _iob[] __asm__("character_iob");')
    path = work / "namespace.h"
    path.write_text("\n".join(lines) + "\n")
    return path


def build(root, work, compiler, target, mutant=None):
    flags = [*FLAGS, "-I" + str(root / "include"), *RENAMES,
             "-include", str(namespace_header(work)), *(ARM if target else [])]
    objects = []
    for name in NAMES:
        source = (mutant[1] if mutant and mutant[0] == name
                  else root / "lib/libc/stdio" / (name + ".c"))
        obj = work / (name + ".o")
        compile_command([*compiler, *flags, "-c", str(source), "-o", str(obj)])
        objects.append(str(obj))
    fixture = work / "fixture.o"
    compile_command([*compiler, *flags, "-c",
                     str(root / "tests/libc_contracts/character_io_test.c"), "-o", str(fixture)])
    objects.append(str(fixture))
    link = ["-Wl,--gc-sections"] if target or sys.platform != "darwin" else ["-Wl,-dead_strip"]
    if target:
        support = work / "support.o"
        compile_command([*compiler, *ARM, "-std=gnu17", "-Wall", "-Wextra", "-Werror",
                         "-fomit-frame-pointer", "-c", str(root / "tests/libbn/qemu_support.c"),
                         "-o", str(support)])
        objects.append(str(support))
        link.extend(["-nostartfiles", "-Wl,-Ttext=0x20000", "-lc", "-lgcc"])
    program = work / "character_io"
    compile_command([*compiler, *(ARM if target else []), *objects, *link, "-o", str(program)])
    return program


def execute(program, target):
    argv = [str(program)]
    if target:
        qemu = shutil.which("qemu-arm")
        require(qemu, "qemu-arm is required for ARM execution")
        argv = [qemu, "-cpu", "cortex-m0", *argv]
    return subprocess.run(argv, capture_output=True, text=True, check=False, timeout=30)


def mutations(root, work):
    specifications = (
        ("getc-byte", "fgetc", "return fgetc(fp);", "return fgetc(fp) + 1;", 11),
        ("getc-eof", "fgetc", "return fgetc(fp);", "return (unsigned char)fgetc(fp);", 12),
        ("putc-error", "fputc", "return fputc(c, fp);",
         "(void)fputc(c, fp); return (unsigned char)c;", 27),
        ("getchar-stream", "getchar", "return getc(stdin);", "return getc(stdout);", 11),
        ("putchar-stream", "putchar", "return putc(c, stdout);", "return putc(c, stdin);", 21),
        ("eof-counter", "filbuf",
         "\t\t/* getc() decrements before calling; keep repeated EOF bounded. */\n"
         "\t\tiop->_cnt = 0;",
         "\t\t/* getc() decrements before calling; keep repeated EOF bounded. */",
         13),
    )
    for label, name, before, after, status in specifications:
        source = (root / "lib/libc/stdio" / (name + ".c")).read_text()
        require(source.count(before) == 1, "mutation anchor differs: " + label)
        directory = work / label
        directory.mkdir()
        path = directory / (name + ".c")
        path.write_text(source.replace(before, after))
        yield label, name, path, status


def run(root, compiler, target, destination=None):
    require(os.environ.get("PYTHON"), "PYTHON must select the interpreter")
    before = source_snapshot(root)
    paths = [*(root / "lib/libc/stdio" / (name + ".c") for name in NAMES),
             root / "include/stdio.h", root / "tests/libc_contracts/character_io_test.c",
             Path(__file__)]
    identities = {str(path.relative_to(root)): digest(path) for path in paths}
    controls = []
    with tempfile.TemporaryDirectory(prefix="character-io-") as temporary:
        work = Path(temporary)
        good = work / "good"
        good.mkdir()
        program = build(root, good, compiler, target)
        observed = execute(program, target)
        require(observed.returncode == 0 and not observed.stdout and not observed.stderr,
                "character I/O good execution differs: " +
                str(observed.returncode) + observed.stderr)
        program_hash = digest(program)
        accepted = work / "accepted"
        shutil.copyfile(program, accepted)
        for label, name, source, status in mutations(root, work):
            program = build(root, source.parent, compiler, target, (name, source))
            observed = execute(program, target)
            require(observed.returncode == status and not observed.stdout and not observed.stderr,
                    label + " rejection differs: " + str(observed.returncode) + observed.stderr)
            controls.append({"case": label, "returncode": observed.returncode,
                             "source_sha256": digest(source)})
        if destination is not None:
            destination = destination.resolve()
            require(destination.parent == root / "tests/libc_contracts" and
                    destination.name in {"character_io_test", "character_io_test32",
                                         "character_io_test_san"},
                    "character I/O program destination is outside its declared products")
            shutil.copyfile(accepted, destination)
            destination.chmod(0o755)
    require(identities == {str(path.relative_to(root)): digest(path) for path in paths},
            "character I/O inputs change during execution")
    after = source_snapshot(root)
    require(all(before[field] == after[field]
                for field in ("commit", "tracked_files", "index_entries")),
            "tracked character I/O inputs change during execution")
    return {"schema_version": 1, "source_identities": identities,
            "source_commit": before["commit"], "program_sha256": program_hash,
            "execution": "Cortex-M0 QEMU" if target else "host",
            "character_calls": 3963, "controls": controls, "physical_hardware": "unrun"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--compiler", default=os.environ.get("HOST_CC", "cc"))
    parser.add_argument("--target", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--program", type=Path)
    args = parser.parse_args()
    try:
        result = run(args.root.resolve(), shlex.split(args.compiler), args.target, args.program)
        if args.output:
            args.output.write_text(json.dumps(result, indent=2) + "\n")
        print("PASS callable character I/O: 3963 operations and six rejected mutations")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("character-io: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
