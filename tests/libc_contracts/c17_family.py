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
"""Execute one family of C17 library members with named faulty providers.

Each family names the tree sources it compiles under tree_ names, the fixture
that calls them through volatile pointers, the exact line a passing run
prints, and mutants. A mutant edits a private copy of one source and must
make the fixture exit with the stated status and print "FAIL <member>", so a
compiler failure or an unrelated crash is not counted as a rejection.
"""

import argparse
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

import netbsd_oracle

# A distribution compiler may enable _FORTIFY_SOURCE by default; its checks
# would abort a faulty provider before the fixture names the failure.
BASE = ["-std=c17", "-pedantic-errors", "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-O2"]
BASE += ["-U_FORTIFY_SOURCE", "-D_FORTIFY_SOURCE=0"]
# newlib's <ctype.h> macros index a table, which the inherited strtod.c
# reaches with a plain char.
ARM = ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft", "-Os", "-Wno-char-subscripts"]

# name: (sources, renamed symbols, fixture, pass line, link libraries, mutants)
# mutant: (source, anchor, replacement, exit status, member, description)
FAMILIES = {
    "locale": (
        (
            "lib/libc/gen/setlocale.c",
            "lib/libc/gen/localeconv.c",
            "lib/libc/string/strcoll.c",
            "lib/libc/string/strxfrm.c",
        ),
        ("setlocale", "localeconv", "strcoll", "strxfrm"),
        "c17_locale_test.c",
        "c17 locale: 508 checks passed\n",
        (),
        (
            (
                "lib/libc/gen/setlocale.c",
                'strcmp(locale, "POSIX") != 0)',
                "0)",
                1,
                "setlocale",
                "setlocale accepts every name",
            ),
            (
                "lib/libc/gen/setlocale.c",
                "\tdefault:\n\t\treturn NULL;",
                "\tdefault:\n\t\tbreak;",
                1,
                "setlocale",
                "setlocale accepts every category",
            ),
            (
                "lib/libc/gen/localeconv.c",
                '.decimal_point = "."',
                '.decimal_point = ","',
                2,
                "localeconv",
                "localeconv reports a comma radix",
            ),
            (
                "lib/libc/gen/localeconv.c",
                ".n_sign_posn = CHAR_MAX",
                ".n_sign_posn = 0",
                2,
                "localeconv",
                "localeconv reports an available sign position",
            ),
            (
                "lib/libc/string/strcoll.c",
                "strcmp(left, right)",
                "strcmp(right, left)",
                3,
                "strcoll",
                "strcoll reverses the order",
            ),
            (
                "lib/libc/string/strxfrm.c",
                "if (length < size)",
                "if (length <= size)",
                4,
                "strxfrm",
                "strxfrm writes the terminator past the size",
            ),
            (
                "lib/libc/string/strxfrm.c",
                "return length;",
                "return length < size ? length : size;",
                4,
                "strxfrm",
                "strxfrm reports the truncated length",
            ),
        ),
    ),
    "strtof": (
        (
            "lib/libc/stdlib/strtof.c",
            "lib/libc/stdlib/strtold.c",
            "lib/libc/stdlib/strtofp.c",
        ),
        ("strtof", "strtold"),
        "c17_strtof_test.c",
        "c17 strtof: 220069 checks passed\n",
        ("-lm",),
        (
            (
                "lib/libc/stdlib/strtof.c",
                "\tif (range)\n\t\terrno = ERANGE;\n",
                "",
                1,
                "strtof",
                "strtof reports no range error",
            ),
            (
                "lib/libc/stdlib/strtold.c",
                "\tif (range)\n\t\terrno = ERANGE;\n",
                "",
                1,
                "strtold",
                "strtold reports no range error",
            ),
            (
                "lib/libc/stdlib/strtold.c",
                "STRTOFP_BINARY64",
                "STRTOFP_BINARY32",
                1,
                "strtold",
                "strtold converts through the binary32 core",
            ),
            (
                "lib/libc/stdlib/strtofp.c",
                "\t\t\tif (c > 0 || (c == 0 && (bits & 1) != 0)) {",
                "\t\t\tif (c > 0) {",
                1,
                "strtof",
                "strtof rounds a decimal tie down",
            ),
            (
                "lib/libc/stdlib/strtofp.c",
                "\tif (sticky) {\n\t\tbig_muladd(&digits, 10, 1);",
                "\tif (sticky && 0) {\n\t\tbig_muladd(&digits, 10, 1);",
                1,
                "strtof",
                "strtof drops digits beyond those it keeps",
            ),
            (
                "lib/libc/stdlib/strtofp.c",
                "(rest == half && (sticky || (bits & 1) != 0))",
                "rest == half",
                1,
                "strtof",
                "strtof rounds a hexadecimal tie up",
            ),
            (
                "lib/libc/stdlib/strtofp.c",
                "\t\tif ((n = word(in, i, \"inity\")) != 0)\n\t\t\ti += n;\n",
                "",
                1,
                "strtof",
                "strtof stops inside INFINITY",
            ),
        ),
    ),
    "time": (
        ("lib/libc/gen/clock.c", "lib/libc/gen/timespec_get.c"),
        ("clock", "timespec_get"),
        "c17_time_test.c",
        "c17 time: 7 checks passed\n",
        (),
        (
            (
                "lib/libc/gen/clock.c",
                "return (clock_t)ticks;",
                "return (clock_t)(ticks & 0);",
                1,
                "clock",
                "clock never advances",
            ),
            (
                "lib/libc/gen/timespec_get.c",
                "base != TIME_UTC || ",
                "",
                2,
                "timespec_get",
                "timespec_get accepts every base",
            ),
            (
                "lib/libc/gen/timespec_get.c",
                "(long)now.tv_usec * 1000L",
                "(long)now.tv_usec * 1000L + 1",
                2,
                "timespec_get",
                "timespec_get reports sub-microsecond digits",
            ),
        ),
    ),
}
# Inherited members are support, not subjects: a sanitizer run does not
# instrument them (fmod.c shifts into the sign bit of an int).
LEGACY = ["-std=gnu17", "-w", "-fno-builtin", "-O2", "-U_FORTIFY_SOURCE", "-fno-sanitize=all"]
SUPPORT = {}
OBJECT_RENAME = {"math"}
EXTRA = {}
MATH_MEMBERS = (
    "acosf acosh acoshf asinf asinh asinhf atan2f atanf "
    "atanh atanhf cbrt cbrtf ceilf copysign copysignf cosf "
    "coshf erfcf erff exp2 exp2f expf expm1 expm1f "
    "fabs fabsf fdim fdimf floorf fma fmaf fmax fmaxf "
    "fmin fminf fmodf frexpf hypotf ilogb ilogbf ldexpf "
    "lgamma lgammaf llrint llrintf llround llroundf log10f log1p "
    "log1pf log2 log2f logb logbf logf lrint lrintf "
    "lround lroundf nan nanf nearbyint nearbyintf nextafter nextafterf "
    "nexttowardf powf remainder remainderf remquo remquof rint rintf "
    "round roundf scalbln scalblnf scalbn scalbnf sinf sinhf "
    "sqrtf tanf tanhf tgamma tgammaf trunc truncf"
).split()
MATH_LEGACY_SYMBOLS = (
    "acos asin atan atan2 exp erf erfc floor ceil fmod hypot log log10 pow sin cos "
    "sinh cosh sqrt tan tanh frexp ldexp modf"
).split()
FAMILIES["math"] = (
    tuple("lib/libm/%s.c" % name for name in MATH_MEMBERS),
    tuple(MATH_MEMBERS + MATH_LEGACY_SYMBOLS),
    "c17_math_test.c",
    "c17 math: 4972090 checks passed\n",
    ("-lm",),
    (
        (
            "lib/libm/rint.c",
            '(fraction == half && (bits & unit) != 0)',
            'fraction == half',
            1,
            "rint",
            "rint rounds every tie upward",
        ),
        (
            "lib/libm/round.c",
            '\tbits += 0x0008000000000000ULL >> e;\n',
            '',
            1,
            "round",
            "round truncates",
        ),
        (
            "lib/libm/trunc.c",
            '\tif (e < 0)\n\t\treturn fp_double(bits & FP_SIGN_BIT);',
            '\tif (e < 0)\n\t\treturn x;',
            1,
            "trunc",
            "trunc keeps fractions below one",
        ),
        (
            "lib/libm/fabs.c",
            "return fp_double(fp_bits(x) & ~FP_SIGN_BIT);",
            "return x < 0.0 ? -x : x;",
            1,
            "fabs",
            "fabs keeps the sign of a negative zero",
        ),
        (
            "lib/libm/nextafter.c",
            'if ((x < y) == (x > 0.0))',
            'if (x < y)',
            2,
            "nextafter",
            "nextafter steps the encoding without regard to sign",
        ),
        (
            "lib/libm/fmax.c",
            'return x > y ? x : y;',
            'return x < y ? x : y;',
            2,
            "fmax",
            "fmax returns the smaller argument",
        ),
        (
            "lib/libm/scalbn.c",
            '(rest == half && (mantissa & 1) != 0)',
            'rest == half',
            3,
            "scalbn",
            "scalbn rounds subnormal ties upward",
        ),
        (
            "lib/libm/ilogb.c",
            'for (e = -1023;',
            'for (e = -1022;',
            3,
            "logb",
            "ilogb miscounts subnormal exponents",
        ),
        (
            "lib/libm/remquo.c",
            '(r == ay - r && (q & 1) != 0)',
            'r == ay - r',
            4,
            "remainder",
            "remquo rounds halfway quotients away from even",
        ),
        (
            "lib/libm/fma.c",
            '(rest == 0x400 && (r.lo != 0 || (mantissa & 1) != 0))',
            '(rest == 0x400 && (mantissa & 1) != 0)',
            5,
            "fma",
            "fma ignores the sticky bits at a tie",
        ),
        (
            "lib/libm/fma.c",
            '(middle << 16)',
            '(middle << 15)',
            5,
            "fma",
            "fma misplaces the middle partial products",
        ),
        (
            "lib/libm/fma.c",
            '\t\treturn x * y;\t\t/* one rounding, and the sign of zero */',
            '\t\treturn x * y + z;',
            5,
            "fma",
            "fma loses the sign of an underflowed product",
        ),
        (
            "lib/libm/fmaf.c",
            'if (error != 0.0) {',
            'if (0) {',
            5,
            "fmaf",
            "fmaf rounds twice",
        ),
        (
            "lib/libm/log1p.c",
            'return log(u) * x / (u - 1.0);',
            'return log(u);',
            6,
            "log1p",
            "log1p loses small arguments",
        ),
        (
            "lib/libm/tgamma.c",
            'result *= factor + z;',
            'result *= factor;',
            6,
            "tgamma",
            "tgamma drops the fractional part from the recurrence",
        ),
        (
            "lib/libm/cbrt.c",
            'for (i = 0; i < 6; i++)',
            'for (i = 0; i < 2; i++)',
            6,
            "cbrt",
            "cbrt stops before convergence",
        ),
    ),
)
SUPPORT["math"] = tuple(
    "lib/libm/%s.c" % name
    for name in "asin atan exp erf floor fmod hypot log pow sin sinh sqrt tan tanh".split()
) + ("lib/libc/gen/frexp.c", "lib/libc/gen/ldexp.c", "lib/libc/gen/modf.c")
# Error bounds in units of the last place, each above the worst error the
# fixture measures on the hosting system. The log-based members inherit
# the accuracy of the tree's log(), whose own error reaches about 20.
EXTRA["math"] = ["-DLIMIT_CBRT=4", "-DLIMIT_EXP=32", "-DLIMIT_LOG=32", "-DLIMIT_LOG2=32"]
EXTRA["math"] += ["-DLIMIT_GAMMA=32", "-DLIMIT_LGAMMA=16", "-DLIMIT_EDGE=32"]
# The fixture defines an oracle wrapper per member and calls most of them.
EXTRA["math"] += ["-Wno-unused-function"]
# newlib's fma(), fmaf() and tgamma() are not accurate enough to be oracles,
# so the qemu-user variant is held to the result digests in
# c17_math_digests.h for those groups, which the glibc-checked runs also
# reproduce; see fused() in the fixture. With the NetBSD oracle fma and
# fmaf stay withdrawn there: NetBSD's fma is withheld as defective and its
# fmaf needs a rounding mode soft-float lacks (netbsd_oracle.py).
UNTRUSTED_NEWLIB = ["-DC17_MATH_UNTRUSTED_FMA", "-DC17_MATH_UNTRUSTED_FMAF"]
UNTRUSTED_NEWLIB += ["-DC17_MATH_UNTRUSTED_APPROX"]
UNTRUSTED_NETBSD_ARM = ["-DC17_MATH_UNTRUSTED_FMA", "-DC17_MATH_UNTRUSTED_FMAF"]
# The NetBSD groups each family's oracle calls reach; see netbsd_oracle.py.
# locale has none: NetBSD's setlocale and collation run on its citrus locale
# database, and its wide conversions and classification read rune tables,
# which a host or newlib program cannot load. time reaches system calls.
NETBSD_GROUPS = {"math": ["libm"], "wide": ["wide"], "strtof": ["strtof", "libm"]}
WIDE_MEMBERS = (
    "btowc c16rtomb c32rtomb iswalnum iswalpha iswblank iswcntrl iswctype "
    "iswdigit iswgraph iswlower iswprint iswpunct iswspace iswupper iswxdigit "
    "mbrlen mbrtoc16 mbrtoc32 mbrtowc mbsinit mbsrtowcs towctrans towlower "
    "towupper wcrtomb wcscat wcschr wcscmp wcscoll wcscpy wcscspn "
    "wcslen wcsncat wcsncmp wcsncpy wcspbrk wcsrchr wcsrtombs wcsspn "
    "wcsstr wcstod wcstof wcstok wcstol wcstoll wcstoul wcstoull "
    "wcsxfrm wctob wctrans wctype wmemchr wmemcmp wmemcpy wmemmove "
    "wmemset "
).split()
FAMILIES["wide"] = (
    tuple("lib/libc/wchar/%s.c" % name for name in WIDE_MEMBERS)
    + (
        "lib/libc/stdlib/wcstoimax.c",
        "lib/libc/stdlib/wcstoumax.c",
        "lib/libc/stdlib/strtofp.c",
    ),
    tuple(WIDE_MEMBERS + ["wcstoimax", "wcstoumax", "__discobsd_wcstou64"]),
    "c17_wide_test.c",
    "c17 wide: 585969 checks passed\n",
    (),
    (
        (
            "lib/libc/wchar/wcsncpy.c",
            "\tfor (; i < n; i++)\n\t\tdst[i] = L'\\0';\n",
            '',
            1,
            "wcsncpy",
            "wcsncpy leaves the tail unpadded",
        ),
        (
            "lib/libc/wchar/wcsrchr.c",
            '\t\t\tfound = s;',
            '\t\t\treturn (wchar_t *)s;',
            1,
            "wcsrchr",
            "wcsrchr returns the first occurrence",
        ),
        (
            "lib/libc/wchar/wcstok.c",
            "\t\t*s++ = L'\\0';",
            '\t\ts++;',
            1,
            "wcstok",
            "wcstok leaves tokens unterminated",
        ),
        (
            "lib/libc/wchar/wcsxfrm.c",
            'if (length < n)',
            'if (length <= n)',
            1,
            "wcsxfrm",
            "wcsxfrm writes the terminator past the size",
        ),
        (
            "lib/libc/wchar/wcsncat.c",
            "\t*d = L'\\0';\n",
            '',
            1,
            "wcsncat",
            "wcsncat omits the terminator",
        ),
        (
            "lib/libc/wchar/wctob.c",
            'c <= UCHAR_MAX',
            'c <= SCHAR_MAX',
            2,
            "wctob",
            "wctob rejects bytes above 0x7f",
        ),
        (
            "lib/libc/wchar/wcrtomb.c",
            'if ((unsigned long)wc > UCHAR_MAX) {',
            'if (0) {',
            2,
            "wcrtomb",
            "wcrtomb truncates an unencodable character",
        ),
        (
            "lib/libc/wchar/mbsrtowcs.c",
            '\t\t\t*src = NULL;',
            '\t\t\t*src = s + i;',
            2,
            "mbsrtowcs",
            "mbsrtowcs leaves the source set after the terminator",
        ),
        (
            "lib/libc/wchar/wcstoul.c",
            "return (unsigned long)value;",
            "return (unsigned long)magnitude;",
            3,
            "wcstoul",
            "wcstoul returns the magnitude of a negated value",
        ),
        (
            "lib/libc/wchar/iswctype.c",
            '\tcase 2:\n\t\treturn iswalpha(c);',
            '\tcase 2:\n\t\treturn iswalnum(c);',
            4,
            "iswctype",
            "iswctype tests the wrong class",
        ),
        (
            "lib/libc/wchar/towupper.c",
            'c < 128 ? (wint_t)toupper((int)c) : c',
            'c < 128 ? (wint_t)tolower((int)c) : c',
            4,
            "towupper",
            "towupper lowers",
        ),
        (
            "lib/libc/wchar/c16rtomb.c",
            'if (c > UCHAR_MAX) {',
            'if (0) {',
            5,
            "c16rtomb",
            "c16rtomb truncates an unencodable code",
        ),
    ),
)
OBJECT_RENAME.add("wide")
EXTRA["strtof"] = ["-DSTRTOF_DIGEST=0xd62dc4a95811dd63ULL"]
# Families whose members reach system calls newlib's support does not serve.
HOST_ONLY = {"time"}


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def command(arguments):
    result = subprocess.run(arguments, capture_output=True, text=True, timeout=180, check=False)
    require(
        result.returncode == 0 and not result.stderr,
        "c17 family compilation fails: " + result.stdout + result.stderr,
    )


def build(root, compiler, work, arm, family, replacement=None, oracle=None):
    sources, symbols, fixture, _, libraries, _ = FAMILIES[family]
    machine = ARM if arm else []
    if arm:
        # Headers the hosting newlib lacks; searched after its own.
        machine = machine + ["-idirafter", str(root / "tests/libc_contracts/newlib_shim")]
    if "-m32" in compiler:
        # x87 arithmetic keeps excess precision, which the target never has.
        machine = machine + ["-msse2", "-mfpmath=sse"]
    renames = ["-D%s=tree_%s" % (name, name) for name in symbols]
    redefine = None
    if family in OBJECT_RENAME:
        # A hosting <math.h> does not survive its function names as macros,
        # so these members compile under their own names and objcopy
        # renames definitions and references alike in each object.
        renames = []
        redefine = work / "redefine"
        redefine.write_text("".join("%s tree_%s\n" % (name, name) for name in symbols))
    objcopy = "arm-none-eabi-objcopy" if arm else "objcopy"
    flags = BASE + EXTRA.get(family, []) + renames + machine
    if arm and family == "math":
        flags = flags + (UNTRUSTED_NETBSD_ARM if oracle else UNTRUSTED_NEWLIB)
    if arm and family == "strtof" and not oracle:
        # newlib's strtof rounds through double; see differential() there.
        flags = flags + ["-DC17_STRTOF_UNTRUSTED_ORACLE"]
    objects = []
    for index, name in enumerate(sources):
        source = replacement[1] if replacement and replacement[0] == name else root / name
        if source.suffix == ".o":
            # A foreign member, already compiled and renamed.
            objects.append(str(source))
            continue
        obj = work / ("member%d.o" % index)
        # A mutant lives in the work directory; its private headers do not.
        include = ["-I", str((root / name).parent)]
        command(compiler + flags + include + ["-c", str(source), "-o", str(obj)])
        if redefine:
            command([objcopy, "--redefine-syms", str(redefine), str(obj)])
        objects.append(str(obj))
    for index, name in enumerate(SUPPORT.get(family, ())):
        # Inherited members the family calls: built as the tree builds them.
        obj = work / ("support%d.o" % index)
        command(compiler + LEGACY + renames + machine + ["-c", str(root / name), "-o", str(obj)])
        if redefine:
            command([objcopy, "--redefine-syms", str(redefine), str(obj)])
        objects.append(str(obj))
    obj = work / "fixture.o"
    command(compiler + flags + ["-c", str(root / "tests/libc_contracts" / fixture), "-o", str(obj)])
    objects.append(str(obj))
    link = list(libraries)
    if arm:
        support = work / "support.o"
        command(
            compiler
            + ARM
            + ["-std=gnu17", "-Wall", "-Wextra", "-Werror", "-fomit-frame-pointer", "-c"]
            + [str(root / "tests/libbn/qemu_support.c"), "-o", str(support)]
        )
        objects.append(str(support))
        link = ["-nostartfiles", "-Wl,-Ttext=0x20000"] + link + ["-lc", "-lgcc"]
    program = work / "program"
    # NetBSD's archive comes ahead of every hosting library, so the oracle's
    # plain names resolve to it wherever NetBSD defines them.
    archive = [str(oracle)] if oracle else []
    command(compiler + machine + objects + archive + link + ["-o", str(program)])
    return program


def execute(program, arm):
    prefix = ["qemu-arm", "-cpu", "cortex-m0"] if arm else []
    return subprocess.run(
        prefix + [str(program)], capture_output=True, text=True, timeout=180, check=False
    )


# For each withheld NetBSD member: the fixture section's exit status and the
# tree members whose failure may report it. The tree's float members are
# their double members narrowed, so NetBSD's rint is caught through rintf
# when the float loop reaches a short fraction first.
FOREIGN_STATUS = {
    "rint": (1, ("rint", "rintf", "nearbyint", "nearbyintf")),
    "rintf": (1, ("rintf",)),
    "logb": (3, ("logb", "logbf")),
    "logbf": (3, ("logbf",)),
    "fma": (5, ("fma",)),
}


def foreign(root, compiler, work, args, oracle):
    """Substitute each withheld NetBSD member for the tree's own member.

    netbsd_oracle.DEFECTS records NetBSD members that are wrong. Each one
    whose tree counterpart belongs to this family replaces it, and the
    fixture must reject it under the member's name, as it would a mutant.
    """
    sources, symbols = FAMILIES[args.family][0], FAMILIES[args.family][1]
    for source, (symbol, _, _) in netbsd_oracle.DEFECTS.items():
        member = next((name for name in sources if Path(name).stem == symbol), None)
        if member is None:
            continue
        obj = work / ("foreign-%s.o" % symbol)
        try:
            netbsd_oracle.compile_member(args.netbsd, compiler, args.arm, work, source, obj)
        except netbsd_oracle.OracleError as error:
            raise SystemExit(str(error)) from error
        redefine = work / "foreign-redefine"
        redefine.write_text("".join("%s tree_%s\n" % (name, name) for name in symbols))
        objcopy = "arm-none-eabi-objcopy" if args.arm else "objcopy"
        command([objcopy, "--redefine-syms", str(redefine), str(obj)])
        program = build(root, compiler, work, args.arm, args.family, (member, obj), oracle=oracle)
        result = execute(program, args.arm)
        status, names = FOREIGN_STATUS[symbol]
        require(
            result.returncode == status
            and any(result.stdout.startswith("FAIL " + name + ":") for name in names),
            "NetBSD's defective %s passes the fixture: " % source + result.stdout + result.stderr,
        )
        print("c17 %s rejects NetBSD's %s: %s" % (args.family, source, result.stdout.strip()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("family", choices=sorted(FAMILIES))
    parser.add_argument("root", type=Path)
    parser.add_argument("compiler")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--arm", action="store_true")
    parser.add_argument(
        "--netbsd",
        type=Path,
        help="src-netbsd checkout whose libc and libm become the oracle",
    )
    args = parser.parse_args()
    require(
        args.netbsd is None or args.family in NETBSD_GROUPS,
        "c17 %s has no NetBSD oracle" % args.family,
    )
    require(
        not (args.arm and args.family in HOST_ONLY),
        args.family + " has no qemu-user variant",
    )
    root = args.root.resolve()
    compiler = shlex.split(args.compiler)
    expected = FAMILIES[args.family][3]
    with tempfile.TemporaryDirectory(prefix="c17-family-") as temporary:
        work = Path(temporary)
        oracle = None
        if args.netbsd is not None:
            try:
                oracle, report = netbsd_oracle.build(
                    args.netbsd, compiler, args.arm, work, NETBSD_GROUPS[args.family]
                )
            except netbsd_oracle.OracleError as error:
                raise SystemExit(str(error)) from error
            print(report)
        program = build(root, compiler, work, args.arm, args.family, oracle=oracle)
        result = execute(program, args.arm)
        require(
            result.returncode == 0 and not result.stderr and result.stdout == expected,
            "c17 %s contract fails: " % args.family + result.stdout + result.stderr,
        )
        print(result.stdout, end="")
        accepted = work / "accepted"
        shutil.copy2(program, accepted)
        for name, anchor, change, status, member, description in FAMILIES[args.family][5]:
            original = (root / name).read_text()
            require(original.count(anchor) == 1, "mutation anchor differs: " + description)
            source = work / "mutant.c"
            source.write_text(original.replace(anchor, change))
            program = build(
                root, compiler, work, args.arm, args.family, (name, source), oracle=oracle
            )
            result = execute(program, args.arm)
            require(
                result.returncode == status and result.stdout.startswith("FAIL " + member + ":"),
                "mutant lacks its named rejection (%s): " % description
                + result.stdout
                + result.stderr,
            )
            print("c17 %s mutant rejected: %s" % (args.family, description))
        if oracle is not None:
            foreign(root, compiler, work, args, oracle)
        if args.output:
            shutil.copy2(accepted, args.output)


if __name__ == "__main__":
    main()
