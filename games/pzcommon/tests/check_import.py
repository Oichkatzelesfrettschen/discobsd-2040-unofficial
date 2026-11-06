#!/usr/bin/env python3
# Verify that the imported Simon Tatham sources under games/pzcommon/tatham
# match the SHA-256 sums recorded in games/pzcommon/tatham.sha256 (the
# snapshot named in NOTICE), then calibrate the check: a copy with one byte
# changed and a copy with a file missing must each be rejected.
#
# usage: check_import.py [pzcommon-dir]

import hashlib
import os
import shutil
import sys
import tempfile


def sums(path):
    out = {}
    with open(path) as f:
        for line in f:
            digest, name = line.split()
            out[name] = digest
    return out


def verify(root):
    """Return the list of problems with the tatham directory under root."""
    problems = []
    recorded = sums(os.path.join(root, "tatham.sha256"))
    present = set(os.listdir(os.path.join(root, "tatham")))
    for name in sorted(set(recorded) | present):
        if name not in recorded:
            problems.append("unrecorded file " + name)
        elif name not in present:
            problems.append("missing file " + name)
        else:
            with open(os.path.join(root, "tatham", name), "rb") as f:
                if hashlib.sha256(f.read()).hexdigest() != recorded[name]:
                    problems.append("changed file " + name)
    return problems


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    problems = verify(root)
    if problems:
        print("FAIL imported sources: " + "; ".join(problems))
        return 1
    with tempfile.TemporaryDirectory() as work:
        for name, change in (("changed", True), ("missing", False)):
            copy = os.path.join(work, name)
            shutil.copytree(os.path.join(root, "tatham"), os.path.join(copy, "tatham"))
            shutil.copy(os.path.join(root, "tatham.sha256"), copy)
            target = os.path.join(copy, "tatham", "flood.c")
            if change:
                with open(target, "ab") as f:
                    f.write(b" ")
            else:
                os.remove(target)
            if not verify(copy):
                print("FAIL the check accepted a %s copy" % name)
                return 1
    print(
        "imported sources: %d files match tatham.sha256; changed and missing copies rejected"
        % len(sums(os.path.join(root, "tatham.sha256")))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
