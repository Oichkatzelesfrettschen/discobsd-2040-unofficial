# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its contributors
#   may be used to endorse or promote products derived from this software
#   without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
"""Verify descriptor ownership for shipped temporary-file users."""

import argparse
import re
from pathlib import Path

SOURCES = (
    "bin/ed/ed.c",
    "usr.bin/ld/ld.c",
    "usr.bin/diff/diffreg.c",
    "usr.bin/m4/eval.c",
    "usr.bin/m4/main.c",
    "usr.bin/m4/misc.c",
    "usr.bin/m4/extern.h",
    "bin/sh/io.c",
    "usr.bin/sort/sort.c",
    "usr.bin/find/updatedb.sh",
    "usr.bin/re/r.main.c",
    "etc/rc",
    "games/sail/sync.c",
)


def verify(source):
    problems = []

    def require(path, text, label):
        if text not in source[path]:
            problems.append("%s: missing %s" % (path, label))

    for path in SOURCES:
        if "mktemp(" in source[path]:
            problems.append("%s: retains mktemp" % path)

    require("bin/ed/ed.c", "tfile = mkstemp(tfname);", "exclusive creation")
    require("bin/ed/ed.c", "unlink(tfname) < 0", "immediate unlink")
    require("bin/ed/ed.c", "ftruncate(tfile, 0)", "descriptor reset")
    require("usr.bin/ld/ld.c", "tempflg ? tmpfile ()", "anonymous streams")
    require("usr.bin/diff/diffreg.c", "f = mkstemp(tempfile);", "exclusive creation")
    require("usr.bin/m4/eval.c", "n = mkstemp(argv[2]);", "atomic maketemp")
    require("usr.bin/m4/eval.c", "outfile[n] = tmpfile()", "anonymous diversion")
    require("usr.bin/m4/misc.c", "dfil = outfile[n];", "owned diversion read")
    require("usr.bin/m4/misc.c", "rewind(dfil);", "diversion rewind")
    for path in ("usr.bin/m4/main.c", "usr.bin/m4/misc.c", "usr.bin/m4/extern.h"):
        if "m4temp" in source[path]:
            problems.append("%s: retains named diversion state" % path)
    require("bin/sh/io.c", "O_CREAT | O_EXCL | O_RDWR", "exclusive here-doc creation")
    require("bin/sh/io.c", "fd < 0 && errno == EEXIST", "creation collision retry")
    require("bin/sh/io.c", "linked < 0 && errno == EEXIST", "link collision retry")
    require("bin/sh/io.c", "if (linked < 0)\n\t\t\tfailed", "link failure check")
    require("usr.bin/sort/sort.c", "mkdir(tempdir, 0700)", "private directory")
    require("usr.bin/sort/sort.c", 'sprintf(file, "%s/stm.", tempdir)', "private names")
    require("usr.bin/sort/sort.c", "rmdir(tempdir);", "private directory cleanup")
    require("usr.bin/find/updatedb.sh", "set -eu", "fail-closed shell mode")
    require("usr.bin/find/updatedb.sh", "set +e; sort", "sort status capture")
    require("usr.bin/find/updatedb.sh", 'mkdir "$candidate"', "private directory")
    require("usr.bin/find/updatedb.sh", "bigrams=$work/bigrams", "private files")
    require("usr.bin/re/r.main.c", "open_recovery(jname, O_RDWR)", "owned journal open")
    require("usr.bin/re/r.main.c", "flags | O_NOFOLLOW", "symlink rejection")
    require("usr.bin/re/r.main.c", "status.st_uid != (uid_t)userid", "owner check")
    require(
        "usr.bin/re/r.main.c", "O_CREAT | O_EXCL | O_RDWR | O_NOFOLLOW, 0600",
        "private creation",
    )
    require(
        "usr.bin/re/r.main.c",
        "journal = open(jname, O_CREAT | O_EXCL | O_RDWR | O_NOFOLLOW, 0600);",
        "exclusive journal creation",
    )
    require("etc/rc", "T=/etc/.motd.$", "protected motd staging parent")
    if "T=/tmp/_motd" in source["etc/rc"]:
        problems.append("etc/rc: retains shared motd staging path")
    require(
        "games/sail/sync.c",
        "O_CREAT | O_EXCL | O_RDWR | O_NOFOLLOW, 0666",
        "exclusive shared-file creation",
    )
    require(
        "games/sail/sync.c", "open(sync_file, O_RDWR | O_NOFOLLOW)",
        "symlink-safe shared-file reopen",
    )
    require("games/sail/sync.c", "S_ISREG(status.st_mode)", "regular file check")
    require(
        "games/sail/sync.c",
        "!sync_record_valid(type, isstr, a, b, c, d)",
        "shared-record admission",
    )
    require(
        "games/sail/sync.c", "if (shipnum < 0 || shipnum >= cc->vessels)",
        "shared-record ship domain",
    )
    require("games/sail/sync.c", '"%9d%9d%9d"', "bounded record header")
    require("games/sail/sync.c", '"%9d%9d%9d%9d"', "bounded record payload")
    sail = source["games/sail/sync.c"]
    validator = sail[sail.index("sync_record_valid"):sail.index("makesignal")]
    updater = sail[sail.index("static int\nsync_update("):sail.index("void\nWrite(")]
    validator_cases = set(re.findall(r"case (W_[A-Z0-9]+):", validator))
    updater_cases = set(re.findall(r"case (W_[A-Z0-9]+):", updater))
    if validator_cases != updater_cases:
        problems.append("games/sail/sync.c: record validator and updater differ")
    return problems


def calibrate(source):
    mutations = (
        (
            "ed-mktemp", "bin/ed/ed.c", "tfile = mkstemp(tfname);",
            "tfile = creat(mktemp(tfname), 0600);",
        ),
        (
            "ld-named", "usr.bin/ld/ld.c", "tempflg ? tmpfile ()",
            "tempflg ? fopen (\"/tmp/ld\", \"w+\")",
        ),
        (
            "diff-creat", "usr.bin/diff/diffreg.c", "f = mkstemp(tempfile);",
            "f = creat(tempfile, 0600);",
        ),
        (
            "m4-maketemp", "usr.bin/m4/eval.c", "n = mkstemp(argv[2]);",
            "n = close((int)mktemp(argv[2]));",
        ),
        (
            "m4-named", "usr.bin/m4/eval.c", "outfile[n] = tmpfile()",
            "outfile[n] = fopen(\"/tmp/m4.0\", \"w+\")",
        ),
        ("sh-truncate", "bin/sh/io.c", "O_CREAT | O_EXCL | O_RDWR", "O_CREAT | O_RDWR"),
        ("sh-link-ignore", "bin/sh/io.c", "if (linked < 0)\n\t\t\tfailed", "if (0)\n\t\t\tfailed"),
        ("sort-shared", "usr.bin/sort/sort.c", "mkdir(tempdir, 0700)", "creat(tempdir, 0600)"),
        (
            "updatedb-shared", "usr.bin/find/updatedb.sh", "bigrams=$work/bigrams",
            "bigrams=/tmp/f.bigrams$$",
        ),
        ("re-follow", "usr.bin/re/r.main.c", "flags | O_NOFOLLOW", "flags"),
        (
            "re-truncate", "usr.bin/re/r.main.c",
            "journal = open(jname, O_CREAT | O_EXCL | O_RDWR | O_NOFOLLOW, 0600);",
            "journal = open(jname, O_CREAT | O_TRUNC | O_RDWR, 0664);",
        ),
        ("rc-shared", "etc/rc", "T=/etc/.motd.$", "T=/tmp/_motd"),
        (
            "sail-follow", "games/sail/sync.c",
            "O_CREAT | O_EXCL | O_RDWR | O_NOFOLLOW, 0666",
            "O_CREAT | O_TRUNC | O_RDWR, 0666",
        ),
        (
            "sail-record", "games/sail/sync.c",
            "!sync_record_valid(type, isstr, a, b, c, d) ||\n\t\t    ",
            "",
        ),
        (
            "sail-ship-domain", "games/sail/sync.c",
            "if (shipnum < 0 || shipnum >= cc->vessels)", "if (0)",
        ),
        (
            "sail-integer", "games/sail/sync.c",
            '"%9d%9d%9d"', '"%d%d%d"',
        ),
    )
    for name, path, before, after in mutations:
        if source[path].count(before) != 1:
            raise ValueError("calibration %s did not select one source span" % name)
        mutant = dict(source)
        mutant[path] = source[path].replace(before, after)
        if not verify(mutant):
            raise ValueError("calibration %s survived" % name)
        print("calibration: %s rejected" % name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    source = {path: (args.root / path).read_text() for path in SOURCES}
    if args.self_test:
        calibrate(source)
    problems = verify(source)
    if problems:
        for problem in problems:
            print("FAIL tempfile ownership: %s" % problem)
        return 1
    print("PASS tempfile ownership: 13 source files retain exclusive objects")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
