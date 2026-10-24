"""Generate known-bad file-status control implementations."""

import argparse
from pathlib import Path

DESCRIPTOR_MUTATIONS = {
    "early-fset-publication": (
        """    error = fioctl(fp, (u_int)(bit == FNONBLOCK ? FIONBIO : FIOASYNC),
        (caddr_t)&value);
    if (error)
        return (error);
    if (value)
        fp->f_flag |= bit;
    else
        fp->f_flag &= ~bit;
    return (0);
""",
        """    if (value)
        fp->f_flag |= bit;
    else
        fp->f_flag &= ~bit;
    error = fioctl(fp,
        (u_int)(bit == FNONBLOCK ? FIONBIO : FIOASYNC),
        (caddr_t)&value);
    return (error);
""",
    ),
    "early-setfl-publication": (
        """        old_flags = fp->f_flag;
        new_flags = (old_flags & ~FCNTLFLAGS) |
            ((FFLAGS(uap->arg)) & FCNTLFLAGS);
        u.u_error = fset(fp, FNONBLOCK, new_flags & FNONBLOCK);
        if (u.u_error)
            break;
        u.u_error = fset(fp, FASYNC, new_flags & FASYNC);
        if (u.u_error)
            (void)fset(fp, FNONBLOCK, old_flags & FNONBLOCK);
        else
            fp->f_flag = new_flags;
""",
        """        old_flags = fp->f_flag;
        new_flags = (old_flags & ~FCNTLFLAGS) |
            ((FFLAGS(uap->arg)) & FCNTLFLAGS);
        fp->f_flag = new_flags;
        u.u_error = fset(fp, FNONBLOCK, new_flags & FNONBLOCK);
        if (u.u_error)
            break;
        u.u_error = fset(fp, FASYNC, new_flags & FASYNC);
        if (u.u_error)
            (void)fset(fp, FNONBLOCK, old_flags & FNONBLOCK);
""",
    ),
    "zero-setfl-rollback": (
        """            (void)fset(fp, FNONBLOCK, old_flags & FNONBLOCK);
""",
        """            (void)fset(fp, FNONBLOCK, 0);
""",
    ),
}

GENERIC_MUTATIONS = {
    "direct-status-pointer": (
        (
            """    case FIONBIO:
    case FIOASYNC:
    case FIOSETOWN:
""",
            """    case FIOSETOWN:
""",
        ),
        (
            """    case FIONBIO:
    case FIOASYNC: {
        int bit = com == FIONBIO ? FNONBLOCK : FASYNC;

        u.u_error = fset(fp, bit, payload.integer);
        return;
    }
""",
        """    case FIONBIO:
        u.u_error = fset(fp, FNONBLOCK, *(int *)uap->cmarg);
        return;
    case FIOASYNC:
        u.u_error = fset(fp, FASYNC, *(int *)uap->cmarg);
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
    source = source_path.read_text(encoding="utf-8")
    for before, after in replacements:
        source = replace_once(source, before, after, arguments.mutation)
    arguments.output.write_text(
        source, encoding="utf-8"
    )


if __name__ == "__main__":
    main()
