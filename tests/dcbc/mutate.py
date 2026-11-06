"""Fault-injection check for the register table and the name table.

usage: mutate.py --root TREE --golden golden.json --work DIRECTORY
       [--hostcc CC] [--bison BISON] [--flex FLEX] [--list]

Each mutation replaces text that must occur exactly once in usr.bin/dc/bcode.c
(the lazily allocated register table and the count J pops), usr.bin/dc/stack.c
(the ownership of a value's array) or usr.bin/bc/bc.y (the name table that
replaced hcreate and hsearch), builds dc and bc from the faulted copy, and runs
run_cases.py against it; a faulted dc is also built with the allocation
accounting and run against peak.json. A mutant is rejected when either run
exits nonzero. The script exits nonzero when a mutant survives or its anchor
text has moved, so a refactor cannot leave an unfaulted "mutant" behind.
"""

import argparse
import os
import shutil
import subprocess
import sys

BCODE = "usr.bin/dc/bcode.c"
STACKC = "usr.bin/dc/stack.c"
BCY = "usr.bin/bc/bc.y"

# (name, file, original text, replacement)
MUTATIONS = (
    (
        "reg_page_zero",
        BCODE,
        "page = &bmachine.regpage[(u_int)idx / REG_PAGE_SIZE];",
        "page = &bmachine.regpage[0];",
    ),
    (
        "reg_slot_low4",
        BCODE,
        "return &(*page)[(u_int)idx % REG_PAGE_SIZE];",
        "return &(*page)[(u_int)idx % 16];",
    ),
    (
        "reg_no_init",
        BCODE,
        "\t\tfor (i = 0; i < REG_PAGE_SIZE; i++)\n\t\t\tstack_init(&(*page)[i]);\n",
        "\t\t(void)i;\n",
    ),
    (
        "reg_page_never_new",
        BCODE,
        "\tif (*page == NULL) {\n\t\t*page = bmalloc(",
        "\tif (0) {\n\t\t*page = bmalloc(",
    ),
    (
        "reg_eager",
        BCODE,
        "\tbmachine.readstack_sz = READSTACK_SIZE;\n",
        "\tfor (i = 0; i < bmachine.reg_array_size / REG_PAGE_SIZE; i++)\n"
        "\t\t(void)regstack((int)(i * REG_PAGE_SIZE));\n"
        "\tbmachine.readstack_sz = READSTACK_SIZE;\n",
    ),
    (
        "reg_range_short",
        BCODE,
        "#define REG_ARRAY_SIZE_BIG\t(UCHAR_MAX + 1 + USHRT_MAX + 1)",
        "#define REG_ARRAY_SIZE_BIG\t(UCHAR_MAX + 1 + USHRT_MAX)",
    ),
    (
        "skip_count_leaked",
        BCODE,
        "\ti = get_ulong(n);\n\tfree_number(n);\n\tif (i == (BN_ULONG)-1)\n\t\twarnx(\"J command",
        "\ti = get_ulong(n);\n\tif (i == (BN_ULONG)-1)\n\t\twarnx(\"J command",
    ),
    (
        "stack_push_copies_array",
        STACKC,
        "\tstack->stack[stack->sp].array = v->array;\n",
        "\tstack->stack[stack->sp].array = v->array == NULL ?\n\t    NULL : array_dup(v->array);\n",
    ),
    (
        "stack_set_tos_copies_array",
        STACKC,
        "\t\tstack->stack[stack->sp] = *v;\n",
        "\t\tstack->stack[stack->sp] = *v;\n"
        "\t\tstack->stack[stack->sp].array = v->array == NULL ?\n"
        "\t\t    NULL : array_dup(v->array);\n",
    ),
    (
        "array_dup_grows_empty",
        STACKC,
        "\tif (a->size > 0)\n\t\tarray_grow(n, a->size);\n",
        "\tarray_grow(n, a->size);\n",
    ),
    (
        "names_compare_first_byte",
        BCY,
        "while (table[i].key != NULL && strcmp(table[i].key, key) != 0)",
        "while (table[i].key != NULL && table[i].key[0] != key[0])",
    ),
    (
        "names_rehash_drops",
        BCY,
        "\t\t\t\t*names_slot(grown, slots, names[i].key) =\n\t\t\t\t    names[i];\n",
        "\t\t\t\t(void)slots;\n",
    ),
    (
        "names_never_grows",
        BCY,
        "if (2 * (names_used + 1) > names_slots) {",
        "if (0) {",
    ),
    (
        "names_no_probe",
        BCY,
        "\t\ti = (i + 1) & (slots - 1);\n",
        "\t\ti = (i + 0) & (slots - 1);\n",
    ),
    (
        "names_key_dropped",
        BCY,
        "\tslot->key = key;\n",
        "\tslot->key = NULL;\n",
    ),
    (
        "names_count_dropped",
        BCY,
        "\tnames_used++;\n",
        "\t(void)names_used;\n",
    ),
    (
        "names_function_as_variable",
        BCY,
        "\t\treturn lookup(str, len, 'F');",
        "\t\treturn lookup(str, len, 'L');",
    ),
)


def run(command, cwd, timeout=600):
    return subprocess.run(
        command, cwd=cwd, capture_output=True, text=True, timeout=timeout, check=False
    )


def write_mutant(root, work, name, target, old, new):
    with open(os.path.join(root, target), encoding="utf-8") as handle:
        text = handle.read()
    if text.count(old) != 1:
        raise SystemExit(
            "mutate.py: %s: anchor occurs %d times in %s" % (name, text.count(old), target)
        )
    path = os.path.join(work, name + "." + os.path.basename(target))
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text.replace(old, new))
    return path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--golden", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--hostcc", default=os.environ.get("HOSTCC", "cc"))
    parser.add_argument("--bison", default=os.environ.get("BISON", "bison"))
    parser.add_argument("--flex", default=os.environ.get("LEX_HOST", "flex"))
    parser.add_argument(
        "--no-peak",
        action="store_true",
        help="skip the -m32 heap run; a mutant only it would reject is reported UNJUDGED",
    )
    args = parser.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(args.root)
    work = os.path.abspath(args.work)
    golden = os.path.abspath(args.golden)
    os.makedirs(work, exist_ok=True)
    dcdir = os.path.join(root, "usr.bin/dc")
    bcdir = os.path.join(root, "usr.bin/bc")
    libbn = os.path.join(root, "lib/libbn")
    flags = [
        "-std=c17",
        "-pedantic-errors",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-D_DEFAULT_SOURCE",
        "-O1",
        "-include",
        os.path.join(here, "host_compat.h"),
        "-I" + libbn,
    ]

    support = [os.path.join(libbn, "bn.c"), os.path.join(here, "host_compat.c")]
    # The tree's malloc(0) returns NULL; the dc and bc builds take that
    # answer from zero_alloc.c, and the peak build from peak_alloc.c.
    zero_alloc = [
        os.path.join(here, "zero_alloc.c"),
        "-Wl,--wrap=malloc,--wrap=realloc,--wrap=reallocarray",
    ]
    scan = os.path.join(work, "scan.c")
    done = run([args.flex, "-o", scan, os.path.join(bcdir, "scan.l")], work)
    if done.returncode:
        raise SystemExit("mutate.py: flex failed: " + done.stderr)
    unjudged = []
    survivors = []
    for name, target, old, new in MUTATIONS:
        mutated = write_mutant(root, work, name, target, old, new)
        bcsource = mutated if target == BCY else os.path.join(bcdir, "bc.y")
        out = os.path.join(work, name)
        shutil.rmtree(out, ignore_errors=True)
        # The faulted bcode.c sits in a copy of usr.bin/dc so that its
        # quoted includes find dc's extern.h and not bc's.
        copy = os.path.join(out, "dcsrc")
        shutil.copytree(dcdir, copy, ignore=shutil.ignore_patterns("*.o", "dc", "*.0"))
        if target in (BCODE, STACKC):
            shutil.copyfile(mutated, os.path.join(copy, os.path.basename(target)))
        bcode = os.path.join(copy, "bcode.c")
        os.makedirs(os.path.join(out, "dc"))
        os.makedirs(os.path.join(out, "bc"))
        os.makedirs(os.path.join(out, "peak", "dc"))
        others = [os.path.join(copy, f) for f in ("dc.c", "inout.c", "mem.c", "stack.c")]
        build = (
            [args.hostcc]
            + flags
            + [
                "-I" + copy,
                "-o",
                os.path.join(out, "dc", "dc"),
                os.path.join(copy, "main.c"),
                bcode,
            ]
            + others
            + support
            + zero_alloc
        )
        done = run(build, work)
        if done.returncode:
            raise SystemExit("mutate.py: %s: dc build failed:\n%s" % (name, done.stderr))
        done = run(
            [args.bison, "-y", "-Wno-yacc", "-o", os.path.join(out, "bc.c"), "-d", bcsource],
            work,
        )
        if done.returncode:
            raise SystemExit("mutate.py: %s: bison failed:\n%s" % (name, done.stderr))
        build = (
            [args.hostcc]
            + flags
            + [
                '-D_PATH_LIBB="%s"' % os.path.join(bcdir, "bc.library"),
                "-I" + out,
                "-I" + bcdir,
                "-I" + copy,
                "-o",
                os.path.join(out, "bc", "bc"),
                os.path.join(out, "bc.c"),
                scan,
                bcode,
            ]
            + others
            + support
            + zero_alloc
        )
        done = run(build, work)
        if done.returncode:
            raise SystemExit("mutate.py: %s: bc build failed:\n%s" % (name, done.stderr))
        peak_build = (
            [args.hostcc]
            + flags
            + [
                "-m32",
                "-I" + copy,
                "-o",
                os.path.join(out, "peak", "dc", "dc"),
                os.path.join(copy, "main.c"),
                bcode,
            ]
            + others
            + support
            + [
                os.path.join(here, "peak_alloc.c"),
                "-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=reallocarray,"
                "--wrap=strdup,--wrap=free",
            ]
        )
        if not args.no_peak:
            done = run(peak_build, work)
            if done.returncode:
                raise SystemExit("mutate.py: %s: peak build failed:\n%s" % (name, done.stderr))
        reason = None
        done = run(
            [
                sys.executable,
                "-I",
                os.path.join(here, "run_cases.py"),
                "--dc",
                os.path.join(out, "dc", "dc"),
                "--bc",
                os.path.join(out, "bc", "bc"),
                "--golden",
                golden,
                "--timeout",
                "10",
            ],
            work,
        )
        if done.returncode:
            lines = [ln for ln in done.stdout.splitlines() if ln.startswith("FAIL")]
            reason = lines[0][:100] if lines else "exit %d" % done.returncode
        elif args.no_peak:
            unjudged.append(name)
            print("UNJUDGED %s: only the -m32 heap run could reject it" % name)
            continue
        else:
            done = run(
                [
                    sys.executable,
                    "-I",
                    os.path.join(here, "run_cases.py"),
                    "--peak",
                    os.path.join(out, "peak", "dc", "dc"),
                    "--peak-limits",
                    os.path.join(here, "peak.json"),
                    "--timeout",
                    "10",
                ],
                work,
            )
            if done.returncode:
                lines = [ln for ln in done.stdout.splitlines() if ln.startswith("FAIL")]
                reason = lines[0][:100] if lines else "peak exit %d" % done.returncode
        if reason is None:
            survivors.append(name)
            print("SURVIVED %s" % name)
        else:
            print("PASS dcbc mutation %s rejected: %s" % (name, reason))
    if survivors:
        print("dcbc mutants: %d of %d survived" % (len(survivors), len(MUTATIONS)))
        return 1
    rejected = len(MUTATIONS) - len(unjudged)
    print("dcbc mutants: %d of %d rejected, %d unjudged without the -m32 heap run"
          % (rejected, len(MUTATIONS), len(unjudged)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
