"""Calibrate descriptor-set header contracts and getty initialization."""

import argparse
import hashlib
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
from record_header_contracts import MODES, syntax_rejection
from string_header_contracts import command_case

FORMS = {
    "standalone": "#include <sys/select.h>\n",
    "repeated": "#include <sys/select.h>\n" * 2,
    **{header + "." + order:
       (f"#include <{header}>\n#include <sys/select.h>\n" if order == "before" else
        f"#include <sys/select.h>\n#include <{header}>\n")
       for header in ("sys/types.h", "sys/time.h", "signal.h", "string.h")
       for order in ("before", "after")},
}
BODY = """
int main(void) {
    fd_set set;
    if (1) FD_ZERO(&set); else return 1;
    FD_SET(0, &set);
    FD_CLR(0, &set);
    return FD_ISSET(0, &set) != 0;
}
"""
CALLER_PREFIX = """#include <sys/select.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
int probe_ioctl(int, int, ...);
int probe_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
ssize_t probe_read(int, void *, size_t);
#define ioctl probe_ioctl
#define select probe_select
#define read probe_read
"""


def replace_once(text, before, after):
    require(text.count(before) == 1, "mutation anchor differs: " + before)
    return text.replace(before, after)


def caller_source(root):
    text = (root / "libexec/getty/subr.c").read_text()
    match = re.search(r"(?m)^char \*\nautobaud\([^\n]*\)\n\{", text)
    require(match is not None, "autobaud definition is absent")
    # autobaud is the final function; retaining its complete suffix preserves
    # the production control flow while typed hooks replace device access.
    return text[match.start():]


def headers(root, workspace, cross):
    clang = shutil.which("clang")
    require(clang is not None, "Clang is required")
    common = ["-mcpu=cortex-m0plus", "-mthumb", "-nostdinc",
              "-I" + str(root / "include"), "-Wall", "-Wextra", "-Werror", "-fno-builtin"]
    profiles = {"gnu17": [cross, "-std=gnu17", *common],
                "c17": [cross, "-std=c17", "-pedantic-errors", *common],
                "clang.c17": [clang, "--target=arm-none-eabi", "-std=c17",
                              "-pedantic-errors", *common]}
    assertions = ('_Static_assert(sizeof(fd_set) == 4, "set ABI");\n'
                  '_Static_assert(_Alignof(fd_set) == 4, "set alignment");\n'
                  '_Static_assert(FD_SETSIZE == 32, "descriptor capacity");\n')
    source = workspace / "header.c"
    cases = []
    for profile, arguments in profiles.items():
        for mode, flags in MODES.items():
            for width in (32, 64):
                for form, includes in FORMS.items():
                    source.write_text(includes + assertions + BODY)
                    cases.append(compile_case([*arguments, *flags, f"-D_TIME_BITS={width}"],
                                              source, f"{profile}.{mode}.{width}.{form}"))
                    require(not cases[-1]["diagnostics"], "valid consumer emits diagnostics")
        source.write_text("int broken( {\n")
        cases.append(compile_case(arguments, source, profile + ".invalid", r"error:"))
        source.write_text('#include <sys/select.h>\n'
                          '_Static_assert(sizeof(fd_set) == 8, "bad set ABI");\n')
        cases.append(compile_case(arguments, source, profile + ".bad-ABI",
                                  r"static assertion failed.*bad set ABI"))
    return {"cases": cases, "clang": tool_identity(clang)}


def smaller(root, workspace, cross, host):
    compiler = workspace / "smlrc"
    cases = [command_case([*host, "-O1", "-Wall", "-Wextra", "-Werror", "-DTHUMB",
                           "-DNO_ANNOTATIONS", "-DNO_PPACK", "-DSTATIC", "-DNO_EXTRA_WARNS",
                           "-DSYNTAX_STACK_MAX=3200", "-o", str(compiler),
                           str(root / "usr.bin/smlrc/smlrc.c")], "smaller.build")]
    assembler = str(Path(cross).with_name(Path(cross).name.replace("gcc", "as")))
    require(Path(assembler).is_file(), "ARM assembler is absent")
    for form, includes in FORMS.items():
        for mode, flags in MODES.items():
            for width in (32, 64):
                label = f"smaller.{form}.{mode}.{width}"
                work = workspace / label.replace("/", "_")
                work.mkdir()
                (work / "input.c").write_text(includes +
                    "typedef char set_abi[sizeof(fd_set) == 4 ? 1 : -1];\n" + BODY)
                cases.append(command_case([cross, "-E", "-P", "-nostdinc",
                    "-I" + str(root / "include"), "-U__GNUC__", "-U__PCC__",
                    "-D__SMALLER_C__=0x0100", "-D__SMALLER_C_32__", "-D__SMALLER_C_UCHAR__",
                    "-D__arm__", "-D__unix__", "-D__BSD__", "-D__DISCOBSD__",
                    *flags, f"-D_TIME_BITS={width}", "input.c", "-o", "input.i"],
                    label + ".preprocess", cwd=work))
                cases.append(command_case([str(compiler), "input.i", "output.s"],
                                          label + ".compile", cwd=work))
                cases.append(command_case([assembler, "-mcpu=cortex-m0plus", "-o", "output.o",
                                          "output.s"], label + ".assemble", cwd=work))
                require((work / "output.o").stat().st_size > 0, "empty Smaller C object")
    (workspace / "invalid.i").write_text("int broken( {\n")
    invalid = subprocess.run([str(compiler), "invalid.i", "invalid.s"], cwd=workspace,
                             capture_output=True, text=True, check=False)
    syntax_rejection(invalid)
    return {"cases": cases, "invalid_rejected": True,
            "compiler_sha256": file_digest(compiler), "assembler": tool_identity(assembler),
            "scope": "cross-preprocessed native input compiles and assembles; native run unrun"}


def runtime(root, workspace, compiler, arm, calibrate):
    original = caller_source(root)
    variants = {"good": original}
    if calibrate:
        variants.update({
            "uninitialized-set": replace_once(original, "\tFD_ZERO(&rfds);\n", ""),
            "wrong-baud": replace_once(original, 'type = "300-baud";', 'type = "4800-baud";'),
            "missing-flush": replace_once(original, "\tioctl(0, (int)TIOCFLUSH, &null);\n\tFD_ZERO",
                                          "\tFD_ZERO"),
        })
        variants["no-clear"] = original
    flags = ["-std=c17", "-pedantic-errors", "-Wstrict-prototypes", "-Wold-style-definition",
             "-Os", "-Wall", "-Wextra", "-Werror", "-fno-builtin",
             "-ftrivial-auto-var-init=pattern"]
    if arm:
        flags += ["-mcpu=cortex-m0plus", "-mthumb"]
    cases = []
    for label, content in variants.items():
        source = workspace / (label + ".c")
        source.write_text(CALLER_PREFIX + content)
        executable = workspace / label
        includes = ["-nostdinc", "-I" + str(root / "include")]
        if label == "no-clear":
            overlay = workspace / "no-clear-headers"
            (overlay / "sys").mkdir(parents=True)
            header = (root / "sys/sys/select.h").read_text()
            start = header.index("#else\n#define\tFD_ZERO")
            end = header.index("#endif", start)
            (overlay / "sys/select.h").write_text(
                header[:start] + "#else\n#define FD_ZERO(p) ((void)(p))\n" + header[end:])
            includes.insert(1, "-I" + str(overlay))
        objects = []
        for name, input_path in (("caller", source),
                                 ("fixture", root / "tests/libc_contracts/fd_set_test.c")):
            output = workspace / (label + "-" + name + ".o")
            cases.append(command_case([*compiler, *flags, *includes, "-c", str(input_path),
                                       "-o", str(output)], label + "." + name, cwd=workspace))
            objects.append(str(output))
        link = [*compiler, *flags, *objects]
        if arm:
            link += ["-nostartfiles", "-Wl,-Ttext=0x20000",
                     str(root / "tests/libbn/qemu_support.c"), "-lc", "-lgcc"]
        cases.append(command_case([*link, "-o", str(executable)], label + ".link", cwd=workspace))
        command = ([os.environ.get("QEMU_ARM", "qemu-arm"), "-cpu", "cortex-m0"] if arm else [])
        result = subprocess.run([*command, str(executable)], capture_output=True, text=True,
                                check=False, timeout=30)
        expected = 0 if label == "good" else 10 if label == "no-clear" else 20
        require(result.returncode == expected,
                f"{label}: runtime exit {result.returncode}, expected {expected}: {result.stderr}")
        cases.append({"label": label + ".execute", "exit_code": result.returncode,
                      "state": "accepted" if expected == 0 else "rejected",
                      "executable_sha256": file_digest(executable)})
    return {"cases": cases, "calibrated": calibrate,
            "caller_sha256": file_digest(root / "libexec/getty/subr.c"),
            "caller_excerpt_sha256": hashlib.sha256(original.encode()).hexdigest(),
            "automatic_storage": "compiler pattern initialization exposes omitted initialization"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("cross", "host", "qemu"))
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path)
    parser.add_argument("--no-calibration", action="store_true")
    args = parser.parse_args()
    try:
        require(os.environ.get("PYTHON"), "PYTHON must explicitly select the interpreter")
        root = args.root.resolve()
        cross = shutil.which("arm-none-eabi-gcc")
        host = shlex.split(os.environ.get("HOST_CC", "cc"))
        require(cross and host and shutil.which(host[0]), "ARM and host compilers are required")
        host[0] = shutil.which(host[0])
        with tempfile.TemporaryDirectory(prefix="fd-set-contracts-") as directory:
            workspace = Path(directory)
            if args.mode == "cross":
                result = {"headers": headers(root, workspace, cross),
                          "smaller_c": smaller(root, workspace, cross, host)}
            else:
                compiler = [cross] if args.mode == "qemu" else host
                result = runtime(root, workspace, compiler, args.mode == "qemu",
                                 not args.no_calibration)
        result.update(schema_version=1, mode=args.mode, hardware="unrun",
                      sources={name: file_digest(root / name) for name in
                               ("sys/sys/select.h", "libexec/getty/subr.c",
                                "tests/libc_contracts/fd_set_test.c",
                                "tools/c17/fd_set_contracts.py")},
                      compiler=tool_identity(cross if args.mode != "host" else host[0]))
        if args.output:
            args.output.write_text(json.dumps(result, indent=2) + "\n")
        print("PASS descriptor-set contracts: " + args.mode)
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("FAIL descriptor-set contracts: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
