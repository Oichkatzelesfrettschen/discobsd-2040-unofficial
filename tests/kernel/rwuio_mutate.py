"""Generate calibrated syscall I/O span-admission mutants."""

import argparse
from pathlib import Path

ADMISSION_BLOCK = """    for (vector = uio->uio_iov, vector_index = 0;
        vector_index < (u_int)uio->uio_iovcnt;
        vector_index++, vector++) {
        if (vector->iov_len != 0 &&
            baduaddr_span(vector->iov_base, vector->iov_len)) {
            u.u_error = EFAULT;
            return;
        }
    }
"""


def replace_once(source, original, replacement, label):
    if source.count(original) != 1:
        raise ValueError(label + " mutation anchor differs")
    return source.replace(original, replacement, 1)


def mutate(source, mutation):
    if mutation == "removed_admission":
        return replace_once(source, ADMISSION_BLOCK, "", mutation)
    if mutation == "first_byte_only":
        return replace_once(
            source,
            "baduaddr_span(vector->iov_base, vector->iov_len)",
            "baduaddr(vector->iov_base)",
            mutation,
        )
    raise ValueError("unknown mutation: " + mutation)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--mutation",
        choices=("removed_admission", "first_byte_only"),
        required=True,
    )
    arguments = parser.parse_args()

    source = arguments.source.read_text(encoding="utf-8")
    arguments.output.write_text(
        mutate(source, arguments.mutation), encoding="utf-8"
    )


if __name__ == "__main__":
    main()
