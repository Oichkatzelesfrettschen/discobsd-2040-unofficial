"""Reject a kill(2) authorization regression in the production signal path."""

import pathlib
import shlex
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def run(command):
    subprocess.run(command, check=True)


def main():
    require(
        len(sys.argv) == 8,
        "usage: sigauth_mutate.py ROOT CC TEST PROT2 PROC SHIM HARNESS",
    )
    root = pathlib.Path(sys.argv[1]).resolve()
    compiler = shlex.split(sys.argv[2])
    test_source = pathlib.Path(sys.argv[3]).resolve()
    linked_objects = [pathlib.Path(argument).resolve() for argument in sys.argv[4:]]
    source_path = root / "sys/kern/kern_sig.c"
    source = source_path.read_text(encoding="utf-8")
    authorization = (
        "        if (! cansignal (p, uap->signo))\n"
        "            error = EPERM;"
    )
    bypass = "        if (0)\n            error = EPERM;"
    require(source.count(authorization) == 1, "kill authorization anchor differs")

    kernel_flags = [
        "-std=gnu17",
        "-ffreestanding",
        "-fno-builtin",
        "-Wno-asm-operand-widths",
        "-DKERNEL",
        "-I" + str(root / "tests/kernel"),
        "-I" + str(root / "sys"),
        "-I" + str(root / "sys/arch"),
        "-Dprintf=hk_kprintf",
        "-Dmalloc=rmap_malloc",
        "-Dmfree=rmap_mfree",
        "-Dmalloc3=rmap_malloc3",
        "-Dmalloc3_contiguous=rmap_malloc3_contiguous",
        "-Dmalloc3_contiguous_next=rmap_malloc3_contiguous_next",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-deprecated-non-prototype",
        "-m32",
    ]
    gate_flags = [flag for flag in kernel_flags if flag != "-Wno-deprecated-non-prototype"]

    with tempfile.TemporaryDirectory(prefix="sigauth-mutation-") as name:
        directory = pathlib.Path(name)
        mutant_source = directory / "kern_sig_permission_mutant.c"
        mutant_source.write_text(
            source.replace(authorization, bypass), encoding="utf-8"
        )
        mutant_object = directory / "kern_sig_permission_mutant.o"
        run(compiler + kernel_flags + ["-c", str(mutant_source), "-o", str(mutant_object)])
        executable = directory / "sigauth_permission_mutant"
        run(
            compiler
            + gate_flags
            + [str(test_source), str(mutant_object)]
            + [str(path) for path in linked_objects]
            + ["-o", str(executable)]
        )
        result = subprocess.run(executable, capture_output=True, text=True)
        require(result.returncode == 1, "kill authorization mutant was accepted")
        require(
            "u.u_error == EPERM" in result.stderr,
            "kill authorization mutant failed for an unexpected reason",
        )

    print("PASS kill contract: authorization mutant rejected")


if __name__ == "__main__":
    main()
