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
        """    case FIOSETOWN: {
        int owner;

        u.u_error = copyin(uap->cmarg, (caddr_t)&owner, sizeof owner);
        if (u.u_error == 0)
            u.u_error = fsetown(fp, owner);
        return;
    }
    case FIOGETOWN: {
        int owner;

        u.u_error = fgetown(fp, &owner);
        if (u.u_error == 0)
            u.u_error = copyout((caddr_t)&owner, uap->cmarg,
                sizeof owner);
        return;
    }
""",
        """    case FIOSETOWN:
        u.u_error = fsetown(fp, *(int *)uap->cmarg);
        return;
    case FIOGETOWN:
        u.u_error = fgetown(fp, (int *)uap->cmarg);
        return;
""",
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
    else:
        source_path = arguments.generic_source
        before, after = GENERIC_MUTATIONS[arguments.mutation]
    source = source_path.read_text()
    arguments.output.write_text(
        replace_once(source, before, after, arguments.mutation)
    )


if __name__ == "__main__":
    main()
