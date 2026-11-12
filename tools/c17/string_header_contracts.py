"""Calibrate string-header namespaces, signatures, and compiler acceptance."""

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from passwd_header_contracts import (
    calibrate_profile_probe,
    compile_case,
    file_digest,
    probe_profile,
    require,
    tool_identity,
)

SIGNATURES = {
    "iso": {
        "strcat": "char *(*)(char *, const char *)",
        "strncat": "char *(*)(char *, const char *, size_t)",
        "strcpy": "char *(*)(char *, const char *)",
        "strncpy": "char *(*)(char *, const char *, size_t)",
        "strstr": "char *(*)(const char *, const char *)",
        "strcmp": "int (*)(const char *, const char *)",
        "strncmp": "int (*)(const char *, const char *, size_t)",
        "strlen": "size_t (*)(const char *)",
        "memcmp": "int (*)(const void *, const void *, size_t)",
        "memmove": "void *(*)(void *, const void *, size_t)",
        "memchr": "void *(*)(const void *, int, size_t)",
        "memcpy": "void *(*)(void *, const void *, size_t)",
        "memset": "void *(*)(void *, int, size_t)",
        "strchr": "char *(*)(const char *, int)",
        "strpbrk": "char *(*)(const char *, const char *)",
        "strrchr": "char *(*)(const char *, int)",
        "strtok": "char *(*)(char *, const char *)",
        "strcspn": "size_t (*)(const char *, const char *)",
        "strspn": "size_t (*)(const char *, const char *)",
        "strerror": "char *(*)(int)",
    },
    "duplicate": {"strdup": "char *(*)(const char *)"},
    "token": {"strtok_r": "char *(*)(char *, const char *, char **)"},
    "xsi": {"memccpy": "void *(*)(void *, const void *, int, size_t)"},
    "issue8": {
        "memmem": "void *(*)(const void *, size_t, const void *, size_t)",
        "strlcpy": "size_t (*)(char *, const char *, size_t)",
        "strlcat": "size_t (*)(char *, const char *, size_t)",
    },
    "bsd": {
        "explicit_bzero": "void (*)(void *, size_t)",
        "strsep": "char *(*)(char **, const char *)",
        "timingsafe_bcmp": "int (*)(const void *, const void *, size_t)",
        "syserrlst": "const char *(*)(int)",
    },
}

# Expected groups are independent of the implementation's visibility arithmetic.
MODES = [
    ("default", {}, ("iso", "duplicate", "token", "xsi", "issue8", "bsd")),
    ("iso", {"_ANSI_SOURCE": 1}, ("iso",)),
    ("legacy-posix", {"_POSIX_SOURCE": 1}, ("iso",)),
    ("posix1995", {"_POSIX_C_SOURCE": 199506}, ("iso", "token")),
    ("posix2001", {"_POSIX_C_SOURCE": 200112}, ("iso", "token")),
    ("posix2008", {"_POSIX_C_SOURCE": 200809}, ("iso", "duplicate", "token")),
    ("posix2024", {"_POSIX_C_SOURCE": 202405},
     ("iso", "duplicate", "token", "issue8")),
    ("xopen500", {"_XOPEN_SOURCE": 500}, ("iso", "duplicate", "token", "xsi")),
    ("xopen600", {"_XOPEN_SOURCE": 600}, ("iso", "duplicate", "token", "xsi")),
    ("xopen700", {"_XOPEN_SOURCE": 700}, ("iso", "duplicate", "token", "xsi")),
    ("xopen800", {"_XOPEN_SOURCE": 800},
     ("iso", "duplicate", "token", "xsi", "issue8")),
    ("new-posix-old-xopen", {"_POSIX_C_SOURCE": 202405, "_XOPEN_SOURCE": 700},
     ("iso", "duplicate", "token", "xsi", "issue8")),
    ("old-posix-new-xopen", {"_POSIX_C_SOURCE": 200809, "_XOPEN_SOURCE": 800},
     ("iso", "duplicate", "token", "xsi", "issue8")),
    ("modern-over-ansi", {"_ANSI_SOURCE": 1, "_POSIX_C_SOURCE": 200809},
     ("iso", "duplicate", "token")),
    ("xopen-over-legacy", {"_POSIX_SOURCE": 1, "_XOPEN_SOURCE": 800},
     ("iso", "duplicate", "token", "xsi", "issue8")),
    ("ansi-over-legacy", {"_ANSI_SOURCE": 1, "_POSIX_SOURCE": 1}, ("iso",)),
    ("bsd-additive", {"_BSD_SOURCE": 1, "_POSIX_C_SOURCE": 200809},
     ("iso", "duplicate", "token", "xsi", "issue8", "bsd")),
    ("default-additive", {"_DEFAULT_SOURCE": 1, "_ANSI_SOURCE": 1},
     ("iso", "duplicate", "token", "xsi", "issue8", "bsd")),
]


def fixture(selectors, visible, form="standalone", smaller=False):
    lines = [f"#define {name} {value}L" for name, value in selectors.items()]
    peers = ["stddef.h", "stdlib.h", "stdio.h", "strings.h"]
    if form == "peers-before":
        lines.extend(f"#include <{name}>" for name in peers)
    lines.append("#include <string.h>")
    if form == "repeated":
        # Duplicate compatible prototypes compile: poison a name to detect reparsing.
        lines.extend(["#define strlen 123", "#include <string.h>", "#undef strlen"])
    if form == "peers-after":
        lines.extend(f"#include <{name}>" for name in reversed(peers))
    lines.extend(["#ifndef NULL", '#error "missing NULL"', "#endif"])
    for name, value in selectors.items():
        lines.extend([f"#if {name} != {value}L",
                      '#error "caller feature selector changes"', "#endif"])
    for group, signatures in SIGNATURES.items():
        for name, signature in signatures.items():
            if group in visible:
                if smaller:
                    declaration = signature.replace("(*)", f"(*contract_{name})")
                    lines.append(f"{declaration} = {name};")
                else:
                    lines.append(f'_Static_assert(_Generic(&{name}, {signature}: 1, '
                                 f'default: 0), "{name} signature");')
            elif form in {"standalone", "repeated"}:
                lines.append(f"int {name};")
    if not smaller:
        lines.append('_Static_assert(sizeof(size_t) == 4, "target size_t");')
    lines.append("int main(void) { return 0; }")
    return "\n".join(lines) + "\n"


def command_case(command, label, cwd=None, reject=False):
    observed = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                              check=False, env=dict(os.environ, LC_ALL="C"))
    require((observed.returncode != 0) if reject else (observed.returncode == 0),
            label + " verdict differs:\n" + observed.stdout + observed.stderr)
    require(reject or not observed.stderr, label + " produces diagnostics: " + observed.stderr)
    return {"label": label, "argv": command, "exit_code": observed.returncode,
            "stdout": observed.stdout, "stderr": observed.stderr,
            "expected_rejection": reject, "state": "passed"}


def smaller_cases(root, workspace, host, cross):
    compiler = workspace / "smlrc"
    cases = [command_case([*host, "-O1", "-Wall", "-Wextra", "-Werror", "-DTHUMB",
                           "-DNO_ANNOTATIONS", "-DNO_PPACK", "-DSTATIC", "-DNO_EXTRA_WARNS",
                           "-DSYNTAX_STACK_MAX=3200", "-o", str(compiler),
                           str(root / "usr.bin/smlrc/smlrc.c")], "smaller.build")]
    assembler = str(Path(cross).with_name(Path(cross).name.replace("gcc", "as")))
    require(Path(assembler).is_file(), "target assembler is absent")
    identities = {"compiler_source": file_digest(root / "usr.bin/smlrc/smlrc.c"),
                  "backend_source": file_digest(root / "usr.bin/smlrc/cgthumb.c"),
                  "assembler": tool_identity(assembler)}
    for label, selectors, visible in MODES:
        source = workspace / "input.c"
        source.write_text(fixture(selectors, visible, smaller=True))
        cases.append(command_case(
            [cross, "-E", "-P", "-nostdinc", "-I" + str(root / "include"),
             "-U__GNUC__", "-U__PCC__", "-D__SMALLER_C__=0x0100", "-D__SMALLER_C_32__",
             "-D__SMALLER_C_UCHAR__", "-D__arm__", "-D__unix__", "-D__BSD__",
             "-D__DISCOBSD__", str(source), "-o", str(workspace / "input.i")],
            "smaller.preprocess." + label))
        cases[-1]["source"] = source.read_text()
        cases[-1]["expanded_sha256"] = file_digest(workspace / "input.i")
        cases.append(command_case([str(compiler), "input.i", "output.s"],
                                  "smaller.compile." + label, cwd=workspace))
        cases.append(command_case([assembler, "-mcpu=cortex-m0plus", "-o", "output.o",
                                   "output.s"], "smaller.assemble." + label, cwd=workspace))
    (workspace / "invalid.i").write_text("int broken( {\n")
    cases.append(command_case([str(compiler), "invalid.i", "invalid.s"],
                              "smaller.invalid-input", cwd=workspace, reject=True))
    return {"evidence_class": "expanded-header-compiler-and-assembler-acceptance",
            "identities": identities, "cases": cases,
            "on_device_preprocessing": "unrun", "runtime_execution": "unrun"}


def hosted_cdefs_cases(root, workspace, host, clang):
    cases = []
    identities = {}
    selectors = ["_ANSI_SOURCE", "_POSIX_SOURCE", "_POSIX_C_SOURCE", "_XOPEN_SOURCE",
                 "_BSD_SOURCE", "_DEFAULT_SOURCE"]
    for profile, compiler in [("host", host), ("clang", [clang])]:
        arguments = [*compiler, "-std=c17", "-pedantic-errors", "-Wall", "-Wextra",
                     "-Werror", "-fno-builtin", "-idirafter", str(root / "include")]
        for label, selected, visible in MODES:
            if label not in {"default", "iso", "posix2008", "posix2024",
                             "xopen700", "xopen800"}:
                continue
            for order in ["before", "after"]:
                source = workspace / "hosted.c"
                body = fixture(selected, visible).replace(
                    "#include <string.h>", f'#include "{root / "include/string.h"}"')
                if order == "before":
                    # Host features may choose defaults.  Reset only public selectors
                    # so the target's namespace test has explicit, independent input.
                    body = "#include <sys/cdefs.h>\n" + "".join(
                        f"#undef {name}\n" for name in selectors) + body
                else:
                    body += "#include <sys/cdefs.h>\n"
                source.write_text(body)
                cases.append(compile_case(arguments, source, f"{profile}.cdefs.{order}.{label}"))
                dependencies = subprocess.check_output(
                    [*arguments, "-M", str(source)], text=True,
                    env=dict(os.environ, LC_ALL="C"))
                paths = [Path(name) for name in shlex.split(dependencies.replace("\\\n", ""))
                         if name.endswith("/sys/cdefs.h")]
                require(len(paths) == 1 and paths[0].resolve() !=
                        (root / "sys/sys/cdefs.h").resolve(),
                        "hosted cdefs fixture does not select exactly one host header")
                identities[str(paths[0])] = file_digest(paths[0])
    require(len(cases) == 24, "hosted cdefs denominator differs")
    return {"cases": cases, "host_cdefs": identities}


def run(root, host_compiler):
    host = shlex.split(host_compiler)
    require(host and shutil.which(host[0]), "host compiler is unavailable")
    host[0] = shutil.which(host[0])
    clang = shutil.which("clang")
    require(clang is not None, "Clang is required")
    strict = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
              "-Wstrict-prototypes", "-Wold-style-definition", "-fno-builtin",
              "-nostdinc", "-I" + str(root / "include")]
    environment = dict(os.environ, LC_ALL="C")
    environment.pop("C17_CAPTURE_DIR", None)
    production = shlex.split(subprocess.check_output(
        ["bmake", "-C", str(root / "lib/libc"), "MACHINE=rp2040", "-V",
         "${CC} ${CFLAGS} ${CPPFLAGS}"], env=environment, text=True))
    require(all(flag in production for flag in ["-std=gnu17", "-mcpu=cortex-m0plus",
                                                "-mthumb", "-mfloat-abi=soft",
                                                "-Wall", "-Wextra", "-Werror"]),
            "evaluated libc compiler lacks the required production profile")
    profiles = [("host.native", [*host, *strict], False),
                ("host.ilp32", [*host, "-m32", *strict], True),
                ("clang.native", [clang, *strict], False),
                ("clang.ilp32", [clang, "-m32", *strict], True),
                ("clang.arm.c17", [clang, *strict, "--target=arm-none-eabi",
                                   "-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft",
                                   "-mabi=aapcs", "-mlittle-endian", "-ffreestanding"], False),
                ("arm.gnu17", [*production, "-fno-builtin"], False),
                ("arm.c17", [*production, "-fno-builtin", "-std=c17", "-pedantic-errors",
                              "-Wstrict-prototypes", "-Wold-style-definition"], False)]
    cases = []
    states = {}
    header = root / "include/string.h"
    original = header.read_text()
    with tempfile.TemporaryDirectory(prefix="string-header-") as temporary:
        workspace = Path(temporary)
        probe = workspace / "probe.c"
        probe.write_text("int probe(void) { return 0; }\n")
        calibration = calibrate_profile_probe(clang, strict, workspace)
        for profile, arguments, optional in profiles:
            states[profile] = probe_profile(arguments, probe, optional)
            if states[profile]["state"] == "unrun":
                continue
            for label, selectors, visible in MODES:
                for form in ["standalone", "repeated", "peers-before", "peers-after"]:
                    source = workspace / "consumer.c"
                    source.write_text(fixture(selectors, visible, form))
                    cases.append(compile_case(arguments, source, f"{profile}.{label}.{form}"))
            mutations = [
                ("bsd-leak", original + "\nchar *strsep(char **, const char *);\n", "iso",
                 "standalone", r"(?:strsep.*redeclared|redefinition of .strsep)"),
                ("iso-hidden", original.replace("strlen", "hidden_strlen"), "iso",
                 "standalone", r"(?:strlen.*undeclared|undeclared identifier .strlen)"),
                ("xsi-leak", original + "\nvoid *memccpy(void *, const void *, int, size_t);\n",
                 "posix2024", "standalone", r"(?:memccpy.*redeclared|redefinition of .memccpy)"),
                ("issue8-leak", original + "\nsize_t strlcpy(char *, const char *, size_t);\n",
                 "posix2008", "standalone", r"(?:strlcpy.*redeclared|redefinition of .strlcpy)"),
                ("default-hidden", "#define _ANSI_SOURCE 1\n" + original, "default",
                 "standalone", r"(?:strdup.*undeclared|undeclared identifier .strdup)"),
                ("wrong-signature", original.replace("const char\t*syserrlst(int);",
                                                     "char *syserrlst(int);"), "default",
                 "standalone", r"static assertion failed.*syserrlst signature"),
                ("guard-disabled", original.replace("#ifndef _STRING_H_", "#if 1"),
                 "default", "repeated", r"expected.*(?:identifier|unqualified-id)"),
            ]
            for label, content, mode, form, diagnostic in mutations:
                require(content != original, label + " mutation is inert")
                mutated = workspace / label
                mutated.mkdir(exist_ok=True)
                (mutated / "string.h").write_text(content)
                _, selectors, visible = next(item for item in MODES if item[0] == mode)
                source = mutated / "consumer.c"
                source.write_text(fixture(selectors, visible, form))
                cases.append(compile_case([arguments[0], "-I" + str(mutated), *arguments[1:]],
                                          source, profile + "." + label, diagnostic))
        hosted = hosted_cdefs_cases(root, workspace, host, clang)
        smaller = smaller_cases(root, workspace, host, production[0])
    available = sum(state["state"] != "unrun" for state in states.values())
    require(len(cases) == available * (len(MODES) * 4 + 7),
            "string-header compiler denominator differs")
    return {"schema_version": 1, "evidence_class": "compiler-acceptance",
            "sources": {str(path.relative_to(root)): file_digest(path) for path in
                        [header, root / "include/_featuretest.h", Path(__file__).resolve()]},
            "tools": {"host": tool_identity(host[0]), "clang": tool_identity(clang),
                      "arm": tool_identity(production[0])},
            "profiles": states, "cases": cases, "profile_calibration": calibration,
            "hosted_cdefs": hosted, "smaller_c": smaller, "hardware": "unrun"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path)
    parser.add_argument("--host-cc", default=os.environ.get("HOST_CC", "cc"))
    arguments = parser.parse_args()
    try:
        receipt = run(arguments.root.resolve(), arguments.host_cc)
        if arguments.output:
            arguments.output.write_text(json.dumps(receipt, indent=2) + "\n")
        print(f"PASS string header contracts: {len(receipt['cases'])} compiler cases; "
              f"{len(receipt['hosted_cdefs']['cases'])} hosted cdefs cases; "
              f"{len(receipt['smaller_c']['cases'])} Smaller C build/input/assembly controls")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("FAIL string header contracts: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
