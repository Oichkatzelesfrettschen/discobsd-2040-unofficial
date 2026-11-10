"""Write a copy of a source file with one exact replacement applied.

A replacement that matches zero or several sites is an error, so a mutant
never silently equals its source.
"""

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: mutate.py SOURCE OUTPUT OLD NEW", file=sys.stderr)
        return 2
    source, output, old, new = sys.argv[1:]
    text = Path(source).read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        print(f"mutate.py: {source}: {count} matches for {old!r}",
              file=sys.stderr)
        return 1
    Path(output).write_text(text.replace(old, new), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
