"""Compile exit/atexit with modeled cleanup and termination; calibrate ordering."""

import pathlib
import shlex
import shutil
import subprocess
import sys
import tempfile

FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror"]
RENAMES = [
    "-Datexit=db_atexit", "-Dexit=db_exit", "-D__atexit=db_atexit_head",
    "-Derrno=exit_test_errno", "-Dmalloc=exit_test_malloc",
    "-D_cleanup=exit_test_cleanup", "-D_exit=exit_test_terminate",
]
CASES = ("nested", "last", "duplicate", "capacity", "failure", "empty")


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def compile_command(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=60,
                            check=False)
    require(result.returncode == 0 and not result.stderr,
            "compile failure: " + result.stdout + result.stderr)


def build(root, compiler, directory, exit_source):
    library = root / "lib/libc/stdio"
    objects = []
    for name, source in (("exit", exit_source), ("atexit", library / "atexit.c")):
        unit = directory / (name + ".o")
        compile_command(compiler + FLAGS + RENAMES + ["-I", str(library), "-c",
                        str(source), "-o", str(unit)])
        objects.append(str(unit))
    program = directory / "exit_lifecycle"
    compile_command(compiler + FLAGS + [str(root / "tests/libc_contracts/exit_lifecycle_test.c"),
                    *objects, "-o", str(program)])
    return program


def run(program, case):
    return subprocess.run([str(program), case], capture_output=True, text=True,
                          timeout=10, check=False)


def main():
    require(len(sys.argv) in (3, 4), "usage: exit_lifecycle.py ROOT COMPILER [OUTPUT]")
    root = pathlib.Path(sys.argv[1]).resolve()
    compiler = shlex.split(sys.argv[2])
    source = root / "lib/libc/stdio/exit.c"
    original = source.read_text()
    pop = ("    while ((p = __atexit) != NULL) {\n"
           "        __atexit = p->next;\n        (*p->func)();\n    }")
    require(original.count(pop) == 1, "exit queue anchor differs")
    mutants = {
        "saved traversal": (pop,
                            "    for (p = __atexit; p; p = p->next)\n        (*p->func)();",
                            "nested"),
        "missing cleanup": ("    _cleanup();", "    ;", "empty"),
        "lost status": ("    _exit (code);", "    (void)code;\n    _exit (0);", "empty"),
    }
    with tempfile.TemporaryDirectory(prefix="exit-lifecycle-") as temporary:
        directory = pathlib.Path(temporary)
        program = build(root, compiler, directory, source)
        for case in CASES:
            result = run(program, case)
            require(result.returncode == 0 and not result.stderr,
                    f"exit case {case} fails: {result.stdout}{result.stderr}")
        accepted = directory / "accepted"
        shutil.copy2(program, accepted)
        for label, (before, after, case) in mutants.items():
            require(original.count(before) == 1, f"{label} anchor differs")
            mutant = directory / "exit_mutant.c"
            mutant.write_text(original.replace(before, after))
            program = build(root, compiler, directory, mutant)
            result = run(program, case)
            require(result.returncode == 1 and "lifecycle mismatch:" in result.stderr,
                    f"{label} lacks the expected behavioral rejection: {result.stderr}")
            print(f"exit lifecycle mutant rejected: {label}")
        if len(sys.argv) == 4:
            shutil.copy2(accepted, sys.argv[3])
    print("exit lifecycle: six cases pass; three behavioral controls reject")


if __name__ == "__main__":
    main()
