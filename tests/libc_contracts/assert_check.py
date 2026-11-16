"""Check the public assert macro and the libc failure routine on the host."""

import os
import resource
import shlex
import signal
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
compiler = shlex.split(os.environ.get("HOSTCC", "cc"))
flags = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror"]


def require(condition, message):
    if not condition:
        raise SystemExit(f"assert contract: FAIL: {message}")


with tempfile.TemporaryDirectory(prefix="discobsd-assert-") as directory:
    temporary = Path(directory)
    for name in ("assert_contract_test", "assert_reinclude_test"):
        binary = temporary / name
        subprocess.run(
            [*compiler, *flags, "-iquote", str(root / "include"),
             str(root / "tests/libc_contracts" / f"{name}.c"),
             str(root / "lib/libc/gen/assert_fail.c"), "-o", str(binary)],
            check=True,
        )
        subprocess.run([binary], check=True)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    result = subprocess.run(
        [temporary / "assert_contract_test", "fail"], capture_output=True, text=True
    )
    require(result.returncode == -signal.SIGABRT, result.returncode)
    for fragment in ("count != 0", "assert_contract_test.c", "line 39", "main"):
        require(fragment in result.stderr, result.stderr)

    header = (root / "include/assert.h").read_text(encoding="utf-8")
    original = "#define assert(expression) ((void)0)"
    require(header.count(original) == 1, "disabled assertion mutation anchor differs")
    (temporary / "assert.h").write_text(
        header.replace(original, "#define assert(expression) ((void)(expression))"),
        encoding="utf-8",
    )
    mutant = temporary / "assert-mutant"
    subprocess.run(
        [*compiler, *flags, "-iquote", str(temporary),
         str(root / "tests/libc_contracts/assert_contract_test.c"),
         str(root / "lib/libc/gen/assert_fail.c"), "-o", str(mutant)],
        check=True,
    )
    result = subprocess.run([mutant], capture_output=True, text=True)
    require(result.returncode == -signal.SIGABRT, result.returncode)
    require("disabled() == 0" in result.stderr, result.stderr)

    # A false constant expression is a translation failure that carries the
    # message; a header without the macro leaves the contract test unbuildable.
    false_source = temporary / "static_assert_false.c"
    false_source.write_text(
        '#include "assert.h"\n'
        'static_assert(sizeof(char) == 2, "discobsd false constant");\n'
        "int main(void) { return 0; }\n",
        encoding="utf-8",
    )
    result = subprocess.run(
        [*compiler, *flags, "-iquote", str(root / "include"),
         "-fsyntax-only", str(false_source)],
        capture_output=True, text=True,
    )
    require(result.returncode != 0, "false static_assert compiled")
    require("discobsd false constant" in result.stderr, result.stderr)

    definition = "#define static_assert _Static_assert\n"
    require(header.count(definition) == 1, "static assertion mutation anchor differs")
    (temporary / "assert.h").write_text(
        header.replace(definition, ""), encoding="utf-8"
    )
    result = subprocess.run(
        [*compiler, *flags, "-iquote", str(temporary), "-fsyntax-only",
         str(root / "tests/libc_contracts/assert_contract_test.c")],
        capture_output=True, text=True,
    )
    require(result.returncode != 0, "contract test built without static_assert")

print("assert contract: PASS; disabled-evaluation, false-constant and "
      "missing-static_assert mutants rejected")
