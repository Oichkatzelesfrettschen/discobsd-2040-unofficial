"""Calibrate callable stdio names independently of their function-like macros."""

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from network_header_contracts import diagnostic_controls
from passwd_header_contracts import compile_case, file_digest, require, tool_identity
from record_header_contracts import MODES, syntax_controls, syntax_rejection
from shipped_closure import source_snapshot
from string_header_contracts import command_case

SIGNATURES = {"fgetc": "int (*)(FILE *)", "getc": "int (*)(FILE *)",
              "getchar": "int (*)(void)", "fputc": "int (*)(int, FILE *)",
              "putc": "int (*)(int, FILE *)", "putchar": "int (*)(int)"}
FORMS = ("standalone", "repeated", "peers-before", "peers-after", "macros-undefined")


def fixture(form, smaller=False):
    peers = ("sys/types.h", "stdlib.h", "stdarg.h", "unistd.h")
    lines = [f"#include <{name}>" for name in peers] if form == "peers-before" else []
    lines.append("#include <stdio.h>")
    if form == "repeated":
        lines.append("#include <stdio.h>")
    if form == "peers-after":
        lines.extend(f"#include <{name}>" for name in reversed(peers))
    if form == "macros-undefined":
        lines.extend("#undef " + name for name in ("getc", "putc", "getchar", "putchar"))
    for name, signature in SIGNATURES.items():
        if not smaller:
            lines.append(f'_Static_assert(_Generic(&({name}), {signature}: 1, '
                         f'default: 0), "{name} signature");')
        lines.append(signature.replace("(*)", "(*import_" + name + ")") + f" = ({name});")
    if smaller:
        lines.append("typedef char file_size[sizeof(FILE) == 24 ? 1 : -1];")
    else:
        lines.append('_Static_assert(sizeof(FILE) == 24 && _Alignof(FILE) == 4, "FILE ABI");')
    lines.append("int main(void) { return 0; }")
    return "\n".join(lines) + "\n"


def run(root, host_cc):
    require(os.environ.get("PYTHON"), "PYTHON must select the interpreter")
    cross, clang, nm, assembler = [shutil.which(name) for name in
                                  ("arm-none-eabi-gcc", "clang", "arm-none-eabi-nm",
                                   "arm-none-eabi-as")]
    require(cross and clang and nm and assembler, "ARM GCC/binutils and Clang are required")
    before = source_snapshot(root)
    verifier = file_digest(Path(__file__))
    flags = ["-Wall", "-Wextra", "-Werror", "-fno-builtin", "-nostdinc",
             "-I" + str(root / "include"), "-mcpu=cortex-m0plus", "-mthumb",
             "-mfloat-abi=soft"]
    profiles = {"gcc.gnu17": [cross, "-std=gnu17", *flags],
                "gcc.c17": [cross, "-std=c17", "-pedantic-errors", *flags],
                "clang.c17": [clang, "--target=arm-none-eabi", "-std=c17",
                               "-pedantic-errors", *flags]}
    cases, smaller = [], []
    with tempfile.TemporaryDirectory(prefix="character-io-headers-") as temporary:
        work = Path(temporary)
        source = work / "consumer.c"
        for profile, command in profiles.items():
            for mode, selectors in MODES.items():
                for width in (32, 64):
                    arguments = [*command, *selectors, f"-D_TIME_BITS={width}"]
                    for form in FORMS:
                        source.write_text(fixture(form))
                        row = compile_case(arguments, source, f"{profile}.{mode}.{width}.{form}")
                        require(not row["diagnostics"], "valid stdio consumer emits diagnostics")
                        cases.append(row)
                    source.write_text(fixture("standalone"))
                    obj = work / "aliases.o"
                    row = command_case([*arguments, "-c", str(source), "-o", str(obj)],
                                       f"{profile}.{mode}.{width}.imports")
                    names = sorted(line.split()[-1] for line in subprocess.check_output(
                        [nm, "-u", str(obj)], text=True).splitlines() if line.split())
                    require(names == sorted(SIGNATURES), "character I/O import identity differs")
                    row.update(undefined_symbols=names, object_sha256=file_digest(obj))
                    cases.append(row)
            original = (root / "include/stdio.h").read_text()
            for name in ("getc", "putc", "getchar", "putchar"):
                declaration = {"getc": "int     getc (FILE *);",
                               "putc": "int     putc (int, FILE *);",
                               "getchar": "int     getchar (void);",
                               "putchar": "int     putchar (int);"}[name]
                require(original.count(declaration) == 1, "stdio declaration anchor differs")
                wrong_type = declaration.replace("int     ", "long    ", 1)
                for kind, replacement in (("missing", ""), ("type", wrong_type)):
                    overlay = work / (profile + name + kind)
                    overlay.mkdir()
                    (overlay / "stdio.h").write_text(original.replace(declaration, replacement))
                    source.write_text(fixture("standalone"))
                    symbol = r"\b" + name + r"\b"
                    diagnostic = (r"error:[^\n]*(?:undeclared[^\n]*" + symbol + "|" +
                                  symbol + r"[^\n]*undeclared)" if kind == "missing" else
                                  r"error:[^\n]*static assertion failed[^\n]*" +
                                  symbol + " signature")
                    cases.append(compile_case([command[0], "-I" + str(overlay), *command[1:]],
                                              source, profile + "." + name + "." + kind,
                                              diagnostic))
        host = shlex.split(host_cc)
        require(host and shutil.which(host[0]), "a host compiler is required")
        compiler = work / "smlrc"
        smaller.append(command_case([*host, "-O1", "-Wall", "-Wextra", "-Werror", "-DTHUMB",
            "-DNO_ANNOTATIONS", "-DNO_PPACK", "-DSTATIC", "-DNO_EXTRA_WARNS",
            "-DSYNTAX_STACK_MAX=3200", "-o", str(compiler),
            str(root / "usr.bin/smlrc/smlrc.c")], "smaller.build"))
        for mode, selectors in MODES.items():
            for width in (32, 64):
                for form in FORMS:
                    label = f"smaller.{mode}.{width}.{form}"
                    directory = work / label
                    directory.mkdir()
                    (directory / "input.c").write_text(fixture(form, True))
                    smaller.append(command_case([cross, "-std=c90", "-E", "-P", "-nostdinc",
                        "-I" + str(root / "include"), "-U__GNUC__", "-U__PCC__",
                        "-D__SMALLER_C__=0x0100", "-D__SMALLER_C_32__", "-D__SMALLER_C_UCHAR__",
                        "-D__arm__", "-D__unix__", "-D__BSD__", "-D__DISCOBSD__",
                        *selectors, f"-D_TIME_BITS={width}", "input.c", "-o", "input.i"],
                        label + ".preprocess", cwd=directory))
                    smaller.append(command_case([str(compiler), "input.i", "output.s"],
                                                label + ".compile", cwd=directory))
                    smaller.append(command_case([assembler, "-mcpu=cortex-m0plus", "-o",
                                                "output.o", "output.s"],
                                                label + ".assemble", cwd=directory))
        (work / "invalid.i").write_text("int invalid( {\n")
        invalid = subprocess.run([str(compiler), "invalid.i", "invalid.s"], cwd=work,
                                 capture_output=True, text=True, check=False,
                                 env=dict(os.environ, LC_ALL="C"))
        syntax_rejection(invalid)
    after = source_snapshot(root)
    require(all(before[field] == after[field] for field in
                ("commit", "tracked_files", "index_entries")), "stdio inputs change during scan")
    require(file_digest(Path(__file__)) == verifier, "stdio verifier changes during scan")
    return {"schema_version": 1, "source_commit": before["commit"],
            "header_sha256": file_digest(root / "include/stdio.h"),
            "verifier_sha256": verifier, "cases": cases, "smaller_cases": smaller,
            "diagnostic_controls": diagnostic_controls(), "syntax_controls": syntax_controls(),
            "tools": {"gcc": tool_identity(cross), "clang": tool_identity(clang)},
            "runtime": "unrun", "physical_hardware": "unrun"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path)
    parser.add_argument("--host-cc", default=os.environ.get("HOST_CC", "cc"))
    args = parser.parse_args()
    try:
        result = run(args.root.resolve(), args.host_cc)
        if args.output:
            args.output.write_text(json.dumps(result, indent=2) + "\n")
        print("PASS callable stdio: signatures, imports, macro suppression and Smaller C input")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("character-io-headers: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
