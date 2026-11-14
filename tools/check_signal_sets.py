"""Compile signal-set headers and providers for target and native input."""

import argparse
import hashlib
import json
import os
import shlex
import subprocess
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("cross", "qemu"))
    args = parser.parse_args()
    work = ROOT / ".build-logs" / ("signal-set-" + uuid.uuid4().hex)
    work.mkdir(parents=True)
    rows = []

    def run(label, command, reject=None):
        result = subprocess.run(command, cwd=work, capture_output=True, timeout=120, check=False)
        (work / (label + ".log")).write_bytes(result.stdout + result.stderr)
        rows.append({"label": label, "command": command, "exit": result.returncode})
        if reject is None:
            if result.returncode or result.stderr:
                raise RuntimeError("command fails clean execution: " + label)
        elif result.returncode <= 0 or reject.encode() not in result.stdout + result.stderr:
            raise RuntimeError("control misses named rejection: " + label)
        print("PASS signal-set " + label, flush=True)

    cc = os.environ.get("SIGNAL_ARM_CC", "arm-none-eabi-gcc")
    cpu = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft"]
    strict = [
        cc,
        "-std=c17",
        "-pedantic-errors",
        "-Wall",
        "-Wextra",
        "-Werror",
        *cpu,
        "-nostdinc",
        "-I" + str(ROOT / "include"),
    ]
    fixture = ROOT / "tests/libc_contracts/signal_set_header.c"
    provider = ROOT / "lib/libc/gen/sigsetops.c"
    run("header", [*strict, "-c", str(fixture), "-o", "header.o"])
    run("provider", [*strict, "-c", str(provider), "-o", "provider.o"])
    if args.mode == "qemu":
        run(
            "link",
            [
                cc,
                "-std=gnu17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Os",
                *cpu,
                "-nostartfiles",
                "-Wl,-Ttext=0x20000",
                "header.o",
                "provider.o",
                str(ROOT / "tests/libbn/qemu_support.c"),
                "-lc",
                "-lgcc",
                "-o",
                "target",
            ],
        )
        run("execute", [os.environ.get("QEMU_ARM", "qemu-arm"), "-cpu", "cortex-m0", "target"])
    else:
        overlay = work / "bad-header"
        overlay.mkdir()
        header = (ROOT / "sys/sys/signal.h").read_text()
        before = "int sigismember(const sigset_t *, int);"
        if header.count(before) != 1:
            raise RuntimeError("membership prototype anchor differs")
        (overlay / "signal.h").write_text(
            header.replace(before, "int sigismember(sigset_t *, int);")
        )
        run(
            "const-rejection",
            [*strict[:1], "-I" + str(overlay), *strict[1:], "-c", str(fixture), "-o", "bad.o"],
            "incompatible-pointer-types",
        )
        run(
            "native-build",
            [
                *shlex.split(os.environ.get("HOST_CC", "cc")),
                "-O1",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DTHUMB",
                "-DNO_ANNOTATIONS",
                "-DNO_PPACK",
                "-DSTATIC",
                "-DNO_EXTRA_WARNS",
                "-DSYNTAX_STACK_MAX=3200",
                "-o",
                "smlrc",
                str(ROOT / "usr.bin/smlrc/smlrc.c"),
            ],
        )
        run(
            "native-preprocess",
            [
                cc,
                "-E",
                "-P",
                "-nostdinc",
                "-I" + str(ROOT / "include"),
                "-U__GNUC__",
                "-U__PCC__",
                "-D__SMALLER_C__=0x0100",
                "-D__SMALLER_C_32__",
                "-D__SMALLER_C_UCHAR__",
                "-D__arm__",
                "-D__unix__",
                "-D__BSD__",
                "-D__DISCOBSD__",
                "-D_TIME_BITS=32",
                str(fixture),
                "-o",
                "input.i",
            ],
        )
        run("native-compile", ["./smlrc", "input.i", "output.s"])
        run(
            "native-assemble",
            [
                os.environ.get("SIGNAL_ARM_AS", "arm-none-eabi-as"),
                "-mcpu=cortex-m0plus",
                "-o",
                "native.o",
                "output.s",
            ],
        )
        (work / "invalid.i").write_text("int broken( {\n")
        run("native-rejection", ["./smlrc", "invalid.i", "invalid.s"], "Unexpected token {")
    sources = [fixture, provider, ROOT / "sys/sys/signal.h"]
    (work / "receipt.json").write_text(
        json.dumps(
            {
                "mode": args.mode,
                "sources": {
                    str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in sources
                },
                "commands": rows,
                "hardware": "unrun",
            },
            indent=2,
        )
        + "\n"
    )


if __name__ == "__main__":
    main()
