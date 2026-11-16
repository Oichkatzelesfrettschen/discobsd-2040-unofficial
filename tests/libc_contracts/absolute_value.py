"""Execute callable absolute-value providers with independent sign controls."""

import argparse
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
         "-fno-builtin", "-O2", "-Dabs=tree_abs", "-Dlabs=tree_labs"]
ARM = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def command(arguments):
    result = subprocess.run(arguments, capture_output=True, text=True,
                            timeout=90, check=False)
    require(result.returncode == 0 and not result.stderr,
            "absolute-value compilation fails: " + result.stdout + result.stderr)


def build(root, compiler, work, arm, replacement=None):
    objects = []
    for name in ("abs", "labs"):
        source = (replacement[1] if replacement and replacement[0] == name else
                  root / "lib/libc/stdlib" / (name + ".c"))
        obj = work / (name + ".o")
        command(compiler + FLAGS + (ARM if arm else []) +
                ["-c", str(source), "-o", str(obj)])
        objects.append(str(obj))
    fixture = work / "fixture.o"
    command(compiler + FLAGS + (ARM if arm else []) + ["-c",
            str(root / "tests/libc_contracts/absolute_value_test.c"), "-o", str(fixture)])
    objects.append(str(fixture))
    link = []
    if arm:
        support = work / "support.o"
        command(compiler + ARM + ["-std=gnu17", "-Wall", "-Wextra", "-Werror",
                "-fomit-frame-pointer", "-c", str(root / "tests/libbn/qemu_support.c"),
                "-o", str(support)])
        objects.append(str(support))
        link = ["-nostartfiles", "-Wl,-Ttext=0x20000", "-lc", "-lgcc"]
    program = work / "absolute_values"
    command(compiler + (ARM if arm else []) + objects + link + ["-o", str(program)])
    return program


def execute(program, arm):
    prefix = ["qemu-arm", "-cpu", "cortex-m0"] if arm else []
    return subprocess.run(prefix + [str(program)], capture_output=True, text=True,
                          timeout=90, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("compiler")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--arm", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    compiler = shlex.split(args.compiler)
    with tempfile.TemporaryDirectory(prefix="absolute-value-") as temporary:
        work = Path(temporary)
        program = build(root, compiler, work, args.arm)
        result = execute(program, args.arm)
        require(result.returncode == 0 and not result.stderr and
                result.stdout == "absolute values: 131088 checks passed\n",
                "absolute-value contract fails: " + result.stdout + result.stderr)
        print(result.stdout, end="")
        accepted = work / "accepted"
        shutil.copy2(program, accepted)
        for status, name in enumerate(("abs", "labs"), start=1):
            original = (root / "lib/libc/stdlib" / (name + ".c")).read_text()
            anchor = "return value < 0 ? -value : value;"
            require(original.count(anchor) == 1, name + " mutation anchor differs")
            source = work / "mutant.c"
            source.write_text(original.replace(anchor, "return value;"))
            program = build(root, compiler, work, args.arm, (name, source))
            result = execute(program, args.arm)
            require(result.returncode == status and result.stdout.startswith("FAIL " + name),
                    name + " mutant lacks its named rejection: " + result.stdout + result.stderr)
            print("absolute-value mutant rejected: " + name + " preserves negative input")
        if args.output:
            shutil.copy2(accepted, args.output)


if __name__ == "__main__":
    main()
