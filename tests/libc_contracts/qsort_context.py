"""Execute context-last sorting and reject broken context, order and record paths."""

import argparse
import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
         "-fno-builtin", "-O2"]
ARM = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os"]


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def command(arguments):
    result = subprocess.run(arguments, capture_output=True, text=True,
                            timeout=90, check=False)
    require(result.returncode == 0 and not result.stderr,
            "qsort context compile failure: " + result.stdout + result.stderr)


def build(root, compiler, work, arm, source):
    flags = FLAGS + (ARM if arm else [])
    library = work / "sort.o"
    # Renaming after system headers avoids host qsort_r compatibility macros.
    namespace = work / "namespace.h"
    namespace.write_text("""#include <stdlib.h>
#undef qsort_r
#define qsort_r db_qsort_r
#undef qsort
#define qsort db_qsort
void qsort_r(void *, size_t, size_t,
    int (*)(const void *, const void *, void *), void *);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
""")
    mode = [] if source.name == "qsort_r.c" else ["-D_DISCOBSD_QSORT_R"]
    command(compiler + flags + mode + ["-include", str(namespace),
            "-c", str(source), "-o", str(library)])
    fixture = work / "fixture.o"
    command(compiler + flags + ["-c", str(root / "tests/libc_contracts/qsort_context_test.c"),
            "-o", str(fixture)])
    plain = work / "plain.o"
    command(compiler + flags + ["-include", str(namespace), "-c",
            str(root / "lib/libc/gen/qsort.c"), "-o", str(plain)])
    objects = [str(library), str(fixture), str(plain)]
    link = []
    if arm:
        support = work / "support.o"
        command(compiler + ARM + ["-std=gnu17", "-Wall", "-Wextra", "-Werror",
                "-fomit-frame-pointer", "-c", str(root / "tests/libbn/qemu_support.c"),
                "-o", str(support)])
        objects.append(str(support))
        link = ["-nostartfiles", "-Wl,-Ttext=0x20000", "-lc", "-lgcc"]
    program = work / "qsort_context"
    command(compiler + (ARM if arm else []) + objects + link + ["-o", str(program)])
    return program


def execute(program, arm):
    prefix = ["qemu-arm", "-cpu", "cortex-m0"] if arm else []
    return subprocess.run(prefix + [str(program)], capture_output=True, text=True,
                          timeout=120, check=False, env=dict(os.environ, LC_ALL="C"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("compiler")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--arm", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    compiler = shlex.split(args.compiler)
    require(bool(compiler), "qsort context compiler is empty")
    if args.arm:
        require(shutil.which("qemu-arm") is not None, "qemu-arm is required")
    mutations = [
        ("lost context", "st.context = context;", "st.context = NULL; (void)context;"),
        ("reversed order", "function(left, right, context)", "function(right, left, context)"),
        ("skipped sorting", "if (n <= 1 || size == 0)", "if (n <= 1 || size != 0)"),
        ("shared call state", "struct qsort_state st;", "static struct qsort_state st;"),
        ("lost record byte", "*i++ = *jj;", "*i++ = 0;"),
    ]
    source = root / "lib/libc/gen/qsort.c"
    original = source.read_text()
    with tempfile.TemporaryDirectory(prefix="qsort-context-") as temporary:
        work = Path(temporary)
        program = build(root, compiler, work, args.arm, source.with_name("qsort_r.c"))
        result = execute(program, args.arm)
        require(result.returncode == 0 and not result.stderr,
                "qsort context fails: " + result.stdout + result.stderr)
        print(result.stdout, end="")
        accepted = work / "accepted"
        shutil.copy2(program, accepted)
        for label, before, after in mutations:
            require(original.count(before) == 1, label + " mutation anchor differs")
            mutant = work / "mutant.c"
            mutant.write_text(original.replace(before, after))
            program = build(root, compiler, work, args.arm, mutant)
            result = execute(program, args.arm)
            diagnostic = result.returncode == 1 and "qsort context:" in result.stderr
            # Shared state retains the nested callback's expired stack context.
            lifetime = (label == "shared call state" and result.returncode != 0 and
                        "ERROR: AddressSanitizer: stack-use-after-return" in result.stderr and
                        "in compare" in result.stderr)
            require(diagnostic or lifetime,
                    label + " lacks behavioral rejection: " + result.stdout + result.stderr)
            print("qsort context mutant rejected: " + label)
        if args.output:
            shutil.copy2(accepted, args.output)
    print("qsort context: five behavioral controls reject")


if __name__ == "__main__":
    main()
