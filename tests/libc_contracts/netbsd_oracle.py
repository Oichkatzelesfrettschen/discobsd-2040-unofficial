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
"""Build NetBSD's own libc and libm members as an oracle for the C17 gates.

The oracle is NetBSD's source compiled against NetBSD's headers from a
caller-named src-netbsd checkout, for the same target as the fixture: the
host's x86-64 or i386 ABI, or Cortex-M0+ for the qemu-user variant. Its
objects define the public names (fma, wcslen, strtof, ...), and the driver
links them ahead of the hosting libraries, so a fixture's plain-name oracle
calls reach NetBSD's implementation wherever NetBSD provides one.

Each group names the NetBSD sources to compile and the public symbols the
built oracle must define. A group whose required symbol is missing fails,
so a NetBSD source that stops compiling cannot quietly hand its function
back to the hosting library. Sources outside the required closure that do
not compile under this configuration (the ld128 long double members, for
example) are skipped and counted.
"""

import platform
import subprocess
from pathlib import Path

GLUE = Path(__file__).resolve().parent

# Machine header directories, as NetBSD's <machine/...> and <x86/...> resolve.
MACHINE = {
    "amd64": {"machine": "sys/arch/amd64/include", "x86": "sys/arch/x86/include"},
    "i386": {"machine": "sys/arch/i386/include", "x86": "sys/arch/x86/include"},
    "arm": {"machine": "sys/arch/arm/include", "arm": "sys/arch/arm/include"},
}
GDTOA_ARCH = {"amd64": "x86_64", "i386": "i386", "arm": "arm"}
FENV = {"amd64": "lib/libm/arch/x86_64/fenv.c", "i386": "lib/libm/arch/i387/fenv.c"}

CLASSIFY = [
    "lib/libc/gen/%s%s_ieee754.c" % (name, width)
    for name in ("fpclassify", "isfinite", "isinf", "isnan", "signbit")
    for width in ("d", "f", "l")
]
GDTOA = [
    "lib/libc/gdtoa/%s.c" % name
    for name in (
        "strtof strtodg gethex hexnan gmisc misc smisc sum ulp dmisc gdtoa_locks hd_init"
    ).split()
]

# NetBSD members withheld from the oracle because they are wrong, each with
# a witness program that must still show the defect. When NetBSD fixes one,
# its witness stops reproducing and the build fails, so the exclusion is
# removed rather than outliving the bug.
#
# s_rintf.c, fdlibm's rintf: for an unbiased exponent of 21 it computes the
# sticky bit as 0x100000 >> 21, which is 0, so a fraction of .75 becomes .5
# and rounds to even, downward. Against glibc over all 2**32 floats it is
# wrong for exactly the 2,097,152 with exponent 21 and both fraction bits
# set; rintf(3659852.75f) gives 3659852.
#
# s_rint.c, fdlibm's rint: the same guard bit is 0x20000 >> 18, which is 0
# at exponent 18 (FreeBSD's msun later handles that exponent apart). It
# matters only when the low 32 bits are zero, so over every such double at
# exponents 0 to 51 it is wrong for exactly the 262,144 at exponent 18 of
# the form n + 0.75 with n even; rint(-490468.75) gives -490468.
#
# s_logb.c and s_logbf.c return -1022 and -126 for every subnormal, the
# IEEE 754-1985 recommended function, where C17 7.12.6.11p2 treats a
# subnormal as though it were normalized: logb(0x1p-1074) is -1074, and
# NetBSD gives -1022. That covers all 2**53 - 2 subnormal doubles and all
# 2**24 - 2 subnormal floats.
#
# s_fma.c, FreeBSD's fma of 2011: a result just below a power of two, where
# the spacing of doubles halves, can round to the power instead.
# fma(-2.5, 0.1, 0x1p52) is exactly 2**-56 below the midpoint of
# 0x1p52 - 0.5 and 0x1p52 and NetBSD returns 0x1p52. Against glibc it is
# wrong for 408,909 of 2,000,000 draws built to land just below a power of
# two, and for none of 2,000,000 general draws.
#
# The C17 gates also substitute each withheld member for the tree's own and
# require the fixture to reject it, which calibrates the fixture against a
# real defect of the class it exists to catch.
DEFECTS = {
    "lib/libm/src/s_rintf.c": (
        "rintf",
        """
#include <stdio.h>
float rintf(float);
int main(void) {
	volatile float x = 3659852.75f;
	printf("%.1f\\n", (double)rintf(x));
	return 0;
}
""",
        "3659852.0\n",
    ),
    "lib/libm/src/s_rint.c": (
        "rint",
        """
#include <stdio.h>
double rint(double);
int main(void) {
	volatile double x = -490468.75;
	printf("%.1f\\n", rint(x));
	return 0;
}
""",
        "-490468.0\n",
    ),
    "lib/libm/src/s_logb.c": (
        "logb",
        """
#include <stdio.h>
double logb(double);
int main(void) {
	volatile double x = 0x1p-1074;
	printf("%.1f\\n", logb(x));
	return 0;
}
""",
        "-1022.0\n",
    ),
    "lib/libm/src/s_logbf.c": (
        "logbf",
        """
#include <stdio.h>
float logbf(float);
int main(void) {
	volatile float x = 0x1p-149f;
	printf("%.1f\\n", (double)logbf(x));
	return 0;
}
""",
        "-126.0\n",
    ),
    "lib/libm/src/s_fma.c": (
        "fma",
        """
#include <stdio.h>
double fma(double, double, double);
int main(void) {
	volatile double x = -2.5, y = 0.1, z = 0x1p52;
	printf("%a\\n", fma(x, y, z));
	return 0;
}
""",
        "0x1p+52\n",
    ),
}
WITHHELD = set(DEFECTS)

# group: (source globs, required public symbols, sources excluded per target)
GROUPS = {
    "libm": (
        ["lib/libm/src/*.c"] + CLASSIFY,
        (
            "trunc round rint nearbyint logb cbrt exp2 expm1 log1p log2 asinh acosh "
            "atanh lgamma tgamma fmax fmin fdim copysign nextafter remainder "
            "truncf roundf rintf nearbyintf logbf fabsf ceilf floorf cbrtf exp2f "
            "expm1f log1pf log2f asinhf acoshf atanhf sqrtf sinf cosf expf logf "
            "fmaxf fminf fdimf copysignf nextafterf remainderf fma scalbn "
            "scalbnf remquo ilogb llrint llround sqrt sin cos exp log"
        ).split(),
        # NetBSD's fmaf rounds a tie by switching to round-toward-zero, which
        # soft-float arithmetic under qemu-user cannot do. s_rintf.c is a
        # known defect; see DEFECTS.
        {
            "amd64": WITHHELD,
            "i386": WITHHELD,
            "arm": WITHHELD | {"lib/libm/src/s_fmaf.c"},
        },
    ),
    "wide": (
        ["lib/libc/string/wcs*.c", "lib/libc/string/wmem*.c"],
        (
            "wcslen wcscmp wcsncmp wmemcmp wcschr wcsrchr wmemchr wcsspn wcscspn "
            "wcspbrk wcsstr wcscpy wcscat wcsncat wcsncpy wmemcpy wmemmove wmemset "
            "wcstok"
        ).split(),
        {},
    ),
    "strtof": (GDTOA, ["strtof"], {}),
    "division": (
        ["lib/libc/stdlib/%s.c" % n for n in ("div", "ldiv", "lldiv", "llabs", "imaxdiv")],
        ["div", "ldiv", "lldiv", "llabs", "imaxdiv"],
        {},
    ),
}




class OracleError(Exception):
    pass


def witness(driver, flags, root, work, machine, archive):
    """Require each withheld member to still show its recorded defect."""
    for source, (symbol, program, wrong) in DEFECTS.items():
        base = work / ("defect-%s-%s" % (machine, symbol))
        obj = base.with_suffix(".o")
        path = root / source
        result = run(driver + private(root, path) + flags + ["-c", str(path), "-o", str(obj)])
        if result.returncode != 0:
            raise OracleError("NetBSD oracle: %s no longer compiles: %s" % (source, result.stderr))
        main = base.with_suffix(".c")
        main.write_text(program)
        exe = base.with_suffix(".exe")
        # The archive supplies the NetBSD helpers a member calls.
        result = run(
            driver + ["-fno-builtin", "-o", str(exe), str(main), str(obj), str(archive), "-lm"]
        )
        if result.returncode != 0:
            raise OracleError(
                "NetBSD oracle: %s witness fails to link: %s" % (source, result.stderr)
            )
        result = run([str(exe)])
        if result.stdout != wrong:
            raise OracleError(
                "NetBSD oracle: %s no longer shows its recorded defect (printed %r); "
                "return it to the oracle and drop it from DEFECTS" % (source, result.stdout)
            )


def target(compiler, arm):
    if arm:
        return "arm"
    if platform.machine() not in ("x86_64", "AMD64"):
        raise OracleError("NetBSD oracle: host %s has no configured headers" % platform.machine())
    return "i386" if "-m32" in compiler else "amd64"


def revision(root):
    result = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise OracleError("NetBSD oracle: %s is not a git checkout" % root)
    return result.stdout.strip()


def run(arguments):
    return subprocess.run(arguments, capture_output=True, text=True, timeout=180, check=False)


def defined(nm, archive):
    result = run([nm, "--defined-only", str(archive)])
    if result.returncode != 0:
        raise OracleError("NetBSD oracle: nm fails: " + result.stderr)
    return {line.split()[-1] for line in result.stdout.splitlines() if len(line.split()) == 3}


def configuration(root, compiler, arm, work):
    """Return the compiler words, flags and machine for NetBSD sources."""
    root = Path(root).resolve()
    machine = target(compiler, arm)
    include = work / ("netbsd-include-" + machine)
    include.mkdir(parents=True, exist_ok=True)
    for name, path in MACHINE[machine].items():
        link = include / name
        if not link.exists():
            link.symlink_to(root / path)
    stdint = include / "stdint.h"
    if not stdint.exists():
        stdint.symlink_to(root / "sys/sys/stdint.h")
    compiler_include = compiler[:1]
    if Path(compiler[0]).name.startswith("clang"):
        compiler_include = ["gcc"]
    gcc_include = run(compiler_include + ["-print-file-name=include"]).stdout.strip()
    flags = [
        "-std=gnu17",
        "-O1",
        "-w",
        "-fno-builtin",
        "-fno-stack-protector",
        "-fno-sanitize=all",
        "-nostdinc",
        "-D_LIBM",
        "-D_LIBC",
        "-D_REENTRANT",
        "-I" + str(root / "lib/libc/locale"),
        "-I" + str(root / "lib/libc/gdtoa"),
        "-I" + str(root / "lib/libc/arch" / GDTOA_ARCH[machine] / "gdtoa"),
        "-I" + str(root / "include"),
        "-I" + str(include),
        "-I" + str(root / "sys"),
        "-I" + str(root / "lib/libpthread"),
        "-isystem",
        gcc_include,
    ]
    if arm:
        flags += ["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft"]
    if machine == "i386":
        # NetBSD's i386 libm takes rint and friends from i387 assembly, so
        # its C members assume no excess precision; SSE2 arithmetic, which
        # the fixtures' -m32 builds also use, gives them that.
        flags += ["-msse2", "-mfpmath=sse"]
    # The compiler words, -m32 among them, select the target ABI. clang's
    # integrated assembler rejects the weak-alias form NetBSD gives several
    # members a public name with ("changed binding to STB_GLOBAL"), and the
    # oracle needs the fixture's ABI rather than its compiler, so a clang
    # fixture gets a gcc-built oracle.
    if Path(compiler[0]).name.startswith("clang"):
        compiler = ["gcc"] + compiler[1:]
    return compiler, flags, machine


def private(root, source):
    """The private header directory a NetBSD source finds first.

    libm and libc each carry a namespace.h that maps public names to the
    reserved ones their weak aliases expect, and they differ, so each source
    must reach its own library's before the other's.
    """
    root = Path(root).resolve()
    relative = str(Path(source).resolve().relative_to(root))
    first, second = root / "lib/libm/src", root / "lib/libc/include"
    if relative.startswith("lib/libc/"):
        first, second = second, first
    return ["-I" + str(first), "-I" + str(second)]


def compile_member(root, compiler, arm, work, source, output):
    """Compile one NetBSD source, as the oracle does, to output."""
    driver, flags, _ = configuration(root, compiler, arm, work)
    path = Path(root).resolve() / source
    result = run(driver + private(root, path) + flags + ["-c", str(path), "-o", str(output)])
    if result.returncode != 0:
        raise OracleError("NetBSD oracle: %s does not compile: %s" % (source, result.stderr))


def build(root, compiler, arm, work, groups):
    """Return the archive of NetBSD's members for these groups, and a report."""
    root = Path(root).resolve()
    driver, flags, machine = configuration(root, compiler, arm, work)
    tools = "arm-none-eabi-" if arm else ""
    sources = []
    excluded = set()
    for group in groups:
        globs, _, exclusions = GROUPS[group]
        excluded |= exclusions.get(machine, set())
        for pattern in globs:
            sources += sorted(root.glob(pattern))
    if any(group == "libm" for group in groups) and machine in FENV:
        sources.append(root / FENV[machine])
    objects = []
    skipped = []
    out = work / ("netbsd-objects-" + machine)
    out.mkdir(parents=True, exist_ok=True)
    for index, source in enumerate(dict.fromkeys(sources)):
        if str(source.relative_to(root)) in excluded:
            continue
        obj = out / ("%04d-%s.o" % (index, source.stem))
        result = run(driver + private(root, source) + flags + ["-c", str(source), "-o", str(obj)])
        if result.returncode == 0:
            objects.append(str(obj))
        else:
            skipped.append(str(source.relative_to(root)))
    # Glue for what NetBSD's libc would supply around these members.
    glue = out / "glue-host.o"
    host = run(
        compiler
        + (["-mcpu=cortex-m0plus", "-mthumb", "-mfloat-abi=soft"] if arm else [])
        + ["-std=gnu17", "-O1", "-Wall", "-Werror", "-fno-sanitize=all"]
        + ["-c", str(GLUE / "netbsd_glue_host.c"), "-o", str(glue)]
    )
    if host.returncode != 0:
        raise OracleError("NetBSD oracle: host glue fails: " + host.stderr)
    objects.append(str(glue))
    if "strtof" in groups:
        glue = out / "glue-netbsd.o"
        result = run(
            driver
            + private(root, root / "lib/libc/gdtoa/strtof.c")
            + flags
            + ["-c", str(GLUE / "netbsd_glue_netbsd.c"), "-o", str(glue)]
        )
        if result.returncode != 0:
            raise OracleError("NetBSD oracle: locale glue fails: " + result.stderr)
        objects.append(str(glue))
    archive = work / ("netbsd-oracle-%s.a" % machine)
    if archive.exists():
        archive.unlink()
    result = run([tools + "ar", "rcs", str(archive)] + objects)
    if result.returncode != 0:
        raise OracleError("NetBSD oracle: ar fails: " + result.stderr)
    if not arm and "libm" in groups:
        witness(driver, flags, root, work, machine, archive)
    have = defined(tools + "nm", archive)
    missing = []
    for group in groups:
        missing += [name for name in GROUPS[group][1] if name not in have]
    withheld = {symbol for symbol, _, _ in DEFECTS.values()}
    if machine == "arm":
        withheld.add("fmaf")
    missing = [name for name in missing if name not in withheld]
    if missing:
        raise OracleError(
            "NetBSD oracle: no NetBSD definition for %s (skipped sources: %s)"
            % (" ".join(missing), " ".join(skipped))
        )
    report = "NetBSD oracle %s at %s: %d members, %d sources skipped" % (
        machine,
        revision(root)[:12],
        len(objects),
        len(skipped),
    )
    return archive, report
