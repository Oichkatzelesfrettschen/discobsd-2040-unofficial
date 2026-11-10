"""Decode every errno message through the generated kernel_errmsg()."""

import argparse
import json
import re
import shlex
import subprocess
import tempfile
from pathlib import Path


class ContractError(Exception):
    pass


# Each mutant changes the generated decoder or its tables in one place and
# has to make the harness fail; a harness that accepts one checks nothing.
MUTANTS = (
    ("rule-halves-swapped", "end = errno_message_expand(end, rule[0]);\n"
     "        symbol = rule[1];", "end = errno_message_expand(end, rule[1]);\n"
     "        symbol = rule[0];"),
    ("first-half-dropped", "        end = errno_message_expand(end, rule[0]);\n", ""),
    ("terminator-missing", "    *end++ = 0;\n", "    end++;\n"),
    ("length-without-terminator", "return (u_int)(end - buffer);",
     "return (u_int)(end - buffer) - 1;"),
    ("range-check-off-by-one", "if (error >= ERRNO_MESSAGE_COUNT)",
     "if (error > ERRNO_MESSAGE_COUNT)"),
    ("first-rule-altered", "errno_message_rules[128][2] = {\n    {",
     "errno_message_rules[128][2] = {\n    {1 + "),
)


def messages(root: Path) -> list[str]:
    result = []
    for line in (root / "sys/kern/errlist.master").read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            result.append(line.split("|", 1)[1].strip())
    return result


def harness(expected: list[str], limit: int) -> str:
    lines = [
        "#include <string.h>",
        "#include <sys/systm.h>",
        "int main(void)",
        "{",
        f"    static const char *const expected[] = {{{', '.join(map(json.dumps, expected))}}};",
        f"    char guarded[{limit} + 2];",
        "    unsigned int error, length;",
        f"    for (error = 0; error < {len(expected)}; error++) {{",
        "        memset(guarded, 0x55, sizeof(guarded));",
        "        length = kernel_errmsg(error, guarded + 1);",
        "        if (length != strlen(expected[error]) + 1) return 1;",
        "        if (memcmp(guarded + 1, expected[error], length) != 0) return 2;",
        "        if (guarded[0] != 0x55 || guarded[1 + length] != 0x55) return 3;",
        "    }",
        "    memset(guarded, 0x55, sizeof(guarded));",
        f"    if (kernel_errmsg({len(expected)}, guarded + 1) != 0 ||",
        "        kernel_errmsg(~0U, guarded + 1) != 0 || guarded[1] != 0x55) return 4;",
        "    return 0;",
        "}",
    ]
    return "\n".join(lines) + "\n"


def run(directory: Path, host_cc: str, source: str, main: str) -> int:
    (directory / "errlist.c").write_text(source)
    (directory / "main.c").write_text(main)
    program = directory / "errmsg"
    subprocess.run(
        [*shlex.split(host_cc), "-std=c17", "-Wall", "-Wextra", "-Werror",
         "-fsanitize=address,undefined",
         "-fno-sanitize-recover=all", f"-I{directory}", "-o", str(program),
         str(directory / "errlist.c"), str(directory / "main.c")],
        check=True,
    )
    return subprocess.run([str(program)], check=False, capture_output=True).returncode


def check(root: Path, host_cc: str) -> str:
    source = (root / "sys/kern/errlist.c").read_text()
    systm = (root / "sys/sys/systm.h").read_text()
    limit_match = re.search(r"^#define KERNEL_ERRMSG_MAX (\d+)$", systm, re.MULTILINE)
    last_match = re.search(
        r"^#define\s+ELAST\s+(\d+)", (root / "sys/sys/errno.h").read_text(), re.MULTILINE
    )
    if limit_match is None or last_match is None:
        raise ContractError("KERNEL_ERRMSG_MAX or ELAST is not a plain definition")
    limit = int(limit_match.group(1))
    expected = messages(root)
    if max(map(len, expected)) + 1 > limit:
        raise ContractError("a message is longer than KERNEL_ERRMSG_MAX")
    with tempfile.TemporaryDirectory(prefix="errmsg-contracts-") as name:
        directory = Path(name)
        (directory / "sys").mkdir()
        # The generated file includes three kernel headers; the harness
        # supplies the four names it takes from them.
        (directory / "sys/param.h").write_text(
            "#include <stdint.h>\ntypedef unsigned int u_int;\n")
        (directory / "sys/errno.h").write_text(f"#define ELAST {last_match.group(1)}\n")
        (directory / "sys/systm.h").write_text(
            f"#include <sys/param.h>\n#define KERNEL_ERRMSG_MAX {limit}\n"
            "u_int kernel_errmsg(u_int error, char *buffer);\n")
        main = harness(expected, limit)
        status = run(directory, host_cc, source, main)
        if status != 0:
            raise ContractError(f"generated kernel_errmsg failed with status {status}")
        for label, old, new in MUTANTS:
            if source.count(old) != 1:
                raise ContractError(f"mutant {label} does not apply exactly once")
            if run(directory, host_cc, source.replace(old, new), main) == 0:
                raise ContractError(f"mutant {label} passed")
    return (f"PASS errno message contracts: {len(expected)} messages, "
            f"{len(MUTANTS)} mutants rejected")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--host-cc", default="cc")
    arguments = parser.parse_args()
    try:
        print(check(arguments.root.resolve(), arguments.host_cc))
    except (ContractError, OSError, subprocess.CalledProcessError) as error:
        print(f"FAIL errno message contracts: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
