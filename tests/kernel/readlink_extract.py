"""Extract the production readlink syscall and its calibrated mutant."""

import argparse
from pathlib import Path

from ufs_topology_extract import INCLUDES, extract_function


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--mutant", action="store_true")
    args = parser.parse_args()
    function = extract_function(args.source.read_text(), "void\nreadlink(void)", "readlink")
    if args.mutant:
        before = "u.u_error = EINVAL;\n        goto out;"
        after = "u.u_error = EINVAL;\n        u.u_rval = uap->count;\n        goto out;"
        if function.count(before) != 1:
            raise SystemExit("readlink invalid-type mutation anchor is not unique")
        function = function.replace(before, after, 1)
    args.output.write_text(INCLUDES + "\n" + function + "\n")


if __name__ == "__main__":
    main()
