"""Check ls's permission renderer and account-name cache contracts."""

# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
# Neither the name of the copyright holder nor the names of its contributors
# may be used to endorse or promote products derived from this software
# without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

PREAMBLE = """\
#define _XOPEN_SOURCE 700
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
"""

MODE_ORACLE = """\
int
main(void)
{
    static const char triplets[8][4] = {
        "---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx"
    };
    static const unsigned high[] = {0, 0100000U, UINT_MAX & ~07777U};
    for (unsigned mode = 0; mode <= 07777U; ++mode) {
        char expected[9];
        memcpy(expected, triplets[(mode >> 6) & 7U], 3);
        memcpy(expected + 3, triplets[(mode >> 3) & 7U], 3);
        memcpy(expected + 6, triplets[mode & 7U], 3);
        if (mode & 04000U)
            expected[2] = expected[2] == 'x' ? 's' : 'S';
        if (mode & 02000U)
            expected[5] = expected[5] == 'x' ? 's' : 'S';
        if (mode & 01000U)
            expected[8] = expected[8] == 'x' ? 't' : 'T';
        for (unsigned variant = 0; variant < sizeof high / sizeof high[0]; ++variant) {
            char result[11];
            memset(result, '#', sizeof result);
            if (fmtmode(result + 1, mode | high[variant]) != result + 10 ||
                result[0] != '#' || result[10] != '#' ||
                memcmp(result + 1, expected, sizeof expected) != 0) {
                fprintf(stderr, "permission mismatch at mode %04o, variant %u\\n", mode, variant);
                return 1;
            }
        }
    }
    return 0;
}
"""

ACCOUNT_STUBS = """\
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>

static unsigned pw_reads, gr_reads, keep_calls;
static char user_long[] = "owner-1234567890", group_long[] = "group-1234567890";
static char user_short[] = "uid65", group_short[] = "gid65";

static int
test_setpassent(int keep)
{
    keep_calls += keep == 1;
    return 1;
}

static struct passwd *
test_getpwuid(uid_t uid)
{
    static struct passwd pw;
    ++pw_reads;
    if (uid != 1 && uid != 65)
        return NULL;
    pw.pw_name = uid == 1 ? user_long : user_short;
    return &pw;
}

static struct group *
test_getgrgid(gid_t gid)
{
    static struct group gr;
    ++gr_reads;
    if (gid != 1 && gid != 65)
        return NULL;
    gr.gr_name = gid == 1 ? group_long : group_short;
    return &gr;
}

#define setpassent test_setpassent
#define getpwuid test_getpwuid
#define getgrgid test_getgrgid
"""

ACCOUNT_ORACLE = """\
static int
name_is(const char *actual, const char *expected)
{
    return actual != NULL && strcmp(actual, expected) == 0;
}

int
main(void)
{
    if (!name_is(getname(1), "owner-123") || !name_is(getgroup(1), "group-123") ||
        !name_is(getname(1), "owner-123") || !name_is(getgroup(1), "group-123") ||
        pw_reads != 1 || gr_reads != 1 || keep_calls != 2) {
        fputs("account cache mismatch on truncation or hit\\n", stderr);
        return 1;
    }
    if (getname(129) != NULL || getgroup(129) != NULL ||
        !name_is(getname(1), "owner-123") || !name_is(getgroup(1), "group-123") ||
        pw_reads != 2 || gr_reads != 2) {
        fputs("account cache mismatch on absent account\\n", stderr);
        return 1;
    }
    if (!name_is(getname(65), "uid65") || !name_is(getgroup(65), "gid65") ||
        !name_is(getname(65), "uid65") || !name_is(getgroup(65), "gid65") ||
        !name_is(getname(1), "owner-123") || !name_is(getgroup(1), "group-123") ||
        pw_reads != 4 || gr_reads != 4 || keep_calls != 7) {
        fputs("account cache mismatch on colliding IDs\\n", stderr);
        return 1;
    }
    return 0;
}
"""


def run_cases(binary, compiler, implementation, preamble, oracle, original, mutation, message):
    if implementation.count(original) != 1:
        raise ValueError(f"mutation anchor: {original}")
    for code, status in ((implementation, 0), (implementation.replace(original, mutation), 1)):
        subprocess.run(
            compiler + ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
                        "-Wstrict-prototypes", "-Wold-style-definition", "-x", "c",
                        "-", "-o", str(binary)],
            input=PREAMBLE + preamble + code + oracle, text=True, check=True,
        )
        result = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
        if result.returncode != status or (status and message not in result.stderr):
            raise RuntimeError(f"unexpected ls format verdict {result.returncode}: {result.stderr}")


def main():
    source = (Path(__file__).resolve().parents[1] / "ls.c").read_text()
    start = source.index("static char *\nfmtmode(")
    end = source.index("\n}\n", start) + len("\n}\n")
    renderer = source[start:end]
    cache = source[source.index("#define ACCOUNT_NAME_BYTES "):]
    compiler = shlex.split(os.environ.get("HOST_CC", "cc"))
    with tempfile.TemporaryDirectory(prefix="discobsd-ls-format-") as temp:
        binary = Path(temp) / "ls-format"
        run_cases(binary, compiler, renderer, "", MODE_ORACLE,
                  "special == S_ISVTX", "special == S_ISGID", "permission mismatch")
        run_cases(binary, compiler, cache, ACCOUNT_STUBS, ACCOUNT_ORACLE,
                  "#define ACCOUNT_NAME_BYTES 9", "#define ACCOUNT_NAME_BYTES 8",
                  "account cache mismatch on truncation")
    print("PASS ls format: 12,288 permission cases, account-cache contracts, "
          "two rejected mutations")


if __name__ == "__main__":
    main()
