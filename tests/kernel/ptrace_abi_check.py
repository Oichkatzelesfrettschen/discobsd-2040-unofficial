"""Verify that the ARM ptrace entry preserves all four syscall arguments."""

import argparse
import re
import subprocess
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit("FAIL ptrace libc ABI: " + message)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--object", type=Path, required=True)
    arguments = parser.parse_args()

    result = subprocess.run(
        [arguments.objdump, "-dr", str(arguments.object)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    output = result.stdout
    function_match = re.search(
        r"^[0-9a-f]+ <ptrace>:\n(?P<body>.*?)(?=\n[0-9a-f]+ <|\Z)",
        output,
        flags=re.MULTILINE | re.DOTALL,
    )
    if function_match is None:
        fail("ptrace symbol is absent")
    body = function_match.group("body")
    instructions = []
    for line in body.splitlines():
        match = re.match(r"\s*[0-9a-f]+:\s+(?:[0-9a-f]{4}\s+)+\s*(.*)", line)
        if match is not None:
            instructions.append(re.sub(r"\s+", " ", match.group(1).strip()))

    required_patterns = (
        r"push\s+\{r0, r1, r2, r3\}",
        r"ldr\s+r1,",
        r"movs?\s+r2, #0",
        r"str\s+r2, \[r1, #0\]",
        r"pop\s+\{r0, r1, r2, r3\}",
        r"svc\s+(?:#)?26",
        r"bcs(?:\.n)?\s+",
        r"bx\s+lr",
        r"ldr\s+r1,",
        r"str\s+r0, \[r1, #0\]",
        r"movs?\s+r0, #0",
        r"mvns?\s+r0, r0",
        r"bx\s+lr",
    )
    if len(instructions) < len(required_patterns):
        fail("missing ordered instruction pattern " + required_patterns[len(instructions)])
    for position, pattern in enumerate(required_patterns):
        if re.match(pattern, instructions[position]) is None:
            fail("missing ordered instruction pattern " + pattern)
    if sum(instruction.startswith("svc") for instruction in instructions) != 1:
        fail("ptrace must contain exactly one supervisor call")
    if "R_ARM_ABS32\terrno" not in output:
        fail("errno relocation is absent")
    print("ptrace libc ABI disassembly contract passed")


if __name__ == "__main__":
    main()
