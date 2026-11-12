"""Exercise independent quick-exit ownership through renamed production sources."""

import argparse
import pathlib
import shlex
import shutil
import subprocess
import tempfile

FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror"]
ARM = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]
RENAMES = [
    "-Dat_quick_exit=db_at_quick_exit", "-Dquick_exit=db_quick_exit",
    "-D__quick_exit_head=db_quick_exit_head", "-Datexit=db_atexit",
    "-Dexit=db_exit", "-D__atexit=db_atexit_head", "-Derrno=quick_test_errno",
    "-Dmalloc=quick_test_malloc", "-D_cleanup=quick_test_cleanup",
    "-D_Exit=db_Exit", "-D_exit=quick_test_terminate",
]
CASES = ("nested", "last", "duplicate", "capacity", "failure", "retry",
         "first-no-heap", "last-repeat", "normal-isolation", "quick-isolation",
         "negative-status", "empty")


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def compile_command(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=60,
                            check=False)
    require(result.returncode == 0 and not result.stderr,
            "compile failure: " + result.stdout + result.stderr)


def production_flags(root, target):
    return FLAGS + (ARM + ["-nostdinc", "-I" + str(root / "include")] if target else [])


def link_program(root, compiler, directory, fixture, objects, program, target):
    unit = directory / "fixture.o"
    compile_command(compiler + FLAGS + (ARM if target else []) + ["-c", str(fixture),
                    "-o", str(unit)])
    inputs = [*objects, str(unit)]
    flags = []
    if target:
        support = directory / "support.o"
        compile_command(compiler + ARM + ["-std=gnu17", "-Wall", "-Wextra", "-Werror",
                        "-fomit-frame-pointer", "-c", str(root / "tests/libbn/qemu_support.c"),
                        "-o", str(support)])
        inputs.append(str(support))
        flags = ["-nostartfiles", "-Wl,-Ttext=0x20000", "-lc", "-lgcc"]
    compile_command(compiler + (ARM if target else []) + inputs + flags +
                    ["-o", str(program)])


def build(root, compiler, directory, overrides, target):
    stdlib = root / "lib/libc/stdlib"
    stdio = root / "lib/libc/stdio"
    objects = []
    sources = [stdlib / name for name in ("at_quick_exit.c", "quick_exit.c", "_Exit.c")]
    sources += [stdio / "atexit.c", stdio / "exit.c"]
    for source in sources:
        unit = directory / (source.stem + ".o")
        compile_command(compiler + production_flags(root, target) + RENAMES +
                        ["-I", str(stdlib), "-I", str(stdio),
                        "-c", str(overrides.get(source.name, source)), "-o", str(unit)])
        objects.append(str(unit))
    program = directory / "quick_exit"
    link_program(root, compiler, directory, root / "tests/libc_contracts/quick_exit_test.c",
                 objects, program, target)
    return program


def run(program, case, target):
    prefix = ["qemu-arm", "-cpu", "cortex-m0"] if target else []
    return subprocess.run(prefix + [str(program)] + ([case] if case else []),
                          capture_output=True, text=True,
                          timeout=10, check=False)


def real_termination(root, compiler, directory, target):
    objects = []
    for filename in ("at_quick_exit.c", "quick_exit.c", "_Exit.c"):
        unit = directory / ("real_" + filename + ".o")
        compile_command(compiler + production_flags(root, target) + RENAMES[:3] +
                        ["-D_Exit=db_Exit", "-c",
                        str(root / "lib/libc/stdlib" / filename), "-o", str(unit)])
        objects.append(str(unit))
    program = directory / "real_termination"
    fixture = root / "tests/libc_contracts/quick_exit_termination_test.c"
    link_program(root, compiler, directory, fixture, objects, program, target)
    result = run(program, None, target)
    require((result.returncode, result.stdout, result.stderr) == (7, "Q", ""),
            "real termination mismatch: " + repr(result))
    original = (root / "lib/libc/stdlib/_Exit.c").read_text()
    require(original.count("_exit(status);") == 1, "immediate-exit control anchor differs")
    mutant = directory / "flushing_exit.c"
    mutant.write_text(original.replace("_exit(status);", "exit(status);"))
    compile_command(compiler + production_flags(root, target) + RENAMES[:3] +
                    ["-D_Exit=db_Exit", "-c", str(mutant),
                    "-o", str(directory / "real__Exit.c.o")])
    link_program(root, compiler, directory, fixture, objects, program, target)
    result = run(program, None, target)
    require((result.returncode, result.stdout, result.stderr) == (99, "Q", ""),
            "normal-exit control lacks registered-handler rejection: " + repr(result))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=pathlib.Path)
    parser.add_argument("compiler")
    parser.add_argument("output", type=pathlib.Path, nargs="?")
    parser.add_argument("--arm", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    compiler = shlex.split(args.compiler)
    require(bool(compiler), "compiler command is empty")
    if args.arm:
        require(shutil.which("qemu-arm") is not None, "qemu-arm is required")
    pop = ("\twhile ((entry = __quick_exit_head) != NULL) {\n"
           "\t\t__quick_exit_head = entry->next;\n\t\tentry->function();\n\t}")
    mutants = {
        "saved traversal": ("quick_exit.c", pop,
                            "\tfor (entry = __quick_exit_head; entry; entry = entry->next)\n"
                            "\t\tentry->function();", "nested"),
        "normal cleanup": ("quick_exit.c", "\t_Exit(status);", "\texit(status);", "empty"),
        "lost status": ("quick_exit.c", "\t_Exit(status);",
                        "\t(void)status;\n\t_Exit(0);", "negative-status"),
        "false allocation success": ("at_quick_exit.c", "\t\t\treturn -1;",
                                     "\t\t\treturn 0;", "failure"),
        "dropped older entries": ("at_quick_exit.c", "entry->next = __quick_exit_head;",
                                  "entry->next = NULL;", "duplicate"),
    }
    with tempfile.TemporaryDirectory(prefix="quick-exit-") as temporary:
        directory = pathlib.Path(temporary)
        program = build(root, compiler, directory, {}, args.arm)
        for case in CASES:
            result = run(program, case, args.arm)
            require(result.returncode == 0 and not result.stderr,
                    f"quick-exit case {case} fails: {result.stdout}{result.stderr}")
        accepted = directory / "accepted"
        shutil.copy2(program, accepted)
        for label, (filename, before, after, case) in mutants.items():
            original = (root / "lib/libc/stdlib" / filename).read_text()
            require(original.count(before) == 1, f"{label} anchor differs")
            mutant = directory / filename
            mutant.write_text(original.replace(before, after))
            program = build(root, compiler, directory, {filename: mutant}, args.arm)
            result = run(program, case, args.arm)
            require(result.returncode == 1 and "mismatch:" in result.stderr,
                    f"{label} lacks behavioral rejection: {result.stdout}{result.stderr}")
            print(f"quick-exit mutant rejected: {label}")
        if args.output:
            shutil.copy2(accepted, args.output)
        real_termination(root, compiler, directory, args.arm)
    print("quick-exit: twelve modeled cases and real termination pass; six controls reject")


if __name__ == "__main__":
    main()
