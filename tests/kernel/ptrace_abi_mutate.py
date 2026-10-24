"""Create calibrated ptrace wrapper argument-clobber mutations."""

import argparse
from pathlib import Path

MUTATIONS = ("drop-save-restore", "postrestore-clobber")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", choices=MUTATIONS, required=True)
    arguments = parser.parse_args()

    source = arguments.source.read_text()
    if arguments.mutation == "drop-save-restore":
        for instruction in ("\tpush\t{r0-r3}\n", "\tpop\t{r0-r3}\n"):
            if source.count(instruction) != 1:
                raise SystemExit("expected one " + instruction.strip())
            source = source.replace(instruction, "", 1)
    else:
        restored_call = "\tpop\t{r0-r3}\n\tsvc\t#SYS_ptrace\n"
        clobbered_call = "\tpop\t{r0-r3}\n\tmov\tr0, #0\n\tsvc\t#SYS_ptrace\n"
        if source.count(restored_call) != 1:
            raise SystemExit("expected one restored syscall sequence")
        source = source.replace(restored_call, clobbered_call, 1)
    arguments.output.write_text(source)


if __name__ == "__main__":
    main()
