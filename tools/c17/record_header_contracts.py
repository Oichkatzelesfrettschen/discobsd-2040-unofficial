"""Calibrate group, device and login-record header inclusion and target ABI."""

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
from string_header_contracts import command_case

HEADERS = ("grp.h", "kmem.h", "lastlog.h")
FORMS = ("standalone", "repeated", "types-before", "types-after")
MODES = {"default": [], "iso": ["-D_ANSI_SOURCE=1"],
         "issue7": ["-D_POSIX_C_SOURCE=200809L"],
         "issue8": ["-D_POSIX_C_SOURCE=202405L"]}


def fixture(header, form, wide, smaller=False):
    lines = ["#include <sys/types.h>"] if form == "types-before" else []
    lines += [f"#include <{header}>"] * (2 if form == "repeated" else 1)
    if form == "types-after":
        lines.append("#include <sys/types.h>")
    if smaller:
        if header == "grp.h":
            lines.append("typedef char group_width[sizeof(struct group) == 16 ? 1 : -1];")
        elif header == "lastlog.h":
            lines.append("typedef char lastlog_width[sizeof(struct lastlog) == 28 ? 1 : -1];")
        else:
            lines.append("dev_t (*device_function)(void) = kmemdev;")
    else:
        lines.append("#include <stddef.h>")
        conditions = {"target pointer": "sizeof(void *) == 4"}
        signatures = {}
        if header == "grp.h":
            conditions.update({"group size": "sizeof(struct group) == 16",
                               "group alignment": "_Alignof(struct group) == 4"})
            fields = (("gr_name", 0, "char **"), ("gr_passwd", 4, "char **"),
                      ("gr_gid", 8, "int *"), ("gr_mem", 12, "char ***"))
            for name, offset, typename in fields:
                conditions[f"group {name} offset"] = f"offsetof(struct group, {name}) == {offset}"
                conditions[f"group {name} type"] = (
                    f"_Generic(&((struct group *)0)->{name}, {typename}: 1, default: 0)")
            signatures = {"getgrent": "struct group *(*)(void)",
                          "getgrnam": "struct group *(*)(const char *)",
                          "getgrgid": "struct group *(*)(gid_t)",
                          "setgrent": "void (*)(void)", "endgrent": "void (*)(void)",
                          "setgroups": "int (*)(size_t, const gid_t *)"}
        elif header == "lastlog.h":
            # ll_time is the kernel's 32-bit count (utime32_t), so the record
            # keeps one layout under both views of time_t.
            width = 4
            conditions.update({"lastlog size": f"sizeof(struct lastlog) == {width + 24}",
                               "lastlog alignment": f"_Alignof(struct lastlog) == {width}",
                               "lastlog time type":
                               "_Generic(&((struct lastlog *)0)->ll_time, "
                               "unsigned long *: 1, default: 0)",
                               "lastlog time": "offsetof(struct lastlog, ll_time) == 0",
                               "lastlog line": f"offsetof(struct lastlog, ll_line) == {width}",
                               "lastlog host": f"offsetof(struct lastlog, ll_host) == {width + 8}",
                               "lastlog line width": "sizeof(((struct lastlog *)0)->ll_line) == 8",
                               "lastlog host width":
                               "sizeof(((struct lastlog *)0)->ll_host) == 16"})
        else:
            signatures = {"kmemdev": "dev_t (*)(void)"}
        for name, signature in signatures.items():
            conditions[name + " signature"] = f"_Generic(&{name}, {signature}: 1, default: 0)"
        lines += [f'_Static_assert({condition}, "{label}");'
                  for label, condition in conditions.items()]
    lines.append("int main(void) { return 0; }")
    return "\n".join(lines) + "\n"


def mutations(root):
    originals = {name: (root / "include" / name).read_text() for name in HEADERS}
    specifications = [
        ("group-guard", "grp.h", "#ifndef _GRP_H_", "#if 1", "repeated", False,
         r"redefinition.*group"),
        ("lastlog-guard", "lastlog.h", "#ifndef _LASTLOG_H_", "#if 1", "repeated", False,
         r"redefinition.*lastlog"),
        ("group-types", "grp.h", "#include <sys/types.h>", "", "standalone", False,
         r"unknown type name"),
        ("device-types", "kmem.h", "#include <sys/types.h>", "", "standalone", False,
         r"unknown type name"),
        ("lastlog-types", "lastlog.h", "#include <sys/_time_t.h>", "", "standalone", False,
         r"unknown type name"),
        ("group-signedness", "grp.h", "int     gr_gid;", "unsigned int gr_gid;",
         "standalone", False, r"static assertion failed.*group gr_gid type"),
        ("lastlog-width", "lastlog.h", "utime32_t ll_time;", "time_t ll_time;",
         "standalone", True, r"static assertion failed.*lastlog"),
        ("device-prototype", "kmem.h", "kmemdev(void)", "kmemdev()",
         "standalone", False, r"(?:strict-prototypes|without a prototype|isn't a prototype)"),
    ]
    for label, header, before, after, form, wide, diagnostic in specifications:
        require(originals[header].count(before) == 1, label + " mutation anchor differs")
        yield label, header, originals[header].replace(before, after), form, wide, diagnostic


def syntax_rejection(result):
    require(result.returncode == 1 and not result.stderr and
            re.fullmatch(r'Error in "invalid.i" \(1:[0-9]+\)\nUnexpected token \{\n',
                         result.stdout) is not None,
            "Smaller C syntax rejection differs: " + result.stdout + result.stderr)


def syntax_controls():
    controls = []
    for label, code, stdout, stderr in [
            ("signal", -11, "", ""),
            ("unrelated-error", 1, "Cannot open invalid.i\n", ""),
            ("false-success", 0, 'Error in "invalid.i" (1:14)\nUnexpected token {\n', ""),
            ("diagnostic-stream", 1, 'Error in "invalid.i" (1:14)\nUnexpected token {\n',
             "unexpected diagnostic\n")]:
        result = subprocess.CompletedProcess(["diagnostic-control"], code, stdout, stderr)
        try:
            syntax_rejection(result)
        except ValueError:
            controls.append({"label": label, "state": "rejected", "exit_code": code,
                             "evidence_class": "diagnostic-fixture"})
        else:
            raise ValueError("syntax verdict accepts " + label)
    return controls


def native_cases(root, workspace, host, cross):
    compiler = workspace / "smlrc"
    cases = [command_case([*host, "-O1", "-Wall", "-Wextra", "-Werror", "-DTHUMB",
                           "-DNO_ANNOTATIONS", "-DNO_PPACK", "-DSTATIC", "-DNO_EXTRA_WARNS",
                           "-DSYNTAX_STACK_MAX=3200", "-o", str(compiler),
                           str(root / "usr.bin/smlrc/smlrc.c")], "smaller.build")]
    assembler = str(Path(cross).with_name(Path(cross).name.replace("gcc", "as")))
    require(Path(assembler).is_file(), "target assembler is absent")
    for header in HEADERS:
        for form in FORMS:
            for wide in (False, True):
                label = f"{header}.{form}.time{64 if wide else 32}"
                source = workspace / "input.c"
                source.write_text(fixture(header, form, wide, smaller=True))
                flags = ["-D_TIME_BITS=64"] if wide else []
                cases.append(command_case(
                    [cross, "-E", "-P", "-nostdinc", "-I" + str(root / "include"),
                     "-U__GNUC__", "-U__PCC__", "-D__SMALLER_C__=0x0100", "-D__SMALLER_C_32__",
                     "-D__SMALLER_C_UCHAR__", "-D__arm__", "-D__unix__", "-D__BSD__",
                     "-D__DISCOBSD__", *flags, "input.c", "-o", "input.i"],
                    label + ".preprocess", cwd=workspace))
                cases[-1]["source"] = source.read_text()
                cases[-1]["expanded_sha256"] = file_digest(workspace / "input.i")
                cases.append(command_case([str(compiler), "input.i", "output.s"],
                                          label + ".compile", cwd=workspace))
                cases.append(command_case([assembler, "-mcpu=cortex-m0plus", "-o", "output.o",
                                           "output.s"], label + ".assemble", cwd=workspace))
    (workspace / "invalid.i").write_text("int broken( {\n")
    command = [str(compiler), "invalid.i", "invalid.s"]
    invalid = subprocess.run(command, cwd=workspace, capture_output=True, text=True,
                             check=False, env=dict(os.environ, LC_ALL="C"))
    syntax_rejection(invalid)
    cases.append({"label": "smaller.invalid", "argv": command, "exit_code": invalid.returncode,
                  "stdout": invalid.stdout, "stderr": invalid.stderr, "state": "rejected"})
    return {"cases": cases, "syntax_controls": syntax_controls(),
            "assembler": tool_identity(assembler),
            "sources": {name: file_digest(root / "usr.bin/smlrc" / name)
                        for name in ("smlrc.c", "cgthumb.c")},
            "scope": "24 cross-preprocessed inputs compile and assemble; native runtime unrun"}


def run(root, host_cc):
    host = shlex.split(host_cc)
    require(host and shutil.which(host[0]), "host compiler is absent")
    host[0] = shutil.which(host[0])
    clang = shutil.which("clang")
    require(clang is not None, "Clang is required")
    environment = dict(os.environ, LC_ALL="C")
    environment.pop("C17_CAPTURE_DIR", None)
    production = shlex.split(subprocess.check_output(
        ["bmake", "-C", str(root / "lib/libc"), "MACHINE=rp2040", "-V",
         "${CC} ${CFLAGS} ${CPPFLAGS}"], text=True, env=environment))
    require(production and all(flag in production for flag in
                              ("-std=gnu17", "-mcpu=cortex-m0plus", "-mthumb",
                               "-mfloat-abi=soft", "-Wall", "-Wextra", "-Werror")),
            "production compiler profile differs")
    strict = ["-std=c17", "-pedantic-errors", "-Wstrict-prototypes", "-Wold-style-definition"]
    profiles = {"arm.gnu17": [*production, "-Wstrict-prototypes"],
                "arm.c17": [*production, *strict],
                "clang.arm.c17": [clang, "--target=arm-none-eabi", "-mcpu=cortex-m0plus",
                                  "-mthumb", "-mfloat-abi=soft", "-nostdinc",
                                  "-I" + str(root / "include"), "-Wall", "-Wextra", "-Werror",
                                  *strict]}
    cases = []
    with tempfile.TemporaryDirectory(prefix="record-headers-") as directory:
        workspace = Path(directory)
        source = workspace / "consumer.c"
        for profile, arguments in profiles.items():
            for header in HEADERS:
                for form in FORMS:
                    for wide in (False, True):
                        source.write_text(fixture(header, form, wide))
                        time_flags = ["-D_TIME_BITS=64"] if wide else []
                        for mode, flags in MODES.items():
                            label = f"{profile}.{header}.{form}.{mode}.time{64 if wide else 32}"
                            cases.append(compile_case([*arguments, *flags, *time_flags],
                                                      source, label))
            for label, header, content, form, wide, diagnostic in mutations(root):
                mutated = workspace / label
                mutated.mkdir(exist_ok=True)
                (mutated / header).write_text(content)
                source.write_text(fixture(header, form, wide))
                flags = ["-D_TIME_BITS=64"] if wide else []
                cases.append(compile_case([arguments[0], "-I" + str(mutated), *arguments[1:],
                                           *flags], source, profile + "." + label, diagnostic))
            source.write_text("#include <kmem.h>\nint main(void) { return kmemdev(1); }\n")
            cases.append(compile_case(arguments, source, profile + ".extra-argument",
                                      r"too many arguments"))
        smaller = native_cases(root, workspace, host, production[0])
    require(len(cases) == 315, "record-header compiler denominator differs")
    return {"schema_version": 1, "evidence_class": "compiler-acceptance",
            "sources": {name: file_digest(root / "include" / name) for name in HEADERS},
            "verifier_sha256": file_digest(Path(__file__)), "profiles": profiles,
            "tools": {"arm": tool_identity(production[0]), "clang": tool_identity(clang),
                      "host": tool_identity(host[0])},
            "cases": cases, "smaller_c": smaller, "hardware": "unrun"}


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
        print("PASS record header contracts: 288 valid compiler cases, 27 rejections; "
              "24 Smaller C inputs compile and assemble")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("FAIL record header contracts: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
