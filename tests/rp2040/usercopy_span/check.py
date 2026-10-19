"""Check the portable RP2040 user-span predicate and target consumers."""

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path


class ContractError(ValueError):
    """A user-copy interface or calibrated verdict differs."""


def require(condition, message):
    if not condition:
        raise ContractError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_command(command, label, cases, *, cwd=None, reject=None):
    observed = subprocess.run(
        command,
        cwd=cwd,
        capture_output=True,
        text=True,
        env=dict(os.environ, LC_ALL="C"),
    )
    cases.append(
        {
            "label": label,
            "argv": command,
            "exit_code": observed.returncode,
            "stdout": observed.stdout,
            "stderr": observed.stderr,
            "expected_rejection": reject,
        }
    )
    output = observed.stderr + observed.stdout
    if reject is None:
        require(observed.returncode == 0, label + ": " + output)
    else:
        require(
            observed.returncode == 1 and re.search(reject, output, re.I | re.S),
            label + " violated rejection contract: " + output,
        )


def consumer_source():
    return r'''#include <machine/usercopy.h>
#include <machine/usercopy.h>

_Static_assert(_Generic(&rp2040_user_span_contains,
    int (*)(unsigned int, unsigned int, unsigned int, unsigned int): 1,
    default: 0), "user span predicate type");

#define CHECK(expression) do { if (!(expression)) return 1; } while (0)

int
main(void)
{
    CHECK(rp2040_user_span_contains(0x20000000U, 0x20024000U,
        0x20000000U, 0U));
    CHECK(rp2040_user_span_contains(0x20000000U, 0x20024000U,
        0x20024000U, 0U));
    CHECK(!rp2040_user_span_contains(0x20000000U, 0x20024000U,
        0x1fffffffU, 0U));
    CHECK(!rp2040_user_span_contains(0x20000000U, 0x20024000U,
        0x20024001U, 0U));
    CHECK(rp2040_user_span_contains(0x20000000U, 0x20024000U,
        0x20000000U, 0x24000U));
    CHECK(rp2040_user_span_contains(0x20000000U, 0x20024000U,
        0x20023fffU, 1U));
    CHECK(!rp2040_user_span_contains(0x20000000U, 0x20024000U,
        0x20023fffU, 2U));
    CHECK(!rp2040_user_span_contains(0x20000000U, 0x20024000U,
        0x20001000U, 0xfffff001U));
    CHECK(!rp2040_user_span_contains(0x20024000U, 0x20000000U,
        0x20001000U, 1U));
    return 0;
}
'''


def check(root, host_cc, output):
    cases = []
    header = root / "sys/arch/rp2040/include/usercopy.h"
    machdep = root / "sys/arch/rp2040/rp2040/machdep.c"
    require(header.is_file(), "RP2040 user-copy contract header is absent")
    host = shlex.split(host_cc)
    require(host and shutil.which(host[0]), "host compiler is unavailable")
    host[0] = shutil.which(host[0])
    clang = shutil.which("clang")
    arm = shutil.which("arm-none-eabi-gcc")
    require(clang and arm, "Clang and Arm GCC are required")
    common = [
        "-std=c17",
        "-pedantic-errors",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wstrict-prototypes",
        "-Wold-style-definition",
        "-ffreestanding",
        "-fno-builtin",
        "-nostdinc",
    ]
    target = [
        "-mcpu=cortex-m0plus",
        "-mabi=aapcs",
        "-mlittle-endian",
        "-mthumb",
        "-mfloat-abi=soft",
    ]
    tools = {
        tool: {
            "sha256": digest(Path(tool)),
            "version": subprocess.check_output(
                [tool, "--version"], text=True
            ).splitlines()[0],
        }
        for tool in sorted({host[0], clang, arm})
    }
    with tempfile.TemporaryDirectory(prefix="discobsd-usercopy-span-") as temporary:
        workspace = Path(temporary)
        (workspace / "machine").symlink_to(header.parent, target_is_directory=True)
        consumer = workspace / "consumer.c"
        consumer.write_text(consumer_source())
        include_flags = ["-I" + str(workspace), "-I" + str(root / "sys")]
        profiles = [
            ("host.c17", [*host, *common, *include_flags], True),
            (
                "host.sanitized.c17",
                [
                    *host,
                    *common,
                    "-fsanitize=address,undefined,alignment",
                    "-fno-omit-frame-pointer",
                    *include_flags,
                ],
                True,
            ),
            ("clang.c17", [clang, *common, *include_flags], True),
            ("arm.c17", [arm, *common, *target, *include_flags], False),
            (
                "clang.arm.c17",
                [clang, "--target=arm-none-eabi", *common, *target, *include_flags],
                False,
            ),
        ]
        for label, flags, execute in profiles:
            if execute:
                executable = workspace / (label + ".test")
                run_command([*flags, str(consumer), "-o", str(executable)],
                            label + ".compile", cases)
                run_command([str(executable)], label + ".execute", cases)
            else:
                run_command([*flags, "-fsyntax-only", str(consumer)],
                            label + ".compile", cases)

        original = header.read_text()
        wrapped = original.replace(
            "length <= upper - start", "start + length <= upper", 1
        )
        require(wrapped != original, "wraparound mutation is inert")
        mutant_machine = workspace / "mutant/machine"
        mutant_machine.mkdir(parents=True)
        (mutant_machine / "usercopy.h").write_text(wrapped)
        mutant = workspace / "wraparound-mutant"
        run_command(
            [*host, *common, "-I" + str(workspace / "mutant"),
             "-I" + str(root / "sys"), str(consumer), "-o", str(mutant)],
            "wraparound-mutant.compile",
            cases,
        )
        run_command([str(mutant)], "wraparound-mutant.execute", cases, reject=r".*")

        unguarded = original.replace("#ifndef _RP2040_USERCOPY_H_\n", "", 1)
        unguarded = unguarded.replace("#define _RP2040_USERCOPY_H_\n", "", 1)
        unguarded = unguarded.replace("#endif\t/* !_RP2040_USERCOPY_H_ */\n", "", 1)
        require(unguarded != original, "include-guard mutation is inert")
        guard_machine = workspace / "guard/machine"
        guard_machine.mkdir(parents=True)
        (guard_machine / "usercopy.h").write_text(unguarded)
        run_command(
            [*host, *common, "-I" + str(workspace / "guard"),
             "-I" + str(root / "sys"), "-fsyntax-only", str(consumer)],
            "unguarded-header.compile",
            cases,
            reject=r"redefinition",
        )

        for configuration in ("PICO", "PICO_UART"):
            build = root / "sys/arch/rp2040/compile" / configuration
            evaluated = subprocess.check_output(
                [
                    "bmake",
                    "-C",
                    str(build),
                    "-V",
                    "${CC} ${CFLAGS} ${INCLUDES} ${PARAM} -DKERNEL",
                ],
                text=True,
            )
            flags = shlex.split(evaluated)
            require(
                all(
                    flag in flags
                    for flag in (
                        "-std=gnu17",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-mcpu=cortex-m0plus",
                        "-mthumb",
                        "-mfloat-abi=soft",
                    )
                ),
                configuration + " production flags differ",
            )
            run_command(
                [*flags, "-fsyntax-only", str(machdep)],
                configuration + ".machdep",
                cases,
                cwd=build,
            )

    result = {
        "schema_version": 1,
        "source_hashes": {
            str(header.relative_to(root)): digest(header),
            str(machdep.relative_to(root)): digest(machdep),
        },
        "tools": tools,
        "cases": cases,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n")
    return len(cases)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--host-cc", default="cc")
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        count = check(arguments.root.resolve(), arguments.host_cc, arguments.output)
    except (ContractError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit("user-copy span contracts: " + str(error)) from error
    print(f"PASS RP2040 user-copy span contracts: {count} calibrated cases")


if __name__ == "__main__":
    main()
