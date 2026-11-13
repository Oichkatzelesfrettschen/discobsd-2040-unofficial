"""Check accounting-header inclusion and preserved target record layouts."""

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from passwd_header_contracts import compile_case, file_digest, require, tool_identity
from string_header_contracts import command_case

RECORDS = {
    "grp": ("group", 16, {"gr_name": 0, "gr_passwd": 4, "gr_gid": 8, "gr_mem": 12}),
    "lastlog": ("lastlog", 28, {"ll_time": 0, "ll_line": 4, "ll_host": 12}),
    "mtab": ("mtab", 68, {"m_path": 0, "m_dname": 32, "m_type": 64}),
    "ttyent": ("ttyent", 24, {"ty_name": 0, "ty_getty": 4, "ty_type": 8,
                              "ty_status": 12, "ty_window": 16, "ty_comment": 20}),
    "tzfile": ("tzhead", 44, {"tzh_reserved": 0, "tzh_timecnt": 32,
                             "tzh_typecnt": 36, "tzh_charcnt": 40}),
    "utmp": ("utmp", 44, {"ut_line": 0, "ut_name": 8, "ut_host": 23, "ut_time": 40}),
}


def fixture(name, form, target):
    include = f"#include <{name}.h>\n"
    peers = "#include <sys/types.h>\n#include <stddef.h>\n"
    forms = {"standalone": include, "repeated": include + include,
             "peers-before": peers + include + include,
             "peers-after": include + peers + include}
    source = forms[form] + "#include <stddef.h>\n"
    record, size, fields = RECORDS[name]
    if target:
        source += '_Static_assert(sizeof(void *) == 4, "target pointer width");\n'
        source += f'_Static_assert(sizeof(struct {record}) == {size}, "record size");\n'
        for field, offset in fields.items():
            source += (f'_Static_assert(offsetof(struct {record}, {field}) == {offset}, '
                       f'"{field} offset");\n')
        alignment = 1 if name in {"mtab", "tzfile"} else 4
        source += (f'_Static_assert(_Alignof(struct {record}) == {alignment}, '
                   '"record alignment");\n')
    if name == "grp":
        source += ('_Static_assert(_Generic(&((struct group *)0)->gr_gid, '
                   'int *: 1, default: 0), "signed legacy group field");\n'
                   '_Static_assert(_Generic(&getgrgid, struct group *(*)(gid_t): 1, '
                   'default: 0), "group lookup signature");\n'
                   '_Static_assert(_Generic(&setgroups, int (*)(size_t, const gid_t *): 1, '
                   'default: 0), "group vector signature");\n')
    if name == "lastlog":
        source += ('_Static_assert(_Generic(&((struct lastlog *)0)->ll_time, '
                   'time_t *: 1, default: 0), "login time type");\n')
    return source + "typedef int accounting_header_probe;\n"


def smaller_cases(root, workspace, host, cross):
    compiler = workspace / "smlrc"
    cases = [command_case([*host, "-O1", "-Wall", "-Wextra", "-Werror", "-DTHUMB",
                           "-DNO_ANNOTATIONS", "-DNO_PPACK", "-DSTATIC", "-DNO_EXTRA_WARNS",
                           "-DSYNTAX_STACK_MAX=3200", "-o", str(compiler),
                           str(root / "usr.bin/smlrc/smlrc.c")], "smaller.build")]
    assembler = str(Path(cross).with_name(Path(cross).name.replace("gcc", "as")))
    require(Path(assembler).is_file(), "target assembler is absent")
    for form in ("standalone", "repeated", "peers-before", "peers-after"):
        names = list(RECORDS)
        if form == "peers-after":
            names.reverse()
        lines = ["#include <sys/types.h>"] if form == "peers-before" else []
        for name in names:
            lines.append(f"#include <{name}.h>")
            if form == "repeated":
                lines.append(f"#include <{name}.h>")
        if form == "peers-after":
            lines.append("#include <sys/types.h>")
        for name, (record, size, _) in RECORDS.items():
            lines.append(f"typedef char size_{name}[(sizeof(struct {record}) == {size}) ? 1 : -1];")
        lines.extend(["struct group *(*lookup)(gid_t) = getgrgid;",
                      "int (*groups)(size_t, const gid_t *) = setgroups;",
                      "int main(void) { return 0; }"])
        source = workspace / ("smaller." + form + ".c")
        source.write_text("\n".join(lines) + "\n")
        expanded = source.with_suffix(".i")
        assembly = source.with_suffix(".s")
        cases.append(command_case(
            [cross, "-E", "-P", "-nostdinc", "-I" + str(root / "include"),
             "-U__GNUC__", "-U__PCC__", "-D__SMALLER_C__=0x0100", "-D__SMALLER_C_32__",
             "-D__SMALLER_C_UCHAR__", "-D__arm__", "-D__unix__", "-D__BSD__",
             "-D__DISCOBSD__", str(source), "-o", str(expanded)], "smaller.preprocess." + form))
        cases[-1]["source"] = source.read_text()
        cases[-1]["expanded_sha256"] = file_digest(expanded)
        cases.append(command_case([str(compiler), str(expanded), str(assembly)],
                                  "smaller.compile." + form))
        cases.append(command_case([assembler, "-mcpu=cortex-m0plus", "-mthumb",
                                   "-o", str(source.with_suffix(".o")), str(assembly)],
                                  "smaller.assemble." + form))
    invalid = workspace / "invalid.i"
    invalid.write_text("int broken( {\n")
    cases.append(command_case([str(compiler), str(invalid), str(workspace / "invalid.s")],
                              "smaller.invalid-input", reject=True))
    return {"cases": cases, "compiler_source": file_digest(root / "usr.bin/smlrc/smlrc.c"),
            "backend_source": file_digest(root / "usr.bin/smlrc/cgthumb.c"),
            "assembler": tool_identity(assembler),
            "scope": "Host preprocessing supplies expanded input to Smaller C; "
                     "native preprocessing and runtime execution remain unrun."}


def run(root, host_cc):
    strict = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
              "-Wstrict-prototypes", "-nostdinc", "-I" + str(root / "include")]
    host = shlex.split(host_cc)
    require(host, "host compiler command is empty")
    clang = shutil.which("clang")
    host_tool = shutil.which(host[0])
    require(clang and host_tool, "host compiler and Clang are required")
    host[0] = host_tool
    environment = dict(os.environ, LC_ALL="C")
    environment.pop("C17_CAPTURE_DIR", None)
    production = shlex.split(subprocess.check_output(
        ["bmake", "-C", str(root / "lib/libc"), "MACHINE=rp2040", "-V",
         "${CC} ${CFLAGS} ${CPPFLAGS}"], text=True, env=environment))
    require(production and all(flag in production for flag in
            ["-std=gnu17", "-mcpu=cortex-m0plus", "-mthumb", "-Wall", "-Wextra", "-Werror"]),
            "libc production compiler profile differs")
    profiles = [("host.c17", [*host, *strict], False),
                ("clang.c17", [clang, *strict], False),
                ("clang.arm.c17", [clang, *strict, "--target=arm-none-eabi",
                                   "-mcpu=cortex-m0plus", "-mthumb", "-ffreestanding"], True),
                ("arm.gnu17", production, True),
                ("arm.c17", [*production, "-std=c17", "-pedantic-errors",
                             "-Wstrict-prototypes"], True)]
    cases = []
    with tempfile.TemporaryDirectory(prefix="discobsd-accounting-headers-") as temporary:
        workspace = Path(temporary)
        for profile, command, target in profiles:
            for name, (record, _, _) in RECORDS.items():
                for form in ["standalone", "repeated", "peers-before", "peers-after"]:
                    label = profile + "." + name + "." + form
                    source = workspace / (label + ".c")
                    source.write_text(fixture(name, form, target))
                    cases.append(compile_case(command, source, label))
                original = (root / "include" / (name + ".h")).read_text()
                guard = "_" + name.upper() + "_H_"
                marker = "#define " + guard + "\n"
                require(original.count(marker) == 1, name + " guard mutation is ambiguous")
                mutant = workspace / (profile + "." + name + ".mutant")
                mutant.mkdir()
                (mutant / (name + ".h")).write_text(original.replace(marker, "", 1))
                source = workspace / (profile + "." + name + ".guard.c")
                source.write_text(fixture(name, "repeated", target))
                cases.append(compile_case([command[0], "-I" + str(mutant), *command[1:]],
                                          source, profile + "." + name + ".guard-disabled",
                                          r"redefinition.*" + record))
                if name in {"grp", "lastlog"}:
                    marker = "#include <sys/types.h>\n"
                    require(original.count(marker) == 1, name + " type mutation is ambiguous")
                    (mutant / (name + ".h")).write_text(original.replace(marker, "", 1))
                    source.write_text(fixture(name, "standalone", target))
                    cases.append(compile_case([command[0], "-I" + str(mutant), *command[1:]],
                                              source, profile + "." + name + ".types-removed",
                                              r"unknown type name.*" +
                                              ("gid_t" if name == "grp" else "time_t")))
        smaller = smaller_cases(root, workspace, host, production[0])
    require(len(cases) == len(profiles) * (len(RECORDS) * 5 + 2),
            "accounting-header case denominator differs")
    return {"schema_version": 1, "evidence_class": "compiler-acceptance",
            "headers": {name + ".h": file_digest(root / "include" / (name + ".h"))
                        for name in RECORDS},
            "verifier_sha256": file_digest(Path(__file__)),
            "tools": {"host": tool_identity(host_tool), "clang": tool_identity(clang),
                      "arm": tool_identity(production[0])},
            "cases": cases, "smaller": smaller, "hardware": "unrun",
            "scope": "Default visibility, six accounting headers; namespaces and runtime "
                     "database behavior retain separate contracts."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--host-cc", default=os.environ.get("HOST_CC", "cc"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        receipt = run(args.root.resolve(), args.host_cc)
        if args.output:
            args.output.write_text(json.dumps(receipt, indent=2) + "\n")
        print(f"PASS accounting header contracts: {len(receipt['cases'])} compiler cases; "
              f"{len(receipt['smaller']['cases'])} Smaller C operations")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("FAIL accounting header contracts: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
