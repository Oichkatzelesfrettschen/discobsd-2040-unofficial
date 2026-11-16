"""Create a formatter mutant that drops the log-to-console replay."""

import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    before = "prf (fmt, console_arguments, TOCONS, (struct tty *)0);"
    after = 'prf ("", console_arguments, TOCONS, (struct tty *)0);'
    if source.count(before) != 1:
        raise SystemExit("log-to-console mutation anchor is not unique")
    args.output.write_text(source.replace(before, after, 1), encoding="utf-8")


if __name__ == "__main__":
    main()
