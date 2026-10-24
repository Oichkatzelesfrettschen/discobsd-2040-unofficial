"""Restore the inherited ptrace argument-clobbering sequence."""

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()

    source = arguments.source.read_text()
    for instruction in ("\tpush\t{r0-r3}\n", "\tpop\t{r0-r3}\n"):
        if source.count(instruction) != 1:
            raise SystemExit("expected one " + instruction.strip())
        source = source.replace(instruction, "", 1)
    arguments.output.write_text(source)


if __name__ == "__main__":
    main()
