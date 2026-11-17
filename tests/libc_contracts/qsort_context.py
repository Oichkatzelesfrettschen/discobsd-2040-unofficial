"""Execute context-last sorting and reject broken context, order and record paths."""

import argparse
import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

from qsort_callers import check_callers

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


def bounds(root, compiler, work, arm, original):
    flags = FLAGS + (ARM if arm else [])
    entry = "    sort(base, count, &state, budget);"
    heap = "            heap(base, count, state);"
    fixture = (root / "tests/libc_contracts/qsort_bounds_test.c").read_text()
    for anchor in (entry, heap, "        budget--;"):
        require(original.count(anchor) == 1, "bound mutation anchor differs")
    require(fixture.count("calls++;") == 1, "counter mutation anchor differs")
    variants = [
        ("good", original, fixture, 0, None),
        ("budget", original.replace("        budget--;", "        (void)budget;"),
         fixture, 4, "comparison bound:"),
        ("fallback", original.replace(entry, "    budget=0;\n" + entry).replace(
            heap, "            (void)heap;\n"
            "            for (size_t i=1;i<count;i++) "
            "(void)CMP(base+(i-1)*width,base+i*width);"),
         fixture, 1, "qsort bounds: output order"),
        ("counter", original, fixture.replace("calls++;", "(void)0;"),
         3, "comparison counter:"),
    ]
    for label, provider, consumer, expected, diagnostic in variants:
        source, test = work / "bounds-provider.c", work / "bounds-fixture.c"
        source.write_text(provider)
        test.write_text(consumer)
        library, caller = work / "bounds-provider.o", work / "bounds-fixture.o"
        command(compiler + flags + ["-include", str(work / "namespace.h"),
                "-c", str(source), "-o", str(library)])
        command(compiler + flags + ["-c", str(test), "-o", str(caller)])
        objects = [str(library), str(caller)]
        link = []
        if arm:
            objects.append(str(work / "support.o"))
            link = ["-nostartfiles", "-Wl,-Ttext=0x20000", "-lc", "-lgcc"]
        program = work / "bounds"
        command(compiler + (ARM if arm else []) + objects + link +
                ["-o", str(program)])
        result = execute(program, arm)
        require(result.returncode == expected, "qsort bound control exit differs: " +
                label + ": " + result.stdout + result.stderr)
        if diagnostic is None:
            require(not result.stderr and
                    "qsort counts: 64 pattern cases pass" in result.stdout,
                    "qsort bound matrix lacks complete execution")
        else:
            require(diagnostic in result.stderr,
                    "qsort bound control misses named rejection: " + label)
        print("qsort bounds control: " + label + " passes")


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
        ("lost context", "state.context = context;", "state.context = NULL; (void)context;"),
        ("reversed order", "state->compare(a, b, state->context)",
         "state->compare(b, a, state->context)"),
        ("skipped sorting", "if (count < 2 || !width)", "if (count < 2 || width)"),
        ("shared call state", "struct qsort_state state;", "static struct qsort_state state;"),
        ("lost record byte", "*a++ = *b;", "*a++ = 0;"),
    ]
    source = root / "lib/libc/gen/qsort.c"
    original = source.read_text()
    with tempfile.TemporaryDirectory(prefix="qsort-context-") as temporary:
        work = Path(temporary)
        program = build(root, compiler, work, args.arm, source.with_name("qsort_r.c"))
        result = execute(program, args.arm)
        require(result.returncode == 0 and not result.stderr and
                "qsort context: 708596 order, permutation and context cases pass" in result.stdout,
                "qsort context fails: " + result.stdout + result.stderr)
        print(result.stdout, end="")
        accepted = work / "accepted"
        shutil.copy2(program, accepted)
        bounds(root, compiler, work, args.arm, original)
        for label, before, after in mutations:
            require(original.count(before) == 1, label + " mutation anchor differs")
            mutant = work / "mutant.c"
            mutant.write_text(original.replace(before, after))
            program = build(root, compiler, work, args.arm, mutant)
            result = execute(program, args.arm)
            diagnostic = result.returncode == 1 and "qsort context:" in result.stderr
            # Shared state retains the nested callback's expired stack context.
            lifetime = (label == "shared call state" and result.returncode != 0 and
                        any("ERROR: AddressSanitizer: " + violation in result.stderr
                            for violation in ("stack-use-after-return",
                                              "stack-use-after-scope")) and
                        "in compare" in result.stderr)
            require(diagnostic or lifetime,
                    label + " lacks behavioral rejection: " + result.stdout + result.stderr)
            print("qsort context mutant rejected: " + label)
        if args.arm:
            check_callers(root, compiler, work, execute=True)
        if args.output:
            shutil.copy2(accepted, args.output)
    print("qsort context: five context and three bound controls reject")


if __name__ == "__main__":
    main()
