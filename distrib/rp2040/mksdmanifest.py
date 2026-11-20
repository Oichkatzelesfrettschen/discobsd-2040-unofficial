#!/usr/bin/env python3
"""Extend a composed root manifest with every other file of the install tree.

Usage: mksdmanifest.py MANIFEST DESTDIR OUTPUT

The PicoCalc's SD root (tools/picocalc/mksdroot.sh) defaults to 128 MiB where the
flash root has 979 KB, so it carries the whole install tree rather than the
flash profile's selection. MANIFEST is the composed flash manifest
(distrib/rp2040/_manifest: the profile's entries plus md.rp2040), which keeps
its owners, modes, setuid bits, links and device nodes. OUTPUT is MANIFEST
followed by one entry for each directory, regular file and symbolic link of
DESTDIR that MANIFEST does not name, owned by root (owner 0, group 0) with
mode 0775 for a directory or an executable and 0664 otherwise. The
install tree's own owners and setuid bits are not copied: a build by an
ordinary user records that user, and only MANIFEST grants setuid.
"""

import os
import stat
import sys
import tempfile

NAMING = ("dir", "file", "pack", "link", "symlink", "cdev", "bdev")


def named_paths(lines):
    paths = set()
    for line in lines:
        words = line.split(maxsplit=1)
        if len(words) == 2 and words[0] in NAMING:
            paths.add(words[1])
    return paths


def manifest_line(kind, value):
    line = f"{kind} {value}"
    # fsutil reads ASCII records into a 1024-byte buffer and strips trailing blanks.
    if not line.isascii() or not line.isprintable() or line.endswith(" ") or len(line) > 1022:
        raise ValueError(f"unrepresentable {kind} argument: {value!r}")
    return line


def scan_error(error):
    raise error


def extras(destdir, named):
    out = []
    # The base manifest owns the representation and permissions of its entries.
    # Only the additions have the uniform metadata derived from the install tree.
    inodes = {}
    for root, dirs, files in os.walk(destdir, onerror=scan_error):
        dirs.sort()
        rel = "/" + os.path.relpath(root, destdir) if root != destdir else ""
        for d in dirs:
            path = f"{rel}/{d}"
            if os.path.islink(os.path.join(root, d)):
                files.append(d)
                continue
            if path not in named:
                out.append(("dir", path, None))
        for f in sorted(files):
            path = f"{rel}/{f}"
            if path in named:
                continue
            full = os.path.join(root, f)
            st = os.lstat(full)
            if stat.S_ISLNK(st.st_mode):
                out.append(("symlink", path, os.readlink(full)))
            elif stat.S_ISREG(st.st_mode):
                if st.st_nlink > 1:
                    identity = (st.st_dev, st.st_ino)
                    if identity in inodes:
                        out.append(("link", path, inodes[identity]))
                        continue
                    inodes[identity] = path
                mode = "0775" if st.st_mode & 0o111 else "0664"
                out.append(("file", path, mode))
            else:
                raise SystemExit(f"mksdmanifest: {path}: not a file, directory or link")
    return out


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    manifest, destdir, output = argv
    with open(manifest, encoding="ascii") as f:
        lines = f.read().splitlines()
    named = named_paths(lines)
    added = extras(destdir, named)
    body = lines + [
        "",
        "# Added by mksdmanifest.py: the rest of the install tree.",
        "default",
        "owner 0",
        "group 0",
        "dirmode 0775",
    ]
    for kind, path, arg in added:
        body.append(manifest_line(kind, path))
        if kind == "file":
            body.append(f"mode {arg}")
        elif kind in ("link", "symlink"):
            body.append(manifest_line("target", arg))
    with tempfile.TemporaryDirectory(dir=os.path.dirname(os.path.abspath(output))) as work:
        tmp = os.path.join(work, "manifest")
        with open(tmp, "w", encoding="ascii") as f:
            f.write("\n".join(body) + "\n")
        os.replace(tmp, output)
    print(f"mksdmanifest: {len(named)} named, {len(added)} added")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
