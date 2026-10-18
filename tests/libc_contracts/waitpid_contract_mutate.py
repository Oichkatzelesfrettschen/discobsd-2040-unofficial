"""Prove the waitpid contract rejects C17 and forwarding regressions."""

import pathlib
import shlex
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def compile_source(root, compiler, source, output):
    return subprocess.run(
        compiler
        + [
            "-std=c17",
            "-pedantic-errors",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wstrict-prototypes",
            "-Wold-style-definition",
            "-Wmissing-prototypes",
            "-nostdinc",
            "-isystem",
            str(root / "include"),
            "-Dwaitpid=db_waitpid",
            "-Dwait4=test_wait4",
            "-c",
            str(source),
            "-o",
            str(output),
        ],
        capture_output=True,
        text=True,
    )


def main():
    require(len(sys.argv) == 4, "usage: waitpid_contract_mutate.py ROOT CC TEST")
    root = pathlib.Path(sys.argv[1]).resolve()
    compiler = shlex.split(sys.argv[2])
    test_source = pathlib.Path(sys.argv[3]).resolve()
    source = (root / "lib/libc/gen/waitpid.c").read_text(encoding="utf-8")
    signature = "waitpid(pid_t pid, int *istat, int options)"
    forwarding = "return (wait4(pid, istat, options, (struct rusage *)0));"
    require(source.count(signature) == 1, "waitpid signature anchor differs")
    require(source.count(forwarding) == 1, "waitpid forwarding anchor differs")

    with tempfile.TemporaryDirectory(prefix="waitpid-contract-") as name:
        directory = pathlib.Path(name)
        baseline_object = directory / "waitpid_baseline.o"
        result = compile_source(
            root,
            compiler,
            root / "lib/libc/gen/waitpid.c",
            baseline_object,
        )
        require(result.returncode == 0, result.stdout + result.stderr)

        old_style = directory / "waitpid_old_style.c"
        old_style.write_text(
            source.replace(
                signature,
                "waitpid(pid, istat, options)\n"
                "\tpid_t pid;\n"
                "\tint *istat;\n"
                "\tint options;",
            ),
            encoding="utf-8",
        )
        result = compile_source(
            root, compiler, old_style, directory / "waitpid_old_style.o"
        )
        require(result.returncode != 0, "waitpid old-style mutant was accepted")

        wrong_selector = directory / "waitpid_wrong_selector.c"
        wrong_selector.write_text(
            source.replace(
                forwarding,
                "return (wait4(pid + 1, istat, options, (struct rusage *)0));",
            ),
            encoding="utf-8",
        )
        mutant_object = directory / "waitpid_wrong_selector.o"
        result = compile_source(root, compiler, wrong_selector, mutant_object)
        require(result.returncode == 0, result.stdout + result.stderr)
        executable = directory / "waitpid_wrong_selector"
        subprocess.run(
            compiler
            + [
                "-std=c17",
                "-pedantic-errors",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wstrict-prototypes",
                "-Wold-style-definition",
                "-Wmissing-prototypes",
                str(test_source),
                str(mutant_object),
                "-o",
                str(executable),
            ],
            check=True,
        )
        result = subprocess.run(executable, capture_output=True, text=True)
        require(result.returncode == 1, "waitpid selector mutant was accepted")
        require(
            "forwards the process selector" in result.stderr,
            "waitpid selector mutant failed for an unexpected reason",
        )

    print("PASS waitpid contract: old-style and selector mutants rejected")


if __name__ == "__main__":
    main()
