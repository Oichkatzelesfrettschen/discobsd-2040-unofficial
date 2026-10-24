import argparse
from pathlib import Path

DESCRIPTOR_MUTATIONS = {
    "unchecked-negation": (
        """    } else {
        if (value < -SHRT_MAX)
            return (EINVAL);
        value = -value;
    }
""",
        """    } else
        value = -value;
""",
    ),
    "unconditional-publication": (
        """    error = fioctl(fp, (u_int)TIOCGPGRP, (caddr_t)&value);
    if (error)
        return (error);
    if (!PGRP_VALID(value))
        return (EINVAL);
    *valuep = -value;
    return (0);
""",
        """    error = fioctl(fp, (u_int)TIOCGPGRP, (caddr_t)&value);
    *valuep = -value;
    return (error);
""",
    ),
}

GENERIC_MUTATIONS = {
    "direct-user-pointer": (
        (
            """    case FIONBIO:
    case FIOASYNC:
    case FIOSETOWN:
    case TIOCSETD:
""",
            """    case FIONBIO:
    case FIOASYNC:
    case TIOCSETD:
""",
        ),
        (
            """    case FIOSETOWN:
        u.u_error = fsetown(fp, input.integer);
        return;
""",
            """    case FIOSETOWN:
        u.u_error = fsetown(fp, *(int *)uap->cmarg);
        return;
""",
        ),
        (
            """    case FIOGETOWN:
        u.u_error = fgetown(fp, &input.integer);
        if (u.u_error == 0)
            u.u_error = copyout((caddr_t)&input.integer, uap->cmarg,
                sizeof input.integer);
        return;
""",
            """    case FIOGETOWN:
        u.u_error = fgetown(fp, (int *)uap->cmarg);
        return;
""",
        ),
    ),
}


def replace_once(source: str, before: str, after: str, mutation: str) -> str:
    if source.count(before) != 1:
        raise SystemExit(f"{mutation}: expected one production pattern")
    return source.replace(before, after, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--descriptor-source", type=Path, required=True)
    parser.add_argument("--generic-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--mutation",
        choices=sorted(DESCRIPTOR_MUTATIONS | GENERIC_MUTATIONS),
        required=True,
    )
    arguments = parser.parse_args()

    if arguments.mutation in DESCRIPTOR_MUTATIONS:
        source_path = arguments.descriptor_source
        before, after = DESCRIPTOR_MUTATIONS[arguments.mutation]
        replacements = ((before, after),)
    else:
        source_path = arguments.generic_source
        replacements = GENERIC_MUTATIONS[arguments.mutation]
    source = source_path.read_text()
    for before, after in replacements:
        source = replace_once(source, before, after, arguments.mutation)
    arguments.output.write_text(
        source
    )


if __name__ == "__main__":
    main()
