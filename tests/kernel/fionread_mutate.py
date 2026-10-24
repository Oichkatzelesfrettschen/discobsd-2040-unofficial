"""Create calibrated ioctl payload-ownership mutations."""

import argparse
from pathlib import Path

STAGING = """    case FIONREAD:
        payload.offset = 0;
        u.u_error = (*Fops[fp->f_type]->fo_ioctl)(fp, com,
            (caddr_t)&payload.offset);
        if (u.u_error == 0)
            u.u_error = copyout((caddr_t)&payload.offset, uap->cmarg,
                sizeof payload.offset);
        return;
"""
DIRECT = """    case FIONREAD:
        u.u_error = (*Fops[fp->f_type]->fo_ioctl)(fp, com, uap->cmarg);
        return;
"""
CONDITIONAL_PUBLICATION = """        if (u.u_error == 0)
            u.u_error = copyout((caddr_t)&payload.offset, uap->cmarg,
                sizeof payload.offset);"""
UNCONDITIONAL_PUBLICATION = """        u.u_error = copyout(
            (caddr_t)&payload.offset, uap->cmarg, sizeof payload.offset);"""
GEOMETRY = """    if (com & (IOC_IN | IOC_OUT)) {
        u_int nbytes = (com >> 16) & IOCPARM_MASK;

        if (nbytes != 0 && baduaddr_span(uap->cmarg, nbytes)) {"""
UNMASKED_SIZE = """    if (com & (IOC_IN | IOC_OUT)) {
        u_int nbytes = (com & ~(IOC_INOUT | IOC_VOID)) >> 16;

        if (nbytes != 0 && baduaddr_span(uap->cmarg, nbytes)) {"""
ENDPOINT_SPAN = """    if (com & (IOC_IN | IOC_OUT)) {
        u_int nbytes = (com >> 16) & IOCPARM_MASK;

        if (baduaddr(uap->cmarg) ||
            baduaddr(uap->cmarg + nbytes - 1)) {"""
DIRECT_INPUT = (
    "    u.u_error = (*Fops[fp->f_type]->fo_ioctl)(fp, com, "
    "(caddr_t)&payload);"
)
DIRECT_INPUT_MUTANT = (
    "    u.u_error = (*Fops[fp->f_type]->fo_ioctl)(fp, com, uap->cmarg);"
)
COMPLETE_COPYIN = """    u.u_error = copyin(uap->cmarg, (caddr_t)&payload,
        (com >> 16) & IOCPARM_MASK);"""
SHORT_COPYIN = """    u.u_error = copyin(uap->cmarg, (caddr_t)&payload, 1);"""
CLASSIFICATION = """    switch (com) {
    case FIOCLEX:"""
MASKED_CLASSIFICATION = """    switch (com & ~(1U << 24)) {
    case FIOCLEX:"""
INOUT_PUBLICATION = """    if (u.u_error == 0 && com == TFALLOC)
        u.u_error = copyout((caddr_t)&payload.offset, uap->cmarg,
            (com >> 16) & IOCPARM_MASK);"""
UNCONDITIONAL_INOUT_PUBLICATION = """    if (com == TFALLOC)
        u.u_error = copyout((caddr_t)&payload.offset, uap->cmarg,
            (com >> 16) & IOCPARM_MASK);"""
OUTPUT_DISPATCH = """        bzero((caddr_t)&payload, sizeof payload);
        u.u_error = (*Fops[fp->f_type]->fo_ioctl)(fp, com,
            (caddr_t)&payload);
        if (u.u_error == 0)
            u.u_error = copyout((caddr_t)&payload, uap->cmarg,
                (com >> 16) & IOCPARM_MASK);
        return;"""
DIRECT_OUTPUT_DISPATCH = """        u.u_error =
            (*Fops[fp->f_type]->fo_ioctl)(fp, com, uap->cmarg);
        return;"""
OUTPUT_PUBLICATION = """        if (u.u_error == 0)
            u.u_error = copyout((caddr_t)&payload, uap->cmarg,
                (com >> 16) & IOCPARM_MASK);"""
UNCONDITIONAL_OUTPUT_PUBLICATION = """        u.u_error = copyout(
            (caddr_t)&payload, uap->cmarg,
            (com >> 16) & IOCPARM_MASK);"""
SHORT_OUTPUT_PUBLICATION = """        if (u.u_error == 0)
            u.u_error = copyout((caddr_t)&payload, uap->cmarg, 1);"""
OUTPUT_INITIALIZATION = "        bzero((caddr_t)&payload, sizeof payload);"
CORRUPT_OUTPUT_INITIALIZATION = """        bzero((caddr_t)&payload,
            sizeof payload);
        payload.partition.dp_status = 1;"""


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
            "direct-input-pointer",
            "short-input-copy",
            "masked-input-command",
            "unconditional-inout-publication",
            "omitted-inout-publication",
            "direct-output-pointer",
            "unconditional-output-publication",
            "short-output-copy",
            "omitted-output-publication",
            "corrupt-output-initialization",
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
            "                sizeof payload.offset);",
            "                sizeof(short));",
            arguments.mutation,
        )
    elif arguments.mutation == "unmasked-size":
        source = replace_once(source, GEOMETRY, UNMASKED_SIZE, arguments.mutation)
    elif arguments.mutation == "endpoint-span":
        source = replace_once(source, GEOMETRY, ENDPOINT_SPAN, arguments.mutation)
    elif arguments.mutation == "direct-input-pointer":
        source = replace_once(
            source, DIRECT_INPUT, DIRECT_INPUT_MUTANT, arguments.mutation
        )
    elif arguments.mutation == "short-input-copy":
        source = replace_once(source, COMPLETE_COPYIN, SHORT_COPYIN, arguments.mutation)
    elif arguments.mutation == "masked-input-command":
        source = replace_once(
            source, CLASSIFICATION, MASKED_CLASSIFICATION, arguments.mutation
        )
    elif arguments.mutation == "unconditional-inout-publication":
        source = replace_once(
            source,
            INOUT_PUBLICATION,
            UNCONDITIONAL_INOUT_PUBLICATION,
            arguments.mutation,
        )
    elif arguments.mutation == "omitted-inout-publication":
        source = replace_once(source, INOUT_PUBLICATION, "", arguments.mutation)
    elif arguments.mutation == "direct-output-pointer":
        source = replace_once(
            source, OUTPUT_DISPATCH, DIRECT_OUTPUT_DISPATCH, arguments.mutation
        )
    elif arguments.mutation == "unconditional-output-publication":
        source = replace_once(
            source,
            OUTPUT_PUBLICATION,
            UNCONDITIONAL_OUTPUT_PUBLICATION,
            arguments.mutation,
        )
    elif arguments.mutation == "short-output-copy":
        source = replace_once(
            source, OUTPUT_PUBLICATION, SHORT_OUTPUT_PUBLICATION, arguments.mutation
        )
    elif arguments.mutation == "omitted-output-publication":
        source = replace_once(source, OUTPUT_PUBLICATION, "", arguments.mutation)
    else:
        source = replace_once(
            source,
            OUTPUT_INITIALIZATION,
            CORRUPT_OUTPUT_INITIALIZATION,
            arguments.mutation,
        )
    arguments.output.write_text(source)


if __name__ == "__main__":
    main()
