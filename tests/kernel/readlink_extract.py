"""Extract the production readlink syscall and its calibrated mutant."""

import argparse
from pathlib import Path

from ufs_topology_extract import INCLUDES, extract_function


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--mutant", choices=("type-result", "span-check"))
    args = parser.parse_args()
    function = extract_function(args.source.read_text(), "void\nreadlink(void)", "readlink")
    if args.mutant == "type-result":
        before = """    if ((ip->i_mode&IFMT) != IFLNK) {
        u.u_error = EINVAL;
        goto out;
    }
"""
        after = """    if ((ip->i_mode&IFMT) != IFLNK) {
        u.u_error = EINVAL;
        u.u_rval = uap->count;
        goto out;
    }
"""
        if function.count(before) != 1:
            raise SystemExit("readlink invalid-type mutation anchor is not unique")
        function = function.replace(before, after, 1)
    elif args.mutant == "span-check":
        before = """    if (uap->count != 0 &&
        baduaddr_span((caddr_t)uap->buf, (u_int)uap->count)) {
        u.u_error = EFAULT;
        goto out;
    }
"""
        if function.count(before) != 1:
            raise SystemExit("readlink span mutation anchor is not unique")
        function = function.replace(before, "", 1)
    args.output.write_text(INCLUDES + "\n" + function + "\n")


if __name__ == "__main__":
    main()
