"""Check the public assert macro and the libc failure routine on the host."""

import os
import resource
import signal
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
compiler = os.environ.get("HOSTCC", "cc")
flags = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror"]

with tempfile.TemporaryDirectory(prefix="discobsd-assert-") as directory:
    temporary = Path(directory)
    for name in ("assert_contract_test", "assert_reinclude_test"):
        binary = temporary / name
        subprocess.run(
            [compiler, *flags, "-iquote", str(root / "include"),
             str(root / "tests/libc_contracts" / f"{name}.c"),
             str(root / "lib/libc/gen/assert_fail.c"), "-o", str(binary)],
            check=True,
        )
        subprocess.run([binary], check=True)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    result = subprocess.run(
        [temporary / "assert_contract_test", "fail"], capture_output=True, text=True
    )
    assert result.returncode == -signal.SIGABRT, result.returncode
    for fragment in ("count != 0", "assert_contract_test.c", "line 33", "main"):
        assert fragment in result.stderr, result.stderr

    header = (root / "include/assert.h").read_text(encoding="utf-8")
    original = "#define assert(expression) ((void)0)"
    assert header.count(original) == 1
    (temporary / "assert.h").write_text(
        header.replace(original, "#define assert(expression) ((void)(expression))"),
        encoding="utf-8",
    )
    mutant = temporary / "assert-mutant"
    subprocess.run(
        [compiler, *flags, "-iquote", str(temporary),
         str(root / "tests/libc_contracts/assert_contract_test.c"),
         str(root / "lib/libc/gen/assert_fail.c"), "-o", str(mutant)],
        check=True,
    )
    result = subprocess.run([mutant], capture_output=True, text=True)
    assert result.returncode == -signal.SIGABRT, result.returncode
    assert "disabled() == 0" in result.stderr, result.stderr

print("assert contract: PASS; disabled-evaluation mutant rejected")
