"""Calibrate public time-type completeness, ownership, and target layouts."""

import argparse
import hashlib
import itertools
import json
import os
import re
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

PUBLIC = ("time.h", "sys/time.h", "sys/select.h", "signal.h")
OWNERS = ("sys/_timespec.h", "sys/_timeval.h", "sys/_sigset_t.h", "sys/_suseconds_t.h")
FORMS = {header + "." + form: (header,) * (2 if form == "repeated" else 1)
         for header in (*PUBLIC, "sys/types.h") for form in ("standalone", "repeated")}
FORMS.update({"order." + ".".join(headers): headers
              for headers in itertools.permutations(PUBLIC)})


def fixture(headers, wide, smaller=False, kernel=False):
    lines = [f"#include <{header}>" for header in headers]
    if set(headers) == {"time.h"}:
        lines.extend(["#if defined(_SYS_TIME_H_) || defined(_SYS_SELECT_H_) || "
                      "defined(_SYS_TYPES_H_)",
                      '#error "ISO time entry imports public system headers"', "#endif"])
    complete_time = bool(set(headers) & {"time.h", "sys/time.h", "sys/select.h"})
    complete_value = bool(set(headers) & {"sys/time.h", "sys/select.h"})
    signal_type = bool(set(headers) & {"sys/select.h", "signal.h"})
    micro_type = complete_value or "sys/types.h" in headers
    width = 8 if wide and not smaller and not kernel else 4
    scalar = "long long" if width == 8 else "unsigned long"
    assertions = {"time width": f"sizeof(time_t) == {width}",
                  "time scalar": f"_Generic((time_t)0, {scalar}: 1, default: 0)"}
    if width == 4:
        assertions["time unsigned"] = "(time_t)-1 > 0"
    types = []
    if complete_time:
        assertions.update({"timespec size": f"sizeof(struct timespec) == {16 if width == 8 else 8}",
                           "timespec seconds":
                           "_Generic(&((struct timespec *)0)->tv_sec, time_t *: 1, default: 0)",
                           "timespec fraction":
                           "_Generic(&((struct timespec *)0)->tv_nsec, long *: 1, default: 0)"})
        types.append("timespec")
    if complete_value:
        seconds = "time_t" if width == 8 else "long"
        assertions.update({"timeval size": f"sizeof(struct timeval) == {16 if width == 8 else 8}",
                           "timeval seconds":
                           f"_Generic(&((struct timeval *)0)->tv_sec, {seconds} *: 1, default: 0)",
                           "timeval fraction":
                           "_Generic(&((struct timeval *)0)->tv_usec, "
                           "suseconds_t *: 1, default: 0)"})
        types.append("timeval")
    if signal_type:
        assertions["signal scalar"] = "_Generic((sigset_t)0, unsigned long: 1, default: 0)"
        assertions["signal width"] = "sizeof(sigset_t) == 4"
        assertions["signal unsigned"] = "(sigset_t)-1 > 0"
    if micro_type:
        assertions["microsecond scalar"] = "_Generic((suseconds_t)0, long: 1, default: 0)"
        assertions["microsecond width"] = "sizeof(suseconds_t) == 4"
        assertions["microsecond signed"] = "(suseconds_t)-1 < 0"
    if not smaller:
        lines.append("#include <stddef.h>")
        for name in types:
            field = "tv_nsec" if name == "timespec" else "tv_usec"
            assertions[name + " alignment"] = f"_Alignof(struct {name}) == {width}"
            assertions[name + " seconds offset"] = f"offsetof(struct {name}, tv_sec) == 0"
            assertions[name + " fraction offset"] = f"offsetof(struct {name}, {field}) == {width}"
        if kernel:
            assertions.update({"kernel timeval": "sizeof(struct __timeval32) == 8",
                               "kernel timespec": "sizeof(struct __timespec32) == 8"})
        lines.extend(f'_Static_assert({value}, "{label}");'
                     for label, value in assertions.items())
    else:
        for label, condition in assertions.items():
            if "_Generic" not in condition:
                name = label.replace(" ", "_")
                lines.append(f"typedef char {name}[{condition} ? 1 : -1];")
        if micro_type:
            lines.append("suseconds_t microseconds;")
        if signal_type:
            lines.append("sigset_t signal_mask;")
    if "sys/select.h" in headers:
        lines.append("int (*select_import)(int, fd_set *, fd_set *, fd_set *, "
                     "struct timeval *) = select;")
    if "time.h" in headers:
        lines.append("time_t (*time_import)(time_t *) = time;")
    lines.append("int main(void) { return 0; }")
    return "\n".join(lines) + "\n"


def mutations(root):
    specifications = [
        ("time-owner", "time.h", "#include <sys/_timespec.h>", "", ("time.h",), True,
         r"error:.*(?:incomplete|undefined).*timespec|error:.*timespec.*(?:incomplete|undefined)"),
        ("time-namespace", "time.h", "#include <sys/_timespec.h>",
         "#include <sys/time.h>", ("time.h",), True,
         r"error:.*ISO time entry imports public system headers"),
        ("select-timespec", "sys/select.h", "#include <sys/_timespec.h>", "",
         ("sys/select.h",), True, r"error:.*(?:incomplete|undefined).*timespec"),
        ("select-timeval", "sys/select.h", "#include <sys/_timeval.h>", "",
         ("sys/select.h",), True, r"error:.*(?:incomplete|undefined).*timeval"),
        ("timespec-width", "sys/_timespec.h", "time_t tv_sec;", "int tv_sec;",
         ("time.h",), True, r"static assertion failed.*timespec"),
        ("time-signedness", "sys/_time_t.h", "typedef long long time_t;",
         "typedef unsigned long long time_t;", ("time.h",), True,
         r"static assertion failed.*time scalar"),
        ("timeval-signedness", "sys/_timeval.h", "long    tv_sec;", "unsigned long tv_sec;",
         ("sys/select.h",), False, r"static assertion failed.*timeval seconds"),
        ("microsecond-signedness", "sys/_suseconds_t.h", "typedef long suseconds_t;",
         "typedef unsigned long suseconds_t;", ("sys/types.h",), True,
         r"static assertion failed.*microsecond scalar"),
        ("signal-signedness", "sys/_sigset_t.h", "typedef unsigned long sigset_t;",
         "typedef long sigset_t;", ("signal.h",), True,
         r"static assertion failed.*signal scalar"),
        ("timespec-guard", "sys/_timespec.h", "#ifndef _SYS__TIMESPEC_H_", "#if 1",
         PUBLIC, True, r"redefinition.*timespec"),
    ]
    for label, header, before, after, includes, wide, diagnostic in specifications:
        source = (root / "include" / header).read_text()
        require(source.count(before) == 1, "mutation anchor differs: " + label)
        yield label, header, source.replace(before, after), includes, wide, diagnostic


def check_linkage(compiler, flags, source, output, nm, label):
    case = command_case([*compiler, *flags, "-c", str(source), "-o", str(output)], label)
    symbols = subprocess.check_output([nm, "-u", str(output)], text=True).splitlines()
    names = sorted(row.split()[-1] for row in symbols if row.split())
    wide = "-D_TIME_BITS=64" in flags and "-D__SMALLER_C__=0x0100" not in flags
    require(names == (["__select64", "__time64"] if wide else ["select", "time"]),
            "time linkage differs: " + str(names))
    case.update(undefined_symbols=names, object_sha256=file_digest(output))
    return case


def width_rejection(result):
    require(result.returncode == 1 and not result.stderr and
            re.fullmatch(r'Error in "bad-width.i" \(1:[0-9]+\)\n'
                         r'Array dimension less than 1\n', result.stdout),
            "Smaller C bad-width rejection differs: " + result.stdout + result.stderr)


def width_controls():
    good = 'Error in "bad-width.i" (1:56)\nArray dimension less than 1\n'
    width_rejection(subprocess.CompletedProcess([], 1, good, ""))
    controls = []
    for label, code, stdout, stderr in (
            ("false-success", 0, good, ""), ("signal", -11, good, ""),
            ("stderr", 1, good, "unexpected diagnostic"),
            ("other-file", 1, good.replace("bad-width.i", "other.i"), ""),
            ("other-error", 1, "Array error\n", ""), ("empty", 1, "", "")):
        try:
            width_rejection(subprocess.CompletedProcess([], code, stdout, stderr))
        except ValueError:
            controls.append({"label": label, "state": "rejected"})
        else:
            raise ValueError("width diagnostic control accepts " + label)
    return controls


def run(root, host_cc):
    require(os.environ.get("PYTHON"), "PYTHON must explicitly select the interpreter")
    cross = shutil.which("arm-none-eabi-gcc")
    clang = shutil.which("clang")
    nm = shutil.which("arm-none-eabi-nm")
    assembler = shutil.which("arm-none-eabi-as")
    host = shlex.split(host_cc)
    require(cross and clang and nm and assembler and host and shutil.which(host[0]),
            "ARM GCC/binutils, Clang, and a host compiler are required")
    host[0] = shutil.which(host[0])
    before = source_snapshot(root)
    verifier_before = file_digest(Path(__file__))
    machine = root / "include/machine"
    require(machine.resolve() == (root / "sys/arch/rp2040/include").resolve(),
            "public machine headers do not select rp2040")
    common = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-nostdinc",
              "-I" + str(root / "include"), "-Wall", "-Wextra", "-Werror", "-fno-builtin"]
    profiles = {"gnu17": [cross, "-std=gnu17", *common],
                "c17": [cross, "-std=c17", "-pedantic-errors", *common],
                "clang.c17": [clang, "--target=arm-none-eabi", "-std=c17",
                              "-pedantic-errors", *common]}
    cases, native = [], []
    with tempfile.TemporaryDirectory(prefix="public-time-types-") as temporary:
        work = Path(temporary)
        source = work / "consumer.c"
        for profile, arguments in profiles.items():
            for mode, selected in MODES.items():
                for width in (32, 64):
                    flags = [*selected, f"-D_TIME_BITS={width}"]
                    for form, headers in FORMS.items():
                        source.write_text(fixture(headers, width == 64))
                        case = compile_case([*arguments, *flags], source,
                                            f"{profile}.{mode}.{width}.{form}")
                        require(not case["diagnostics"], "valid header emits diagnostics")
                        cases.append(case)
                    source.write_text(fixture(PUBLIC, width == 64))
                    cases.append(check_linkage(arguments, flags, source, work / "aliases.o",
                                               nm, f"{profile}.{mode}.{width}.aliases"))
            for width in (32, 64):
                source.write_text(fixture(("sys/time.h",), False, kernel=True))
                cases.append(compile_case([*arguments, "-DKERNEL", f"-D_TIME_BITS={width}"],
                                          source, f"{profile}.kernel.{width}"))
            for label, header, content, includes, wide, diagnostic in mutations(root):
                overlay = work / (profile + "-" + label)
                (overlay / header).parent.mkdir(parents=True)
                (overlay / header).write_text(content)
                source.write_text(fixture(includes, wide))
                cases.append(compile_case([arguments[0], "-I" + str(overlay), *arguments[1:],
                                          f"-D_TIME_BITS={64 if wide else 32}"], source,
                                          profile + "." + label, diagnostic))
            source.write_text("int invalid( {\n")
            cases.append(compile_case(arguments, source, profile + ".invalid", r"error:"))
        compiler = work / "smlrc"
        native.append(command_case([*host, "-O1", "-Wall", "-Wextra", "-Werror", "-DTHUMB",
            "-DNO_ANNOTATIONS", "-DNO_PPACK", "-DSTATIC", "-DNO_EXTRA_WARNS",
            "-DSYNTAX_STACK_MAX=3200", "-o", str(compiler),
            str(root / "usr.bin/smlrc/smlrc.c")], "smaller.build"))
        for form, headers in FORMS.items():
            for mode, selected in MODES.items():
                for width in (32, 64):
                    label = f"smaller.{form}.{mode}.{width}"
                    directory = work / label.replace("/", "_")
                    directory.mkdir()
                    (directory / "input.c").write_text(fixture(headers, False, smaller=True))
                    native.append(command_case([cross, "-E", "-P", "-nostdinc",
                        "-I" + str(root / "include"), "-U__GNUC__", "-U__PCC__",
                        "-D__SMALLER_C__=0x0100", "-D__SMALLER_C_32__", "-D__SMALLER_C_UCHAR__",
                        "-D__arm__", "-D__unix__", "-D__BSD__", "-D__DISCOBSD__",
                        *selected, f"-D_TIME_BITS={width}", "input.c", "-o", "input.i"],
                        label + ".preprocess", cwd=directory))
                    native.append(command_case([str(compiler), "input.i", "output.s"],
                                              label + ".compile", cwd=directory))
                    native.append(command_case([assembler, "-mcpu=cortex-m0plus", "-o",
                                               "output.o", "output.s"],
                                              label + ".assemble", cwd=directory))
                    require((directory / "output.o").stat().st_size > 0,
                            "empty native-input object")
        (work / "invalid.i").write_text("int invalid( {\n")
        rejected = subprocess.run([str(compiler), "invalid.i", "invalid.s"], cwd=work,
                                  capture_output=True, text=True, check=False)
        syntax_rejection(rejected)
        (work / "bad-width.i").write_text(
            "typedef char rejected_width[sizeof(long) == 8 ? 1 : -1];\n"
            "int main(void) { return 0; }\n")
        rejected = subprocess.run([str(compiler), "bad-width.i", "bad-width.s"], cwd=work,
                                  capture_output=True, text=True, check=False)
        width_rejection(rejected)
    after = source_snapshot(root)
    for field in ("commit", "tracked_files", "index_entries"):
        require(before[field] == after[field], "tracked input state changes during type matrix")
    require(file_digest(Path(__file__)) == verifier_before, "type verifier changes during scan")
    require(machine.resolve() == (root / "sys/arch/rp2040/include").resolve(),
            "public machine header selection changes during scan")
    return {"schema_version": 1, "evidence_class": "compiler-and-object-acceptance",
            "source_headers": {name: file_digest(root / "include" / name)
                               for name in (*PUBLIC, "sys/types.h", *OWNERS)},
            "verifier_sha256": verifier_before, "cases": cases,
            "source_commit": before["commit"],
            "tracked_input_state_sha256": hashlib.sha256(json.dumps(
                {field: before[field] for field in ("commit", "tracked_files", "index_entries")},
                sort_keys=True).encode()).hexdigest(),
            "diagnostic_controls": diagnostic_controls(), "smaller_cases": native,
            "smaller_syntax_controls": syntax_controls(),
            "smaller_width_controls": width_controls(),
            "smaller_invalid_rejected": True,
            "smaller_bad_width_rejected": True,
            "native_time_policy": "32-bit time_t under both _TIME_BITS selectors",
            "tools": {"arm": tool_identity(cross), "clang": tool_identity(clang),
                      "host": tool_identity(host[0]), "nm": tool_identity(nm)},
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
        print("PASS public time types: standalone/reordered layouts, linkage, and native input")
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("FAIL public time types: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
