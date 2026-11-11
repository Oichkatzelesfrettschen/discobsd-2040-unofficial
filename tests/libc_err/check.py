"""Exercise production err(3) definitions and calibrate output/status checks."""

import errno
import itertools
import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def verify(binary):
    for mode, indirect, formatted in itertools.product(range(4), range(2), range(2)):
        result = subprocess.run(
            [str(binary), str(mode), str(indirect), str(formatted)],
            capture_output=True, text=True, check=False,
        )
        message = "err-fixture: " + ("17 value 1.25" if formatted else "")
        if mode % 2 == 0:
            message += (": " if formatted else "") + os.strerror(errno.ENOENT)
        expected = (23 if mode >= 2 else 0,
                    "" if mode >= 2 else "returned\n", message + "\n")
        actual = (result.returncode, result.stdout, result.stderr)
        if actual != expected:
            raise AssertionError(f"case {(mode, indirect, formatted)}: {actual!r} != {expected!r}")


def main():
    source = (ROOT / "lib/libc/gen/err.c").read_text()
    with tempfile.TemporaryDirectory(prefix="discobsd-err-") as directory:
        work = Path(directory)
        shutil.copyfile(ROOT / "include/err.h", work / "err.h")
        (work / "program.h").write_text("extern char *__progname;\n")
        compiler = shlex.split(os.environ.get("HOST_CC") or "cc")

        def build(name, contents):
            path = work / (name + ".c")
            path.write_text(contents)
            binary = work / name
            subprocess.run(compiler + [
                "-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
                "-Wstrict-prototypes", "-Wold-style-definition", "-fno-builtin",
                "-D__progname=err_fixture_progname",
                "-I" + str(work), "-include", str(work / "program.h"),
                str(path), str(ROOT / "tests/libc_err/fixture.c"), "-o", str(binary),
            ], check=True)
            return binary

        verify(build("production", source))
        for name, before, after in (
            ("wrong-errno", "sverrno = errno;", "sverrno = EINVAL;"),
            ("wrong-status", "exit(eval);", "exit(eval + 1);"),
            ("missing-prefix", "fputs (__progname, stderr);", "(void)__progname;"),
        ):
            if before not in source:
                raise AssertionError(f"missing mutation anchor: {name}")
            try:
                verify(build(name, source.replace(before, after)))
            except AssertionError:
                continue
            raise AssertionError(f"accepted {name}")
    print("PASS err(3): 32 output/status/return cases; three mutations rejected")


if __name__ == "__main__":
    main()
