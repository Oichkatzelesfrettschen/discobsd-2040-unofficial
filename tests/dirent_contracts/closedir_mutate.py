"""Generate closedir result and ownership falsifiers from production source."""

import sys
from pathlib import Path

MUTATIONS = {
    "result_loss": ("return result;", "return result == -1 ? 0 : result;"),
    "errno_loss": ("errno = saved_errno;", "(void)saved_errno;"),
    "free_loss": ("free(dirp);", "(void)dirp;"),
    "close_repeat": (
        "result = close(dirp->dd_fd);",
        "result = close(dirp->dd_fd);\n\t(void)close(dirp->dd_fd);",
    ),
}


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: closedir_mutate.py SOURCE OUTPUT_DIRECTORY")
    source = Path(sys.argv[1]).read_text()
    output_directory = Path(sys.argv[2])
    for name, (anchor, replacement) in MUTATIONS.items():
        if source.count(anchor) != 1:
            raise SystemExit(f"{name}: mutation anchor differs")
        (output_directory / f"closedir_{name}.mutant.c").write_text(
            source.replace(anchor, replacement, 1)
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
