# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
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
"""Run cpu.c against a whole SingleStepTests suite.

    sst.py SSTRUN 8088 DIR [--limit N] [--only FORM,...]
    sst.py SSTRUN 80286 DIR [--limit N] [--only FORM,...]

The samples of this directory hold a few vectors of each instruction form;
this driver runs every vector of every documented form, or the first N of
each. DIR is what mksample.py takes for the same suite, and the vectors,
the flag masks and the classes left out by rule are mksample.py's: a vector
that the sample would hold is compared here in the same way. The suites are
not part of this tree (the 8088 one is about 800 MB).

Exit status 0 when no documented form fails a vector.
"""

import concurrent.futures
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mksample  # noqa: E402

MODEL = {"8088": "0", "80286": "2"}


def run_form(job):
    sstrun, suite, src, entry, name, meta, limit = job
    tests = mksample.load(suite, src, entry)
    if limit:
        tests = tests[:limit]
    vectors = []
    skipped = 0
    for test in tests:
        form = mksample.form_of(meta, name, test)
        if form.get("status", "normal") != "normal":
            return name, 0, 0, 0, None
        line = mksample.line_of(suite, name, form, test)
        if line is None:
            skipped += 1
            continue
        vectors.append((test, line.split("|")))
    lines = []
    for _, (_, before, _, store, _, compare) in vectors:
        pairs = [int(x, 16) for x in store.split()]
        triples = [int(x, 16) for x in compare.split()]
        parts = [int(x, 16) for x in before.split()]
        parts += [len(pairs) // 2] + pairs + [len(triples) // 3] + triples[0::3]
        lines.append(" ".join(str(x) for x in parts))
    done = subprocess.run(
        [sstrun, MODEL[suite]],
        input="\n".join(lines) + "\n",
        capture_output=True,
        text=True,
        check=False,
    )
    replies = done.stdout.splitlines()
    if done.returncode != 0 or len(replies) != len(vectors):
        return name, len(vectors), len(vectors), skipped, "sstrun exit %d" % done.returncode
    failed = 0
    first = None
    for (test, (_, _, mask, _, after, compare)), reply in zip(vectors, replies):
        got = [int(x) for x in reply.split()]
        want = [int(x, 16) for x in after.split()]
        mask = int(mask, 16)
        got[13] &= mask
        want[13] &= mask
        triples = [int(x, 16) for x in compare.split()]
        memory = all(
            (have ^ value) & m == 0
            for have, value, m in zip(got[14:], triples[1::3], triples[2::3])
        )
        if got[:14] != want or not memory:
            failed += 1
            if first is None:
                first = "%s [%s]" % (
                    test.get("name", "?"),
                    " ".join("%02x" % b for b in test["bytes"]),
                )
    return name, len(vectors), failed, skipped, first


def main(argv):
    if len(argv) < 4 or argv[2] not in MODEL:
        sys.stderr.write(__doc__)
        return 2
    sstrun, suite, src = argv[1:4]
    limit, only = 0, None
    rest = argv[4:]
    while rest:
        if len(rest) > 1 and rest[0] == "--limit":
            limit = int(rest[1])
        elif len(rest) > 1 and rest[0] == "--only":
            only = set(rest[1].upper().split(","))
        else:
            sys.stderr.write(__doc__)
            return 2
        rest = rest[2:]
    where = src if suite == "8088" else os.path.join(src, "v1_real_mode")
    suffix = ".json.gz" if suite == "8088" else ".MOO.gz"
    meta = mksample.metadata(where)
    jobs = []
    for entry in sorted(os.listdir(where)):
        if not entry.endswith(suffix):
            continue
        name = entry[: -len(suffix)]
        if only and name not in only and name.split(".")[0] not in only:
            continue
        jobs.append((sstrun, suite, src, entry, name, meta, limit))
    if not jobs:
        sys.stderr.write("sst: no vectors in %s\n" % where)
        return 2
    total = bad = left_out = 0
    with concurrent.futures.ProcessPoolExecutor() as pool:
        for name, count, failed, skipped, first in pool.map(run_form, jobs):
            total += count
            left_out += skipped
            if failed:
                bad += 1
                print("%-6s %5d of %d fail, first %s" % (name, failed, count, first))
    print(
        "sst %s: %d vectors of documented forms run, %d left out by rule, "
        "%d forms with a failing vector" % (suite, total, left_out, bad)
    )
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
