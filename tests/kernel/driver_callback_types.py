"""Require distinct controller and device probe signatures in config tables."""

import argparse
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FLAGS = ("-std=gnu17", "-Wall", "-Wextra", "-Werror", "-Wshadow", "-Wundef",
         "-Wformat=2")
LEVELS = ("O0", "O", "O1", "O2", "O3", "Og", "Os", "Oz")

DEVICE_MISMATCH = """\
#include <sys/config.h>
static int controller_probe(struct conf_ctlr *controller) { return controller != 0; }
struct driver device = { "wrong", controller_probe };
"""
CONTROLLER_MISMATCH = """\
#include <sys/config.h>
static int device_probe(struct conf_device *device) { return device != 0; }
struct controller_driver controller = { "wrong", device_probe };
"""
MATCHING_CALLBACKS = """\
#include <sys/config.h>
static int controller_probe(struct conf_ctlr *controller) { return controller != 0; }
static int device_probe(struct conf_device *device) { return device != 0; }
struct controller_driver controller = { "controller", controller_probe };
struct driver device = { "device", device_probe };
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", action="append", dest="compilers")
    args = parser.parse_args()
    compilers = args.compilers or ["cc", "clang", "arm-none-eabi-gcc"]
    failures = []
    with tempfile.TemporaryDirectory(prefix="discobsd-driver-callback-") as temporary:
        directory = Path(temporary)
        header = (ROOT / "sys/sys/config.h").read_text(encoding="utf-8")
        typed_slot = "(*d_init)(struct conf_device *)"
        if header.count(typed_slot) != 1:
            print("FAIL driver-callback-types: device callback type is absent or ambiguous")
            return 1
        mutant_include = directory / "mutant"
        (mutant_include / "sys").mkdir(parents=True)
        (mutant_include / "sys/config.h").write_text(
            header.replace(typed_slot, "(*d_init)()"), encoding="utf-8"
        )
        for compiler in compilers:
            compiler_parts = shlex.split(compiler)
            executable = shutil.which(compiler_parts[0]) if compiler_parts else None
            if executable is None:
                failures.append(f"{compiler}: compiler unavailable")
                continue
            for level in LEVELS:
                for name, source, should_compile in (
                    ("device-mismatch", DEVICE_MISMATCH, False),
                    ("controller-mismatch", CONTROLLER_MISMATCH, False),
                    ("matching", MATCHING_CALLBACKS, True),
                ):
                    input_path = directory / f"{name}.c"
                    input_path.write_text(source, encoding="utf-8")
                    output_path = directory / f"{name}.o"
                    command = [executable, *compiler_parts[1:], *FLAGS, f"-{level}",
                               "-I", str(ROOT / "sys"),
                               "-c", str(input_path), "-o", str(output_path)]
                    result = subprocess.run(command, capture_output=True, text=True, check=False)
                    label = f"{compiler} -{level} {name}"
                    if should_compile and result.returncode != 0:
                        failures.append(f"{label}: matching signatures rejected: "
                                        f"{result.stderr.strip()}")
                    if not should_compile and (
                        result.returncode == 0 or "incompatible" not in result.stderr.lower()
                    ):
                        failures.append(f"{label}: incompatible assignment not rejected: "
                                        f"{result.stderr.strip()}")
                service_source = ROOT / "tests/kernel/service_callback_contract.c"
                for wrong_arity in (False, True):
                    service_command = [executable, *compiler_parts[1:], *FLAGS, f"-{level}",
                                       "-I", str(ROOT / "sys"), "-fsyntax-only"]
                    if wrong_arity:
                        service_command.append("-DTEST_WRONG_ARITY")
                    service_command.append(str(service_source))
                    service = subprocess.run(service_command, capture_output=True, text=True,
                                             check=False)
                    if (not wrong_arity and service.returncode != 0) or (
                        wrong_arity and (service.returncode == 0 or
                                         "incompatible" not in service.stderr.lower())
                    ):
                        failures.append(f"{compiler} -{level} service wrong_arity={wrong_arity}: "
                                        f"unexpected result: {service.stderr.strip()}")
            mutant_path = directory / "mutant-device.c"
            mutant_path.write_text(DEVICE_MISMATCH, encoding="utf-8")
            mutant_command = [executable, *compiler_parts[1:], *FLAGS, "-Os", "-I",
                              str(mutant_include), "-c", str(mutant_path), "-o",
                              str(directory / "mutant-device.o")]
            mutant = subprocess.run(mutant_command, capture_output=True, text=True, check=False)
            if mutant.returncode != 0:
                failures.append(f"{compiler}: weakened-header mutant failed unexpectedly: "
                                f"{mutant.stderr.strip()}")
    if failures:
        for failure in failures:
            print(f"FAIL driver-callback-types: {failure}")
        return 1
    print(f"PASS driver-callback-types: {len(compilers)} compilers reject category mismatches")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
