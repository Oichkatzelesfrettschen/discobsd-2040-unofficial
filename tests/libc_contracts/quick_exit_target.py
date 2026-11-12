"""Check target signatures, native input, and allocator-free termination selection."""

import argparse
import pathlib
import shlex
import subprocess
import tempfile

FIXTURE = """#include <stdlib.h>
#include <stdlib.h>
int (*registration)(void (*)(void)) = at_quick_exit;
void (*termination)(int) = quick_exit;
void (*immediate)(int) = _Exit;
"""
FLAGS = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
         "-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-ffreestanding",
         "-fno-builtin", "-fno-common", "-Os", "-nostdinc"]


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def command(argv, *, cwd=None, reject=None):
    result = subprocess.run(argv, cwd=cwd, capture_output=True, text=True,
                            timeout=60, check=False)
    if reject is None:
        require(result.returncode == 0 and not result.stderr,
                "target command fails: " + repr(argv) + "\n" + result.stdout + result.stderr)
    else:
        require(result.returncode != 0 and reject in result.stdout + result.stderr,
                "target control lacks named rejection: " + repr(result))
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=pathlib.Path)
    parser.add_argument("compiler")
    parser.add_argument("--host", default="cc")
    args = parser.parse_args()
    root = args.root.resolve()
    compiler = args.compiler
    require(compiler.endswith("gcc"), "target compiler must name the GCC driver")
    prefix = compiler[:-3]
    with tempfile.TemporaryDirectory(prefix="quick-exit-target-") as temporary:
        work = pathlib.Path(temporary)
        flags = FLAGS + ["-I" + str(root / "include")]
        source = work / "fixture.c"
        source.write_text(FIXTURE)
        command([compiler, *flags, "-c", str(source), "-o", str(work / "fixture.o")])
        source.write_text(FIXTURE.replace("int (*registration)", "void (*registration)"))
        command([compiler, *flags, "-c", str(source), "-o", str(work / "bad.o")],
                reject="incompatible-pointer-types")
        source.write_text(FIXTURE)
        for name in ("at_quick_exit", "quick_exit", "_Exit"):
            command([compiler, *flags, "-c", str(root / "lib/libc/stdlib" / (name + ".c")),
                     "-o", str(work / (name + ".o"))])

        smaller = work / "smlrc"
        command([*shlex.split(args.host), "-O1", "-Wall", "-Wextra", "-Werror", "-DTHUMB",
                 "-DNO_ANNOTATIONS", "-DNO_PPACK", "-DSTATIC", "-DNO_EXTRA_WARNS",
                 "-DSYNTAX_STACK_MAX=3200", "-o", str(smaller),
                 str(root / "usr.bin/smlrc/smlrc.c")])
        command([compiler, "-E", "-P", "-nostdinc", "-I" + str(root / "include"),
                 "-U__GNUC__", "-U__PCC__", "-D__SMALLER_C__=0x0100", "-D__SMALLER_C_32__",
                 "-D__SMALLER_C_UCHAR__", "-D__arm__", "-D__unix__", "-D__BSD__",
                 "-D__DISCOBSD__", str(source), "-o", str(work / "input.i")])
        command([str(smaller), "input.i", "output.s"], cwd=work)
        command([prefix + "as", "-mcpu=cortex-m0plus", "output.s", "-o", "native.o"], cwd=work)
        (work / "invalid.i").write_text("int broken( {\n")
        command([str(smaller), "invalid.i", "invalid.s"], cwd=work, reject="Error")

        source.write_text("#include <stdlib.h>\nvoid probe(void) { quick_exit(7); }\n")
        command([compiler, *flags, "-c", str(source), "-o", str(work / "probe.o")])
        archive = root / "lib/libc.a"
        require(archive.is_file(), "build the target libc before archive selection")
        command([compiler, "-mcpu=cortex-m0plus", "-mthumb", "-nostdlib", "-Wl,-e,probe",
                 "-Wl,-Ttext=0x20000000", "-o", str(work / "probe"),
                 str(work / "probe.o"), str(archive)])
        symbols = command([prefix + "nm", str(work / "probe")])
        defined = {line.split()[-1] for line in symbols.splitlines() if line.split()}
        require({"probe", "quick_exit", "_Exit", "_exit", "__quick_exit_head"} <= defined,
                "termination archive closure lacks required symbols")
        require(not ({"malloc", "free", "exit", "atexit", "at_quick_exit", "_cleanup"} & defined),
                "termination-only consumer selects allocation or normal cleanup")
        print("quick-exit target: strict signatures, native input and termination selection pass")
        print(command([prefix + "size", str(work / "probe")]), end="")


if __name__ == "__main__":
    main()
