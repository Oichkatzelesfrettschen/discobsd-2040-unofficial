"""Check root-header inclusion and curses integer ABI across boolean modes."""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from passwd_header_contracts import compile_case, file_digest, require, tool_identity
from record_header_contracts import syntax_controls, syntax_rejection
from string_header_contracts import command_case

MODES = {
    "default": [],
    "iso": ["-D_ANSI_SOURCE=1"],
    "issue7": ["-D_POSIX_C_SOURCE=200809L"],
    "issue8": ["-D_POSIX_C_SOURCE=202405L"],
}
GUARDS = {
    "curses.h": "CURSES_H",
    "mtab.h": "_MTAB_H_",
    "regexp.h": "_REGEXP_H_",
    "ttyent.h": "_TTYENT_H_",
    "tzfile.h": "_TZFILE_H_",
    "vmf.h": "_VMF_H_",
}


def curses_fixture(order):
    lines = [f"#include <{name}>" for name in order]
    lines.append("#include <stddef.h>")
    lines += [
        '_Static_assert(sizeof(WINDOW) == 48, "WINDOW size");',
        '_Static_assert(_Alignof(WINDOW) == 4, "WINDOW alignment");',
    ]
    for field, offset in (("_clear", 16), ("_leave", 20), ("_scroll", 24)):
        lines += [
            f'_Static_assert(offsetof(WINDOW, {field}) == {offset}, "WINDOW offset");',
            f'_Static_assert(_Generic(&((WINDOW *)0)->{field}, int *: 1, '
            'default: 0), "WINDOW integer field");',
        ]
    for name in ("AM", "BS", "GT", "NONL", "_pfast", "My_term", "_echoit",
                 "_rawmode", "_endwin"):
        lines.append(f'_Static_assert(_Generic(&{name}, int *: 1, default: 0), '
                     '"curses integer object");')
    if "stdbool.h" in order:
        lines += [
            '_Static_assert(_Generic((bool)0, _Bool: 1, default: 0), "C17 bool type");',
            '_Static_assert((bool)2 == 1, "C17 bool conversion");',
        ]
    else:
        lines.append('_Static_assert(_Generic((bool)0, int: 1, default: 0), '
                     '"legacy curses bool type");')
    lines.append("int main(void) { return 0; }")
    return "\n".join(lines) + "\n"


def native_cases(root, work, cross, host_cc):
    host = shlex.split(host_cc)
    require(host and shutil.which(host[0]), "host compiler is required")
    compiler = work / "smlrc"
    assembler = str(Path(cross).with_name("arm-none-eabi-as"))
    require(Path(assembler).is_file(), "ARM assembler is required")
    cases = [command_case([*host, "-O1", "-Wall", "-Wextra", "-Werror", "-DTHUMB",
                           "-DNO_ANNOTATIONS", "-DNO_PPACK", "-DSTATIC", "-DNO_EXTRA_WARNS",
                           "-DSYNTAX_STACK_MAX=3200", "-o", str(compiler),
                           str(root / "usr.bin/smlrc/smlrc.c")], "smaller.build")]
    orders = [(h,) * n for h in GUARDS for n in (1, 2)]
    orders += [("stdbool.h",), ("curses.h", "stdbool.h"), ("stdbool.h", "curses.h")]
    unsupported = []
    for index, order in enumerate(orders):
        source = "".join(f"#include <{h}>\n" for h in order)
        if "curses.h" in order:
            source += "typedef char window_size[sizeof(WINDOW) == 48 ? 1 : -1];\n"
            source += "int *mode_address = &AM;\n"
            source += "int *field_address(WINDOW *w) { return &w->_clear; }\n"
        source += "int main(void) { return 0; }\n"
        (work / "input.c").write_text(source)
        # C90 preprocessing selects stdbool's enum fallback, matching the
        # absence of a C99 version declaration in Smaller C's input contract.
        cases.append(command_case(
            [cross, "-std=c90", "-E", "-P", "-nostdinc", "-I" + str(root / "include"),
             "-U__GNUC__", "-U__PCC__", "-D__SMALLER_C__=0x0100", "-D__SMALLER_C_32__",
             "-D__SMALLER_C_UCHAR__", "-D__arm__", "-D__unix__", "-D__BSD__",
             "-D__DISCOBSD__", "input.c", "-o", "input.i"],
            f"smaller.{index}.preprocess", cwd=work))
        cases[-1]["source"] = source
        cases[-1]["expanded_sha256"] = file_digest(work / "input.i")
        command = [str(compiler), "input.i", "output.s"]
        if "stdbool.h" in order:
            result = subprocess.run(command, cwd=work, capture_output=True, text=True,
                                    check=False, env=dict(os.environ, LC_ALL="C"))
            if result.returncode != 0:
                require(result.returncode == 1 and not result.stderr and
                        re.fullmatch(r'Error in "input.i" \([0-9]+:[0-9]+\)\n'
                                     r'Unexpected token _Bool\n', result.stdout),
                        "native boolean input fails outside its known parser limit: "
                        + result.stdout + result.stderr)
                unsupported.append({"headers": order, "state": "unsupported",
                                    "reason": result.stdout, "source": source,
                                    "expanded_sha256": file_digest(work / "input.i")})
                continue
        cases.append(command_case(command, f"smaller.{index}.compile", cwd=work))
        cases.append(command_case([assembler, "-mcpu=cortex-m0plus", "-o", "output.o",
                                  "output.s"], f"smaller.{index}.assemble", cwd=work))
    (work / "invalid.i").write_text("int broken( {\n")
    invalid = subprocess.run([str(compiler), "invalid.i", "invalid.s"], cwd=work,
                             capture_output=True, text=True, check=False,
                             env=dict(os.environ, LC_ALL="C"))
    syntax_rejection(invalid)
    return {"cases": cases, "syntax_controls": syntax_controls(),
            "unsupported_inputs": unsupported,
            "invalid_syntax_exit": invalid.returncode,
            "scope": "12 legacy inputs plus three boolean inputs; unsupported inputs "
                     "remain open and supply no acceptance or runtime evidence"}


def run(root, host_cc):
    names = subprocess.check_output(
        ["git", "-c", "core.fsmonitor=false", "-C", str(root),
         "ls-files", "-z", "include"], text=True).split("\0")
    headers = sorted(Path(name).name for name in names
                     if Path(name).parent == Path("include") and name.endswith(".h"))
    require(headers and set(GUARDS) <= set(headers), "root-header denominator is incomplete")
    cross, clang = shutil.which("arm-none-eabi-gcc"), shutil.which("clang")
    require(cross and clang, "ARM GCC and Clang are required")
    flags = ["-std=c17", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
             "-nostdinc", "-I" + str(root / "include"), "-mcpu=cortex-m0plus",
             "-mthumb", "-mfloat-abi=soft"]
    profiles = {"gcc.arm.c17": [cross, *flags],
                "clang.arm.c17": [clang, "--target=arm-none-eabi", *flags]}
    cases = []
    with tempfile.TemporaryDirectory(prefix="public-header-inclusion-") as directory:
        work = Path(directory)
        source = work / "consumer.c"
        for profile, command in profiles.items():
            source.write_text("int main(void) { return 0; }\n")
            cases.append(compile_case(command, source, profile + ".good-control"))
            source.write_text('#error "header rejection control"\n')
            cases.append(compile_case(command, source, profile + ".bad-control",
                                      "header rejection control"))
            for mode, selectors in MODES.items():
                arguments = [*command, *selectors]
                for header in headers:
                    for count in (1, 2):
                        source.write_text(f"#include <{header}>\n" * count
                                          + "int main(void) { return 0; }\n")
                        cases.append(compile_case(arguments, source,
                                                  f"{profile}.{mode}.{header}.{count}"))
                for label, order in (("forward", headers), ("reverse", headers[::-1])):
                    source.write_text("".join(f"#include <{h}>\n" for h in order * 2)
                                      + "int main(void) { return 0; }\n")
                    cases.append(compile_case(arguments, source, f"{profile}.{mode}.{label}"))
                for order in (("curses.h",), ("curses.h", "stdbool.h"),
                              ("stdbool.h", "curses.h")):
                    source.write_text(curses_fixture(order))
                    cases.append(compile_case(arguments, source,
                                              f"{profile}.{mode}.abi.{'.'.join(order)}"))
            for header, guard in GUARDS.items():
                original = (root / "include" / header).read_text()
                anchor = "#ifndef " + guard
                require(original.count(anchor) == 1, "guard mutation anchor differs: " + header)
                overlay = work / (profile + "-" + header)
                overlay.mkdir()
                (overlay / header).write_text(original.replace(anchor, "#if 1"))
                source.write_text(f"#include <{header}>\n" * 2)
                cases.append(compile_case([command[0], "-I" + str(overlay), *command[1:]],
                                          source, profile + ".guard." + header, "redefinition"))
            original = (root / "include/curses.h").read_text()
            for label, before, after, diagnostic in (
                    ("field-storage", "int         _clear;", "bool        _clear;",
                     "static assertion failed.*WINDOW"),
                    ("object-storage", "extern int AM,", "extern bool AM,",
                     "static assertion failed.*curses integer object"),
                    ("boolean-macro", "typedef int bool;", "#define bool int",
                     "redefined")):
                require(original.count(before) == 1, label + " mutation anchor differs")
                overlay = work / (profile + "-" + label)
                overlay.mkdir()
                (overlay / "curses.h").write_text(original.replace(before, after))
                order = (("curses.h", "stdbool.h") if label == "boolean-macro"
                         else ("stdbool.h", "curses.h"))
                source.write_text(curses_fixture(order))
                cases.append(compile_case([command[0], "-I" + str(overlay), *command[1:]],
                                          source, profile + "." + label, diagnostic))
        smaller = native_cases(root, work, cross, host_cc)
    return {"schema_version": 1, "evidence_class": "compiler-acceptance",
            "headers": {h: file_digest(root / "include" / h) for h in headers},
            "profiles": profiles, "cases": cases,
            "tools": {"gcc": tool_identity(cross), "clang": tool_identity(clang)},
            "smaller_c": smaller,
            "verifier_sha256": file_digest(Path(__file__)),
            "scope": "Tracked root headers; nested headers and runtime remain separate."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path)
    parser.add_argument("--host-cc", default=os.environ.get("HOST_CC", "cc"))
    args = parser.parse_args()
    try:
        receipt = run(args.root.resolve(), args.host_cc)
        if args.output:
            args.output.write_text(json.dumps(receipt, indent=2) + "\n")
        print(f"PASS public header inclusion: {len(receipt['headers'])} headers; "
              f"{len(receipt['cases'])} compiler cases")
        native = receipt["smaller_c"]
        accepted = sum(c["label"].endswith(".compile") for c in native["cases"])
        print(f"Smaller C: {accepted} inputs compile and assemble; "
              f"{len(native['unsupported_inputs'])} boolean inputs remain unsupported")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("FAIL public header inclusion: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
