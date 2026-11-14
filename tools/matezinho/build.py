#!/usr/bin/env python3
"""Build the native or ARMv6-M development executable without board access."""

import argparse
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tools/matezinho/out"


def build(backend):
    OUT.mkdir(exist_ok=True)
    common = ["-std=gnu17", "-Wall", "-Wextra", "-Werror", "-Os"]
    sources = [
        "usr.bin/matezinho/main.c",
        "usr.bin/matezinho/core.c",
        "sys/arch/rp2040/dev/lcdfont.c",
    ]
    output = OUT / f"matezinho.{backend}"
    if backend == "host":
        command = [os.environ.get("HOST_CC", "cc"), *common, "-D_DEFAULT_SOURCE"]
    else:
        gcc = os.environ.get("ARM_CC", "arm-none-eabi-gcc")
        command = [
            gcc,
            *common,
            "-mcpu=cortex-m0plus",
            "-mthumb",
            "-mfloat-abi=soft",
            "-DMZ_QEMU",
            "-nostartfiles",
            "-Wl,-T,tools/matezinho/qemu.ld",
        ]
        if os.environ.get("NEWLIB_INCLUDE"):
            command += ["-I" + os.environ["NEWLIB_INCLUDE"]]
        if os.environ.get("NEWLIB_LIB"):
            command += ["-L" + os.environ["NEWLIB_LIB"]]
        sources += ["tools/matezinho/qemu_runtime.c", "-lc", "-lgcc"]
    subprocess.run([*command, "-o", str(output), *sources], cwd=ROOT, check=True)
    return output


def run_command(backend, executable, volume, frame):
    command = [str(executable), str(volume), str(frame)]
    if backend == "qemu":
        qemu = shutil.which("qemu-arm")
        if not qemu:
            raise RuntimeError("qemu-arm is required; no ARM execution occurred")
        command = [qemu, "-cpu", "cortex-m0", *command]
    return command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("host", "qemu"), default="host")
    args = parser.parse_args()
    print(build(args.backend))


if __name__ == "__main__":
    main()
