"""Extract and mutate the production pipe file-operation adapter."""
import argparse
from pathlib import Path

from exec_elf_admission_extract import definition_signature, extract_function

MUTATIONS = {
    "exact-fit": ("chunk != pending", "chunk <= pending"),
    "split-record": ("request <= PIPE_BUF", "request < PIPE_BUF"),
    "nonblock": ("fp->f_flag & FNONBLOCK ? IO_NDELAY : 0", "0"),
    "sync": ("flag |= IO_SYNC;", "flag |= 0;"),
    "mount": ("MNT_SYNCHRONOUS))", "0))"),
    "append": ("return (writep(fp, uio, flag));",
               "return (writep(fp, uio, flag | IO_APPEND));"),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutant", choices=MUTATIONS)
    args = parser.parse_args()
    source = args.source.read_text()
    function = extract_function(
        source, definition_signature(source, "int", "pipe_rw"), "pipe_rw")
    writer = extract_function(
        source, definition_signature(source, "int", "writep"), "writep")
    function += "\nstatic " + writer.replace("writep(", "bounded_write(", 1)
    if args.mutant:
        original, replacement = MUTATIONS[args.mutant]
        if function.count(original) != 1:
            raise ValueError(f"{args.mutant} mutation anchor is not unique")
        function = function.replace(original, replacement, 1)
    args.output.write_text(function + "\n")


if __name__ == "__main__":
    main()
