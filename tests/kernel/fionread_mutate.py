"""Create calibrated FIONREAD result-publication mutations."""

import argparse
from pathlib import Path

STAGING = """    case FIONREAD: {
        off_t available = 0;

        u.u_error = (*Fops[fp->f_type]->fo_ioctl)(fp, com,
            (caddr_t)&available);
        if (u.u_error == 0)
            u.u_error = copyout((caddr_t)&available, uap->cmarg,
                sizeof available);
        return;
    }
"""
DIRECT = """    case FIONREAD:
        u.u_error = (*Fops[fp->f_type]->fo_ioctl)(fp, com, uap->cmarg);
        return;
"""
CONDITIONAL_PUBLICATION = """        if (u.u_error == 0)
            u.u_error = copyout((caddr_t)&available, uap->cmarg,
                sizeof available);"""
UNCONDITIONAL_PUBLICATION = """        u.u_error = copyout((caddr_t)&available,
            uap->cmarg, sizeof available);"""
GEOMETRY = """        u_int nbytes = (com >> 16) & IOCPARM_MASK;

        if (nbytes != 0 && baduaddr_span(uap->cmarg, nbytes)) {"""
UNMASKED_SIZE = """        u_int nbytes =
            (com & ~(IOC_INOUT | IOC_VOID)) >> 16;

        if (nbytes != 0 && baduaddr_span(uap->cmarg, nbytes)) {"""
ENDPOINT_SPAN = """        u_int nbytes = (com >> 16) & IOCPARM_MASK;

        if (baduaddr(uap->cmarg) ||
            baduaddr(uap->cmarg + nbytes - 1)) {"""


def replace_once(source, old, new, label):
    if source.count(old) != 1:
        raise ValueError(label + " mutation does not match exactly once")
    return source.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--mutation",
        choices=(
            "direct-user-pointer",
            "unconditional-publication",
            "wrong-width",
            "unmasked-size",
            "endpoint-span",
        ),
        required=True,
    )
    arguments = parser.parse_args()
    source = arguments.source.read_text()

    if arguments.mutation == "direct-user-pointer":
        source = replace_once(source, STAGING, DIRECT, arguments.mutation)
    elif arguments.mutation == "unconditional-publication":
        source = replace_once(
            source,
            CONDITIONAL_PUBLICATION,
            UNCONDITIONAL_PUBLICATION,
            arguments.mutation,
        )
    elif arguments.mutation == "wrong-width":
        source = replace_once(
            source,
            "                sizeof available);",
            "                sizeof(short));",
            arguments.mutation,
        )
    elif arguments.mutation == "unmasked-size":
        source = replace_once(source, GEOMETRY, UNMASKED_SIZE, arguments.mutation)
    else:
        source = replace_once(source, GEOMETRY, ENDPOINT_SPAN, arguments.mutation)
    arguments.output.write_text(source)


if __name__ == "__main__":
    main()
