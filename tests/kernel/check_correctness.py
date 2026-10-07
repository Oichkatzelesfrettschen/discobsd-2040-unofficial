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
    sources = {
        "io": ["kern_time.c", "sys_inode.c", "ufs_subr.c", "ufs_syscalls2.c"],
        "credentials": ["kern_prot.c", "kern_prot2.c", "kern_proc.c"],
        "bmap": ["ufs_bmap.c"],
        "synch": ["vm_sched.c"],
    }[suite]
    # A mutated kernel header goes into the private include directory, which
    # -I lists ahead of the tree's sys directory.
    for name, (old, new) in changes.items():
        if name.endswith(".h"):
            text = (ROOT / "sys/sys" / name).read_text()
            if text.count(old) != 1:
                raise RuntimeError("mutation anchor is ambiguous: " + name)
            (work / "sys").mkdir(exist_ok=True)
            (work / "sys" / name).write_text(text.replace(old, new))
    if suite == "bmap":
        common += ["-Dfree=ufs_free"]
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
        elif name == "kern_synch.c":
            # HASH() folds a wait channel through int, narrower than a host
            # pointer; the target's pointers are int-sized.
            extra += ["-Wno-pointer-to-int-cast"]
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
    test = (
        TESTS
        / {
            "io": "time_io_test.c",
            "credentials": "cred_test.c",
            "bmap": "bmap_write_test.c",
            "synch": "synch_meter_test.c",
        }[suite]
    )
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
        ("baseline-bmap", "bmap", {}, False),
        ("baseline-synch", "synch", {}, False),
        (
            "discard-direct-init-error",
            "bmap",
            {
                "ufs_bmap.c": (
                    "error = bwrite(bp);\n                if (error)",
                    "error = bwrite(bp);\n                if (0)",
                )
            },
            True,
        ),
        (
            "discard-root-init-error",
            "bmap",
            {
                "ufs_bmap.c": (
                    "error = bwrite(bp);\n            if (error)",
                    "error = bwrite(bp);\n            if (0)",
                )
            },
            True,
        ),
        (
            "discard-child-init-error",
            "bmap",
            {"ufs_bmap.c": ("error = bwrite(nbp);", "error = bwrite(nbp); error = 0;")},
            True,
        ),
        ("leak-failed-allocation", "bmap", {"ufs_bmap.c": ("free(ip, nb);", "(void)ip;")}, True),
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
        (
            "narrow-vm-counters",
            "synch",
            {
                "vmmeter.h": ("    u_int       v_syscall;", "    u_short     v_syscall;"),
            },
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
