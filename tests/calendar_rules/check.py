#!/usr/bin/env python3
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

"""Check the leap-year rule of date and basic and the year chpass prints.

The production functions are cut out of their sources and compiled on the
host: fmttime() from bin/date2/date.c (shipped as /bin/date),
date_string() from usr.bin/basic/basic.c and ttoa() from
usr.bin/chpass/util.c. February's length (%t) and the leap flag (%L) must
follow the Gregorian rule for 1900, 2000, 2023, 2024, 2100, 2104 and 2400,
and ttoa() must print the full year. --selftest restores each earlier
defect (a year divisible by 4 is a leap year, in each program and in the
port's tzfile.h; chpass prints "19" before the years since 1900) and
requires the check to reject it.

usage: check.py ROOT CC
       check.py --selftest ROOT CC
"""

import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

YEARS = (1900, 2000, 2023, 2024, 2100, 2104, 2400)
DATE = "bin/date2/date.c"
BASIC = "usr.bin/basic/basic.c"
CHPASS = "usr.bin/chpass/util.c"


def gregorian(year):
    return year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)


def function(source, signature, optional=False):
    """Return the definition that starts with signature, through its brace."""
    if optional and signature not in source:
        return ""
    start = source.find(signature)
    if start < 0 or source.find(signature, start + 1) >= 0:
        raise SystemExit(f"calendar rules: {signature!r} occurs {source.count(signature)} times")
    depth = 0
    for i in range(source.find("{", start), len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start : i + 1]
    raise SystemExit(f"calendar rules: {signature!r} has no closing brace")


def tables(source, names):
    lines = []
    for name in names:
        match = re.search(rf"^[^\n]*\b{name}\[\][^;]*;", source, re.M | re.S)
        if match is None:
            raise SystemExit(f"calendar rules: table {name} not found")
        lines.append(match.group(0))
    return "\n".join(lines)


PREAMBLE = """#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
"""
TZFILE = "include/tzfile.h"


def tzfile_macros(root):
    """Return the isleap and TM_YEAR_BASE definitions of the port's tzfile.h."""
    text = (root / TZFILE).read_text(encoding="latin-1")
    lines = []
    for name in ("TM_YEAR_BASE", "isleap"):
        match = re.search(rf"^#define\s+{name}\b.*$", text, re.M)
        if match is None:
            raise SystemExit(f"calendar rules: {TZFILE} lacks {name}")
        lines.append(match.group(0))
    return "\n".join(lines)


DRIVER = r"""
static struct tm fixed;
struct tm *test_localtime(const time_t *t) { (void)t; return &fixed; }
int
main(void)
{
    static const int years[] = {YEARS};
    char line[64];
    for (unsigned i = 0; i < sizeof years / sizeof years[0]; i++) {
        memset(&fixed, 0, sizeof fixed);
        fixed.tm_year = years[i] - 1900;
        fixed.tm_mon = 1;
        fixed.tm_mday = 1;
        printf("%d date ", years[i]);
        fflush(stdout);
        fmttime("%t %L", &fixed);
        date_string("%t", line);
        printf(" basic %s", line);
        date_string("%L", line);
        printf(" %s\n", line);
    }
    printf("chpass %s\n", ttoa((time_t)1781481600));
    printf("chpass %s\n", ttoa((time_t)946598400));
    return 0;
}
"""


def program(root, overrides):
    date = overrides.get(DATE, (root / DATE).read_text(encoding="latin-1"))
    basic = overrides.get(BASIC, (root / BASIC).read_text(encoding="latin-1"))
    chpass = overrides.get(CHPASS, (root / CHPASS).read_text(encoding="latin-1"))
    date_tables = tables(date, ("wday_s", "wday_l", "month_l", "month_s"))
    basic_fn = function(basic, "void date_string(char *format, char *ptr)")
    ttoa = function(chpass, "ttoa(tval)")
    ttoa = "char *\nttoa(time_t tval)\n" + ttoa[ttoa.index("{") :]
    return "\n".join(
        (
            PREAMBLE,
            overrides.get(TZFILE) or tzfile_macros(root),
            "static " + date_tables.replace("\nconst", "\nstatic const"),
            "#define wday_s basic_wday_s\n#define wday_l basic_wday_l",
            "#define month_s basic_month_s\n#define month_l basic_month_l",
            "static "
            + tables(basic, ("wday_s", "wday_l", "month_l", "month_s"))
            .replace("\nconst", "\nstatic const")
            .replace("\nchar", "\nstatic char"),
            "#undef wday_s\n#undef wday_l\n#undef month_s\n#undef month_l",
            function(date, "static int\nleapyear(int tm_year)", optional=True),
            function(date, "void fmttime(char *fmt, struct tm *tm)"),
            "#define wday_s basic_wday_s\n#define wday_l basic_wday_l",
            "#define month_s basic_month_s\n#define month_l basic_month_l",
            "#define localtime test_localtime\nstruct tm *test_localtime(const time_t *);",
            basic_fn,
            "#undef localtime",
            tables(chpass, ("months",)),
            ttoa,
            DRIVER.replace("YEARS", ", ".join(str(y) for y in YEARS)),
        )
    )


def run(root, compiler, overrides=None):
    """Return the list of failures for the sources, with overrides applied."""
    text = program(root, overrides or {})
    with tempfile.TemporaryDirectory(prefix="calendar-rules-") as temporary:
        source = Path(temporary) / "calendar.c"
        binary = Path(temporary) / "calendar"
        source.write_text(text, encoding="latin-1")
        subprocess.run(
            compiler + ["-std=gnu17", "-w", "-o", str(binary), str(source)],
            check=True,
        )
        output = subprocess.run(
            [str(binary)],
            capture_output=True,
            text=True,
            check=True,
            env={"TZ": "UTC0"},
        ).stdout.splitlines()
    failures = []
    for year, line in zip(YEARS, output):
        days, leap = (29, 1) if gregorian(year) else (28, 0)
        want = f"{year} date {days} {leap} basic {days} {leap}"
        if line != want:
            failures.append(f"February {year}: {line!r}, want {want!r}")
    want = ["chpass June 15, 2026", "chpass December 31, 1999"]
    if output[len(YEARS) :] != want:
        failures.append(f"chpass years: {output[len(YEARS) :]!r}, want {want!r}")
    return failures


MUTANTS = (
    (
        "tzfile divides by 4",
        TZFILE,
        None,
        "#define TM_YEAR_BASE 1900\n#define isleap(y) (((y) % 4) == 0)",
        "February 1900",
    ),
    (
        "date helper divides by 4",
        DATE,
        "return (isleap(tm_year + TM_YEAR_BASE));",
        "return ((tm_year + 1900) % 4 == 0);",
        "February 2100",
    ),
    (
        "date length divides by 4",
        DATE,
        "leapyear(tm->tm_year) ? 29 : 28",
        "((tm->tm_year+1900) % 4 == 0) ? 29 : 28",
        "February 2100",
    ),
    (
        "date flag divides by 4",
        DATE,
        "leapyear(tm->tm_year) ? 1 : 0",
        "((tm->tm_year+1900) % 4 == 0) ? 1 : 0",
        "February 2100",
    ),
    (
        "basic divides by 4",
        BASIC,
        "isleap(tm->tm_year+TM_YEAR_BASE) ? 29 : 28",
        "((tm->tm_year+1900) % 4 == 0) ? 29 : 28",
        "February 1900",
    ),
    (
        "chpass prints 19",
        CHPASS,
        '"%s %d, %d", months[tp->tm_mon],\n\t\t    tp->tm_mday, tp->tm_year + 1900);',
        '"%s %d, 19%d", months[tp->tm_mon],\n\t\t    tp->tm_mday, tp->tm_year);',
        "chpass years",
    ),
)


def main(argv):
    selftest = argv[:1] == ["--selftest"]
    if selftest:
        argv = argv[1:]
    if len(argv) != 2:
        raise SystemExit("usage: check.py [--selftest] ROOT CC")
    root = Path(argv[0]).resolve()
    compiler = shlex.split(argv[1])
    if selftest:
        for name, path, anchor, replacement, reason in MUTANTS:
            source = (root / path).read_text(encoding="latin-1")
            if anchor is None:
                override = replacement
            elif source.count(anchor) != 1:
                raise SystemExit(
                    f"calendar rules: mutant {name}: anchor occurs {source.count(anchor)} times"
                )
            else:
                override = source.replace(anchor, replacement)
            failures = run(root, compiler, {path: override})
            if not any(failure.startswith(reason) for failure in failures):
                raise SystemExit(
                    f"calendar rules: mutant {name} was not rejected for {reason}: {failures}"
                )
            print(f"calendar rules: mutant {name} rejected ({reason})")
        return 0
    failures = run(root, compiler)
    for failure in failures:
        print(f"calendar rules: {failure}", file=sys.stderr)
    if failures:
        return 1
    print(f"calendar rules: February in {len(YEARS)} years and two chpass dates match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
