"""Generate opendir admission and cleanup falsifiers from production source."""

import sys
from pathlib import Path

MUTATIONS = {
    "directory_type": (
        "if (!S_ISDIR(status.st_mode))", "if (0)"
    ),
    "close_on_exec": (
        "if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) == -1)", "if (0)"
    ),
    "fcntl_error_loss": (
        "if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) == -1)",
        "if (fcntl(descriptor, F_SETFD, FD_CLOEXEC) == 2)",
    ),
    "descriptor_cleanup": (
        "(void)close(descriptor);", "(void)descriptor;"
    ),
    "cleanup_errno_loss": (
        "errno = saved_errno;", "(void)saved_errno;"
    ),
}


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: opendir_mutate.py SOURCE OUTPUT_DIRECTORY")
    source = Path(sys.argv[1]).read_text()
    output_directory = Path(sys.argv[2])
    for name, (anchor, replacement) in MUTATIONS.items():
        if source.count(anchor) != 1:
            raise SystemExit(f"{name}: mutation anchor differs")
        (output_directory / f"opendir_{name}.mutant.c").write_text(
            source.replace(anchor, replacement, 1)
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
