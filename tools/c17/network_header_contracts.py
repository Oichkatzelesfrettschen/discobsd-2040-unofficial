"""Calibrate network-header inclusion, legacy signatures and ARM layout."""

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch

from passwd_header_contracts import compile_case, file_digest, require, tool_identity
from record_header_contracts import MODES, syntax_controls, syntax_rejection
from string_header_contracts import command_case

HEADERS = ("netinet/in.h", "arpa/inet.h")
FORMS = {
    **{f"{header}.{form}":
       ("#include <sys/types.h>\n" if form == "types-before" else "") +
       f"#include <{header}>\n" * (2 if form == "repeated" else 1) +
       ("#include <sys/types.h>\n" if form == "types-after" else "")
       for header in HEADERS
       for form in ("standalone", "repeated", "types-before", "types-after")},
    "in-inet": "#include <netinet/in.h>\n#include <arpa/inet.h>\n",
    "inet-in": "#include <arpa/inet.h>\n#include <netinet/in.h>\n",
    "in-inet-repeated": "#include <netinet/in.h>\n#include <arpa/inet.h>\n" * 2,
    "inet-in-repeated": "#include <arpa/inet.h>\n#include <netinet/in.h>\n" * 2,
}
SIGNATURES = {
    "inet_addr": "unsigned long (*)(char *)",
    "inet_ntoa": "char *(*)(struct in_addr)",
    "inet_makeaddr": "struct in_addr (*)(long, long)",
    "inet_network": "unsigned long (*)(char *)",
    "inet_netof": "unsigned long (*)(struct in_addr)",
    "inet_lnaof": "unsigned long (*)(struct in_addr)",
    **{name: "unsigned (*)(unsigned)" for name in ("htonl", "htons", "ntohl", "ntohs")},
}


def fixture(includes, smaller=False):
    lines = [includes]
    if smaller:
        lines += ["typedef char address_size[sizeof(struct in_addr) == 4 ? 1 : -1];",
                  "typedef char socket_size[sizeof(struct sockaddr_in) == 16 ? 1 : -1];"]
    else:
        lines.append("#include <stddef.h>")
        conditions = {
            "ARM width": "sizeof(long) == 4 && sizeof(void *) == 4",
            "in_addr size": "sizeof(struct in_addr) == 4",
            "in_addr alignment": "_Alignof(struct in_addr) == 4",
            "sockaddr_in size": "sizeof(struct sockaddr_in) == 16",
            "sockaddr_in alignment": "_Alignof(struct sockaddr_in) == 4",
            "s_addr type": ("_Generic(&((struct in_addr *)0)->s_addr, "
                            "unsigned long *: 1, default: 0)"),
        }
        for name, offset, field_type in (("sin_family", 0, "short *"),
                                         ("sin_port", 2, "unsigned short *"),
                                         ("sin_addr", 4, "struct in_addr *"),
                                         ("sin_zero", 8, "char (*)[8]")):
            conditions[name + " offset"] = f"offsetof(struct sockaddr_in, {name}) == {offset}"
            conditions[name + " type"] = (
                f"_Generic(&((struct sockaddr_in *)0)->{name}, {field_type}: 1, default: 0)")
        lines += [f'_Static_assert({value}, "{name}");' for name, value in conditions.items()]
    if "arpa/inet.h" in includes:
        for name, signature in SIGNATURES.items():
            if smaller:
                # Parentheses bypass function-like macros while preserving their definitions.
                lines.append(signature.replace("(*)", f"(*contract_{name})") + f" = ({name});")
            else:
                lines.append(f'_Static_assert(_Generic(&({name}), {signature}: 1, default: 0), '
                             f'"{name} signature");')
        for name, value, expected in (("htonl", "0x12345678U", "0x78563412U"),
                                      ("ntohl", "0x12345678U", "0x78563412U"),
                                      ("htons", "0x1234U", "0x3412U"),
                                      ("ntohs", "0x1234U", "0x3412U")):
            lines += [f"#ifndef {name}", f'#error "missing {name} macro"', "#endif"]
            if not smaller:
                lines.append(f'_Static_assert({name}({value}) == {expected}, "{name} constant");')
        lines.append("unsigned contract_swap(unsigned value) { return htonl(value) + "
                     "htons(value) + ntohl(value) + ntohs(value); }")
    lines.append("int main(void) { return 0; }")
    return "\n".join(lines) + "\n"


def mutations(root):
    specifications = [
        ("in-guard", "netinet/in.h", "#ifndef _NETINET_IN_H_", "#if 1",
         "netinet/in.h.repeated", r"redefinition.*in_addr"),
        ("inet-guard", "arpa/inet.h", "#ifndef _ARPA_INET_H_", "#if 1",
         "arpa/inet.h.repeated",
         r"expected (?:identifier|declaration specifiers|parameter declarator)"),
        ("in-prerequisite", "netinet/in.h", "#include <sys/types.h>", "",
         "netinet/in.h.standalone", r"unknown type name ['‘]u_long"),
        ("inet-prerequisite", "arpa/inet.h", "#include <netinet/in.h>", "",
         "arpa/inet.h.standalone",
         r"(?:incomplete type|incomplete definition|declared inside parameter)"),
        ("address-type", "netinet/in.h", "u_long s_addr;", "long s_addr;",
         "netinet/in.h.standalone", r"static assertion failed.*s_addr type"),
        ("socket-layout", "netinet/in.h", "char    sin_zero[8];", "char    sin_zero[9];",
         "netinet/in.h.standalone", r"static assertion failed.*sockaddr_in size"),
    ]
    for label, header, before, after, form, diagnostic in specifications:
        original = (root / "include" / header).read_text()
        require(original.count(before) == 1, label + " mutation anchor differs")
        yield label, header, original.replace(before, after), form, diagnostic


def diagnostic_controls():
    controls = []
    with tempfile.TemporaryDirectory(prefix="network-diagnostics-") as directory:
        source = Path(directory) / "control.c"
        source.write_text("int control;\n")
        for label, code, diagnostic in (("empty", 1, ""), ("unrelated", 1, "unrelated error"),
                                        ("false-success", 0, "error: unknown type name"),
                                        ("signal", -11, "error: unknown type name")):
            # Calibrate the shared diagnostic matcher with synthetic process results.
            result = subprocess.CompletedProcess([], code, "", diagnostic)
            with patch("passwd_header_contracts.subprocess.run", return_value=result):
                try:
                    compile_case(["diagnostic-control"], source, label,
                                 r"error: unknown type name")
                except ValueError:
                    pass
                else:
                    raise ValueError("diagnostic matcher accepts " + label)
            controls.append({"label": label, "state": "rejected",
                             "evidence_class": "diagnostic-fixture"})
    return controls


def smaller_cases(root, workspace, host, cross):
    compiler = workspace / "smlrc"
    cases = [command_case([*host, "-O1", "-Wall", "-Wextra", "-Werror", "-DTHUMB",
                           "-DNO_ANNOTATIONS", "-DNO_PPACK", "-DSTATIC", "-DNO_EXTRA_WARNS",
                           "-DSYNTAX_STACK_MAX=3200", "-o", str(compiler),
                           str(root / "usr.bin/smlrc/smlrc.c")], "smaller.build")]
    assembler = str(Path(cross).with_name(Path(cross).name.replace("gcc", "as")))
    require(Path(assembler).is_file(), "ARM assembler is absent")
    for form, includes in FORMS.items():
        for mode, flags in MODES.items():
            label = f"smaller.{form}.{mode}"
            case_workspace = workspace / label
            case_workspace.mkdir(parents=True)
            source = case_workspace / "input.c"
            source.write_text(fixture(includes, smaller=True))
            cases.append(command_case(
                [cross, "-E", "-P", "-nostdinc", "-I" + str(root / "include"),
                 "-U__GNUC__", "-U__PCC__", "-D__SMALLER_C__=0x0100", "-D__SMALLER_C_32__",
                 "-D__SMALLER_C_UCHAR__", "-D__arm__", "-D__unix__", "-D__BSD__",
                 "-D__DISCOBSD__", *flags, "input.c", "-o", "input.i"],
                label + ".preprocess", cwd=case_workspace))
            cases[-1].update(source=source.read_text(),
                             expanded_sha256=file_digest(case_workspace / "input.i"))
            cases.append(command_case([str(compiler), "input.i", "output.s"],
                                      label + ".compile", cwd=case_workspace))
            require((case_workspace / "output.s").stat().st_size > 0, "empty Smaller C assembly")
            cases.append(command_case([assembler, "-mcpu=cortex-m0plus", "-o", "output.o",
                                       "output.s"], label + ".assemble", cwd=case_workspace))
            require((case_workspace / "output.o").stat().st_size > 0, "empty ARM object")
    (workspace / "invalid.i").write_text("int broken( {\n")
    command = [str(compiler), "invalid.i", "invalid.s"]
    invalid = subprocess.run(command, cwd=workspace, capture_output=True, text=True,
                             check=False, env=dict(os.environ, LC_ALL="C"))
    syntax_rejection(invalid)
    cases.append({"label": "smaller.invalid", "argv": command, "exit_code": invalid.returncode,
                  "stdout": invalid.stdout, "stderr": invalid.stderr, "state": "rejected"})
    require(len(cases) == 146, "Smaller C case denominator differs")
    return {"cases": cases, "syntax_controls": syntax_controls(),
            "compiler_sha256": file_digest(compiler), "assembler": tool_identity(assembler),
            "sources": {name: file_digest(root / "usr.bin/smlrc" / name)
                        for name in ("smlrc.c", "cgthumb.c")},
            "scope": "48 cross-preprocessed inputs compile and assemble; native runtime unrun"}


def run(root, host_cc):
    require(os.environ.get("PYTHON"), "PYTHON must explicitly select the interpreter")
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
    with tempfile.TemporaryDirectory(prefix="network-headers-") as directory:
        workspace = Path(directory)
        source = workspace / "consumer.c"
        for profile, arguments in profiles.items():
            for form, includes in FORMS.items():
                source.write_text(fixture(includes))
                for mode, flags in MODES.items():
                    cases.append(compile_case([*arguments, *flags], source,
                                              f"{profile}.{form}.{mode}"))
                    require(not cases[-1]["diagnostics"], "valid consumer produces diagnostics")
            for label, header, content, form, diagnostic in mutations(root):
                mutated = workspace / label
                (mutated / header).parent.mkdir(parents=True, exist_ok=True)
                (mutated / header).write_text(content)
                source.write_text(fixture(FORMS[form]))
                cases.append(compile_case([arguments[0], "-I" + str(mutated), *arguments[1:]],
                                          source, profile + "." + label, diagnostic))
        smaller = smaller_cases(root, workspace, host, production[0])
    require(len(cases) == 162, "network-header compiler denominator differs")
    return {"schema_version": 1, "evidence_class": "compiler-acceptance",
            "sources": {name: file_digest(root / "include" / name) for name in HEADERS},
            "prerequisites": {name: file_digest(root / "include" / name) for name in
                              ("sys/types.h", "sys/cdefs.h", "sys/endian.h", "machine/types.h")},
            "helpers": {name: file_digest(root / "tools/c17" / name) for name in
                        ("passwd_header_contracts.py", "record_header_contracts.py",
                         "string_header_contracts.py")},
            "verifier_sha256": file_digest(Path(__file__)), "profiles": profiles,
            "tools": {"arm": tool_identity(production[0]), "clang": tool_identity(clang),
                      "host": tool_identity(host[0])},
            "diagnostic_controls": diagnostic_controls(), "cases": cases,
            "smaller_c": smaller, "runtime": "unrun", "hardware": "unrun"}


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
        print("PASS network header contracts: 144 valid compiler cases, 18 rejections; "
              "48 Smaller C inputs compile and assemble")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("FAIL network header contracts: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
