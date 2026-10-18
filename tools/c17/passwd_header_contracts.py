"""Check password-header inclusion, public types and target layout."""

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


class HeaderContractError(ValueError):
    """A public-header contract or calibrated compiler verdict differs."""


def require(condition, message):
    if not condition:
        raise HeaderContractError(message)


def file_digest(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            result.update(chunk)
    return result.hexdigest()


def tool_identity(tool):
    return {"path": tool, "sha256": file_digest(Path(tool)),
            "version": subprocess.check_output([tool, "--version"], text=True,
                                               env=dict(os.environ, LC_ALL="C")).splitlines()[0]}


def profile_verdict(command, observed, optional):
    if observed.returncode == 0:
        return {"state": "available", "argv": command}
    unavailable = re.search(
        r"(?m)^[^\s:]+: error: (?:unsupported option|unrecognized command-line option|"
        r"unknown argument)[^\n]*-m32", observed.stderr)
    if optional and observed.returncode == 1 and unavailable:
        return {"state": "unrun", "argv": command, "reason": observed.stderr}
    raise HeaderContractError("compiler profile probe failed: " + observed.stderr)


def probe_profile(arguments, source, optional):
    command = [*arguments, "-fsyntax-only", str(source)]
    observed = subprocess.run(command, capture_output=True, text=True, check=False,
                              env=dict(os.environ, LC_ALL="C"))
    return profile_verdict(command, observed, optional)


def calibrate_profile_probe(clang, flags, workspace):
    good = workspace / "good-profile.c"
    bad = workspace / "bad-profile.c"
    good.write_text("int profile_probe(void) { return 0; }\n")
    bad.write_text("#error error: unsupported option '-m32' must reject\n")
    available = probe_profile([clang, *flags], good, True)
    diagnostic = subprocess.CompletedProcess(
        ["diagnostic-control"], 1, stdout="",
        stderr="clang: error: unsupported option '-m32' for target 'diagnostic-control'\n")
    unrun = profile_verdict(["diagnostic-control", "-m32"], diagnostic, True)
    unrun["evidence_class"] = "diagnostic-fixture"
    require(available["state"] == "available" and unrun["state"] == "unrun",
            "profile availability controls differ")
    rejections = []
    try:
        profile_verdict(["diagnostic-control", "-m32"], diagnostic, False)
    except HeaderContractError as error:
        rejections.append({"label": "mandatory-width", "state": "rejected", "reason": str(error)})
    else:
        raise HeaderContractError("profile policy accepted an unavailable mandatory width")
    try:
        probe_profile([clang, *flags], bad, True)
    except HeaderContractError as error:
        rejections.append({"label": "source-error", "state": "rejected", "reason": str(error)})
    else:
        raise HeaderContractError("profile policy accepted a source error as unavailable")
    return {"available": available, "unrun": unrun, "rejections": rejections}


def compile_case(arguments, source, label, expected=None):
    result = subprocess.run([*arguments, "-fsyntax-only", str(source)],
                            capture_output=True, text=True, check=False,
                              env=dict(os.environ, LC_ALL="C"))
    if expected is None:
        require(result.returncode == 0,
                label + " known-good compilation failed:\n" + result.stderr)
    else:
        require(result.returncode == 1 and re.search(expected, result.stderr, re.I | re.S),
                label + " failed its expected-rejection contract:\n" + result.stderr)
    return {"label": label, "argv": [*arguments, "-fsyntax-only", str(source)],
            "exit_code": result.returncode, "expected_rejection": expected,
            "source": source.read_text(), "source_sha256": file_digest(source),
            "diagnostics": result.stderr, "state": "passed"}


def assertions():
    lines = [
        '#include <stddef.h>',
        '_Static_assert(sizeof(int) == 4, "passwd integer width");',
        '_Static_assert(sizeof(void *) == 4 || sizeof(void *) == 8, "pointer model");',
        '_Static_assert(sizeof(struct passwd) == 5 * sizeof(void *) + 8, "passwd size");',
        '_Static_assert(_Alignof(struct passwd) == _Alignof(void *), "passwd alignment");',
        '_Static_assert(_PW_KEYBYNAME == \'0\' && _PW_KEYBYUID == \'1\', "password keys");',
    ]
    offsets = {"pw_name": "0", "pw_passwd": "sizeof(void *)", "pw_uid": "2*sizeof(void *)",
               "pw_gid": "2*sizeof(void *)+4", "pw_gecos": "2*sizeof(void *)+8",
               "pw_dir": "3*sizeof(void *)+8", "pw_shell": "4*sizeof(void *)+8"}
    for name, offset in offsets.items():
        field_type = "int *" if name in {"pw_uid", "pw_gid"} else "char **"
        lines.append(f'_Static_assert(offsetof(struct passwd, {name}) == '
                     f'{offset}, "passwd {name} offset");')
        lines.append(f'_Static_assert(_Generic(&((struct passwd *)0)->{name}, '
                     f'{field_type}: 1, default: 0), "passwd {name} type");')
    signatures = {
        "getpwent": "struct passwd *(*)(void)",
        "getpwuid": "struct passwd *(*)(int)",
        "getpwnam": "struct passwd *(*)(const char *)",
        "endpwent": "void (*)(void)",
        "setpwfile": "void (*)(const char *)",
        "setpwent": "int (*)(void)",
        "setpassent": "int (*)(int)",
    }
    for name, signature in signatures.items():
        lines.append(f'_Static_assert(_Generic(&{name}, {signature}: 1, default: 0), '
                     f'"passwd {name} signature");')
    return "\n".join(lines) + "\nint main(void) { return 0; }\n"


def run(root, host_compiler):
    strict = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
              "-Wstrict-prototypes", "-Wold-style-definition", "-nostdinc",
              "-I" + str(root / "include")]
    host = shlex.split(host_compiler)
    require(host, "host compiler command is empty")
    host_tool = shutil.which(host[0])
    clang = shutil.which("clang")
    require(host_tool is not None and clang is not None, "host compiler and Clang are required")
    host[0] = host_tool
    tools = {"host": tool_identity(host_tool), "clang": tool_identity(clang)}
    profiles = [("host.native", [*host, *strict], False),
                ("host.ilp32", [*host, "-m32", *strict], True),
                ("clang.native", [clang, *strict], False),
                ("clang.ilp32", [clang, "-m32", *strict], True),
                ("clang.arm.c17", [clang, *strict, "--target=arm-none-eabi",
                                   "-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft",
                                   "-mabi=aapcs", "-mlittle-endian", "-ffreestanding"], False)]
    environment = dict(os.environ, LC_ALL="C")
    environment.pop("C17_CAPTURE_DIR", None)
    evaluated = subprocess.check_output(
        ["bmake", "-C", str(root / "lib/libc"), "MACHINE=rp2040", "-V",
         "${CC} ${CFLAGS} ${CPPFLAGS}"], env=environment, text=True)
    production = shlex.split(evaluated)
    require(production, "evaluated libc compiler is empty")
    tools["arm"] = tool_identity(production[0])
    require(all(flag in production for flag in ["-std=gnu17", "-mcpu=cortex-m0plus",
                                                "-mthumb", "-mfloat-abi=soft",
                                                "-Wall", "-Wextra", "-Werror"]),
            "evaluated libc compiler lacks the required production profile")
    profiles.extend([("arm.gnu17", production, False),
                     ("arm.c17", [*production, "-std=c17", "-pedantic-errors",
                                   "-Wstrict-prototypes", "-Wold-style-definition"], False)])
    header = root / "include/pwd.h"
    original = header.read_text()
    forms = {"standalone": "#include <pwd.h>\n",
             "repeated": "#include <pwd.h>\n#include <pwd.h>\n",
             "peers-before": "#include <sys/types.h>\n#include <stddef.h>\n"
                                     "#include <pwd.h>\n#include <pwd.h>\n",
             "peers-after": "#include <pwd.h>\n#include <sys/types.h>\n"
                                    "#include <stddef.h>\n#include <pwd.h>\n"}
    cases = []
    profile_states = {}
    active_profiles = []
    with tempfile.TemporaryDirectory(prefix="discobsd-passwd-header-") as temporary:
        workspace = Path(temporary)
        probe = workspace / "width-probe.c"
        probe.write_text("int width_probe(void) { return 0; }\n")
        profile_calibration = calibrate_profile_probe(clang, strict, workspace)
        for profile, arguments, optional in profiles:
            state = probe_profile(arguments, probe, optional)
            if state["state"] == "unrun":
                profile_states[profile] = state
                continue
            profile_states[profile] = {"state": "passed", "argv": arguments}
            active_profiles.append((profile, arguments))
            for form, includes in forms.items():
                source = workspace / (profile + "." + form + ".c")
                target_width = ("_Static_assert(sizeof(void *) == 4 && "
                                "sizeof(struct passwd) == 28 && _Alignof(struct passwd) == 4, "
                                "\"password ARM/ILP32 ABI\");\n"
                                if "arm" in profile or profile.endswith(".ilp32") else "")
                source.write_text(includes + assertions() + target_width)
                cases.append(compile_case(arguments, source, profile + "." + form))
        guardless = original.replace("#ifndef _PWD_H_\n", "", 1).replace(
            "#define _PWD_H_\n", "", 1).replace("#endif /* _PWD_H_ */", "", 1)
        unsigned = original.replace("int     pw_uid;", "unsigned int pw_uid;", 1)
        require(guardless != original and unsigned != original,
                "password-header mutation did not change its intended input")
        for label, content, includes, diagnostic in [
                ("guard-disabled", guardless, forms["repeated"], r"redefinition.*passwd"),
                ("uid-unsigned", unsigned, forms["standalone"],
                 r"static assertion failed.*pw_uid type")]:
            mutated = workspace / label
            mutated.mkdir()
            (mutated / "pwd.h").write_text(content)
            source = mutated / "consumer.c"
            source.write_text(includes + assertions())
            for profile, arguments in active_profiles:
                cases.append(compile_case([arguments[0], "-I" + str(mutated), *arguments[1:]],
                                          source, profile + "." + label, diagnostic))
        source = workspace / "callback-mismatch.c"
        source.write_text(forms["standalone"] +
                          "struct passwd *(*wrong_lookup)(unsigned int) = getpwuid;\n")
        for profile, arguments in active_profiles:
            cases.append(compile_case(arguments, source, profile + ".callback-mismatch",
                                      r"incompatible.*pointer"))
    required_profiles = {"host.native", "clang.native", "clang.arm.c17", "arm.gnu17", "arm.c17"}
    require(len(profiles) == 7 and required_profiles <= {name for name, _ in active_profiles}
            and len(cases) == 7 * len(active_profiles),
            "password-header compiler denominator differs")
    return {"schema_version": 1, "header": "include/pwd.h",
            "header_sha256": file_digest(header),
            "verifier_sha256": file_digest(Path(__file__)),
            "evidence_class": "compiler-acceptance", "tools": tools,
            "environment": {"LC_ALL": "C"},
            "production_argv": production, "profiles": profile_states, "cases": cases,
            "profile_calibration": profile_calibration,
            "native_execution": "separate-smaller-c-gate", "hardware": "unrun"}


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
        unrun = sum(profile["state"] == "unrun" for profile in receipt["profiles"].values())
        print(f"PASS passwd header contracts: {len(receipt['cases'])} compiler cases; "
              f"{unrun} optional profiles unrun")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("FAIL passwd header contracts: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
