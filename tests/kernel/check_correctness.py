"""Link production paths; require behavioral regressions to reject mutations."""

import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "tests/kernel"
CC = shlex.split(os.environ.get("HOST_CC", "cc"))
GC_FLAG = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"


def run(args):
    subprocess.run(args, check=True)


def build(work, suite, changes):
    includes = [
        "-I" + str(work),
        "-I" + str(ROOT / "sys"),
        "-I" + str(ROOT / "sys/arch"),
        "-I" + str(TESTS),
    ]
    common = [
        "-std=gnu17",
        "-ffreestanding",
        "-fno-builtin",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-deprecated-non-prototype",
        "-Wno-asm-operand-widths",
        "-DKERNEL",
        "-Dprintf=hk_kprintf",
    ]
    objects = []
    sources = (
        ["kern_time.c", "sys_inode.c", "ufs_subr.c", "ufs_syscalls2.c"]
        if suite == "io"
        else ["kern_prot.c", "kern_prot2.c", "kern_proc.c"]
    )
    for name in sources:
        source = ROOT / "sys/kern" / name
        text = source.read_text()
        if name in changes:
            old, new = changes[name]
            if text.count(old) != 1:
                raise RuntimeError("mutation anchor is ambiguous: " + name)
            text = text.replace(old, new)
            source = work / name
            source.write_text(text)
        extra = ["-ffunction-sections", "-fdata-sections"]
        if name == "kern_time.c":
            extra += ["-Dgettimeofday=gate_gettimeofday", "-Dsettimeofday=gate_settimeofday"]
            if name in changes:
                # Clang diagnoses the historical wrong-sign bound before the
                # runtime oracle can reject it. Only this mutant needs the waiver.
                extra += ["-Wno-tautological-overlap-compare"]
        elif name == "sys_inode.c":
            # Host off_t is wider than the target's unsigned-size expression.
            extra += ["-Wno-sign-compare", "-Dvhangup=gate_vhangup"]
        elif name == "ufs_syscalls2.c":
            extra += [
                "-Wno-pointer-to-int-cast",
                "-Wno-int-to-pointer-cast",
                "-Dumask=gate_umask",
                "-Dlseek=gate_lseek",
                "-Dfsync=gate_fsync",
                "-Dutimes=gate_utimes",
                "-Dstatfs=gate_statfs",
                "-Dfstatfs=gate_fstatfs",
            ]
        elif name == "ufs_subr.c":
            extra += ["-Dsync=gate_sync"]
        obj = work / (name + ".o")
        run(CC + common + includes + extra + ["-c", str(source), "-o", str(obj)])
        objects.append(str(obj))
    harness = work / "harness.o"
    run(
        CC
        + [
            "-std=gnu17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-c",
            str(TESTS / "hostkern.c"),
            "-o",
            str(harness),
        ]
    )
    binary = work / "gate"
    test = TESTS / ("time_io_test.c" if suite == "io" else "cred_test.c")
    run(
        CC
        + common
        + includes
        + [GC_FLAG, str(test), str(TESTS / "hostkern_kern.c"), str(harness)]
        + objects
        + ["-o", str(binary)]
    )
    return subprocess.run([str(binary)], capture_output=True, text=True)


def main():
    cases = [
        ("baseline-io", "io", {}, False),
        ("baseline-credentials", "credentials", {}, False),
        (
            "clock-positive-lower-bound",
            "io",
            {"kern_time.c": ("adjust < -0x8000", "adjust < 0x8000")},
            True,
        ),
        (
            "discard-io-sync",
            "io",
            {
                "sys_inode.c": (
                    "    if (type == IFCHR) {",
                    "    if (type == IFREG) ioflag &= ~IO_SYNC;\n    if (type == IFCHR) {",
                )
            },
            True,
        ),
        (
            "discard-write-error",
            "io",
            {"sys_inode.c": ("error = bwrite(bp);", "bwrite(bp);")},
            True,
        ),
        ("omit-metadata-sync", "io", {"sys_inode.c": ("error = syncip(ip);", "error = 0;")}, True),
        (
            "narrow-process-group",
            "credentials",
            {"kern_prot.c": ("if (!PGRP_VALID(uap->pgrp))", "if (0)")},
            True,
        ),
        (
            "accept-group-sentinel",
            "credentials",
            {"kern_prot.c": ("if (groups[i] == NOGROUP)", "if (0)")},
            True,
        ),
    ]
    with tempfile.TemporaryDirectory(prefix="discobsd-correctness-") as tmp:
        base = Path(tmp)
        for label, suite, changes, negative in cases:
            work = base / label
            (work / "machine").mkdir(parents=True)
            for header in (ROOT / "sys/arch/rp2040/include").glob("*.h"):
                text = "#include <rp2040/include/" + header.name + ">\n"
                if header.name == "intr.h":
                    text += '#include "' + str(TESTS / "hostintr.h") + '"\n'
                (work / "machine" / header.name).write_text(text)
            result = build(work, suite, changes)
            expected = 1 if negative else 0
            if result.returncode != expected:
                raise RuntimeError(label + ": unexpected result\n" + result.stdout + result.stderr)
            print(
                label
                + ": "
                + ("calibrated rejection" if negative else (result.stdout + result.stderr).strip())
            )


if __name__ == "__main__":
    main()
