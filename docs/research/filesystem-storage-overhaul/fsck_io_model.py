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

"""Count the I/O of sbin/fsck -p over UFS images of the sizes a card holds.

Builds each image with tools/fsutil (one inode per 16 KiB by default), empty
or filled to about half its blocks with copies of a content directory, runs
the host-built fsck of tests/fsck_inode_range under fsck_io_shim.so and
records, per image: the superblock geometry fsck reports, its read calls and
bytes on the image, its scratch-device reads, appends and rewrites (the
maps that do not fit fsck's 16 in-core buffers go to /dev/temp0, which the
shim sends to a file), and the user-mode x86 instruction count from perf
when perf is present ("not run" otherwise).

Calibration runs first and decides whether the rows count:
  known good  on an empty image the shim's read bytes equal the bytes strace
              records on the descriptors fsck opened on the image (when
              strace is present), and lie between the geometric floor (the
              superblock, every inode block and every free-list chain block,
              once each) and that floor plus 8 KiB (the root directory and a
              reread inode chunk);
  known bad   an image whose superblock free count is lowered by one must
              make fsck report the repair, and an image whose maps exceed
              the in-core pool must open the scratch device.

Usage: fsck_io_model.py FSUTIL FSCK_HOST SHIM_SO CONTENT_DIR WORKDIR OUT_TSV
WORKDIR must be empty or absent; the caller removes it afterwards.
Exit status: 0 measured and calibrated, 1 calibration failed, 2 usage.
"""

import os
import re
import shutil
import struct
import subprocess
import sys

SIZES_KB = (988, 8192, 32768, 57344, 61440, 131072, 262144, 1048576)
BLOCK = 1024
NICFREE = 200
INODES_PER_BLOCK = 16
MAXPATH = 60  # fsutil truncates a path past 63 bytes and then cannot find it
SUMMARY = re.compile(r"(\d+) files, (\d+) used, (\d+) free")


def superblock(image):
    """Return (isize, fsize, nfree, tfree) from the image's superblock."""
    with open(image, "rb") as handle:
        data = handle.read(BLOCK)
    isize, fsize = struct.unpack_from("<II", data, 4)
    nfree = struct.unpack_from("<i", data, 16)[0]
    tfree_offset = 16 + 4 + 4 * NICFREE + 4 + 4 * 32 + 4 * 4 + 4
    tfree = struct.unpack_from("<I", data, tfree_offset)[0]
    return isize, fsize, nfree, tfree


def chain_blocks(image, nfree):
    """Count the free-list chain blocks reachable from the superblock."""
    with open(image, "rb") as handle:
        handle.seek(20)
        head = struct.unpack("<i", handle.read(4))[0] if nfree > 0 else 0
        count = 0
        while head > 0:
            count += 1
            handle.seek(head * BLOCK)
            block = handle.read(8)
            entries, head = struct.unpack("<ii", block)
            if entries <= 0:
                break
    return count


def stage(content, target, budget_bytes, budget_files):
    """Copy files of content into target until either budget is reached."""
    used = files = 0
    replica = 0
    while True:
        for root, _dirs, names in os.walk(content):
            for name in sorted(names):
                source = os.path.join(root, name)
                relative = os.path.relpath(root, content)
                inside = os.path.normpath(os.path.join(f"/r{replica}", relative, name))
                if not os.path.isfile(source) or os.path.islink(source) or \
                        len(inside) > MAXPATH:
                    continue
                size = os.path.getsize(source)
                blocks = (size + BLOCK - 1) // BLOCK + (1 if size > 7 * BLOCK else 0)
                if used + blocks * BLOCK > budget_bytes or files + 1 > budget_files:
                    return used, files
                directory = os.path.join(target, f"r{replica}", relative)
                os.makedirs(directory, exist_ok=True)
                shutil.copyfile(source, os.path.join(directory, name))
                used += blocks * BLOCK
                files += 1
        replica += 1


def run_fsck(fsck, shim, image, temp, report):
    """Run fsck -p on image under the shim; return (status, output, counts, insns)."""
    open(temp, "wb").close()
    env = dict(os.environ, FSCK_IMAGE=image, FSCK_TEMP=temp, FSCK_IO_REPORT=report)
    perf = shutil.which("perf")
    command = ["env", f"LD_PRELOAD={shim}", fsck, "-p", image]
    stat_file = report + ".perf"
    if perf:
        command = [perf, "stat", "-x,", "-e", "instructions:u", "-o", stat_file] + command
    done = subprocess.run(command, env=env, capture_output=True, text=True, timeout=600)
    with open(report, encoding="ascii") as handle:
        counts = [int(field) for field in handle.read().split()]
    instructions = "not run"
    if perf and os.path.exists(stat_file):
        with open(stat_file, encoding="ascii") as handle:
            for line in handle:
                fields = line.split(",")
                if len(fields) > 2 and fields[2].startswith("instructions"):
                    instructions = fields[0]
    return done.returncode, done.stdout + done.stderr, counts, instructions


def strace_read_bytes(fsck, image, trace):
    """Sum the bytes fsck reads on the image's descriptors, as strace records."""
    strace = shutil.which("strace")
    if strace is None:
        return None
    subprocess.run([strace, "-e", "trace=open,openat,read,close", "-o", trace,
                    fsck, "-p", image], capture_output=True, timeout=600)
    opened = re.compile(r'^open(?:at)?\((?:AT_FDCWD, )?"([^"]*)".*= (\d+)$')
    closed = re.compile(r"^close\((\d+)\)")
    read = re.compile(r"^read\((\d+), .*= (\d+)$")
    image_fds, total = set(), 0
    with open(trace, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.rstrip("\n")
            match = opened.match(line)
            if match:
                if match.group(1) == image:
                    image_fds.add(int(match.group(2)))
                continue
            match = closed.match(line)
            if match:
                image_fds.discard(int(match.group(1)))
                continue
            match = read.match(line)
            if match and int(match.group(1)) in image_fds:
                total += int(match.group(2))
    return total


def make_image(fsutil, image, size_kb, directory=None):
    command = [fsutil, "--new", f"--size={size_kb}", image]
    if directory:
        command.append(directory)
    subprocess.run(command, check=True, capture_output=True)


def calibrate(fsutil, fsck, shim, work):
    """Return a list of calibration failures; empty means the harness counts."""
    failures = []
    image = os.path.join(work, "cal-empty.img")
    make_image(fsutil, image, 32768)
    isize, _fsize, nfree, _tfree = superblock(image)
    floor = (1 + (isize - 1) + chain_blocks(image, nfree)) * BLOCK
    status, output, counts, _ = run_fsck(fsck, shim, image, image + ".tmp",
                                         image + ".rep")
    if status != 0 or not floor <= counts[1] <= floor + 8 * BLOCK:
        failures.append(f"known good: read {counts[1]} bytes, floor {floor}, "
                        f"status {status}: {output.strip()}")
    traced = strace_read_bytes(fsck, image, os.path.join(work, "cal.strace"))
    if traced is None:
        print("fsck_io_model: strace cross-check not run (no strace)")
    elif traced != counts[1]:
        failures.append(f"known good: shim counted {counts[1]} bytes, strace {traced}")
    bad = os.path.join(work, "cal-tfree.img")
    shutil.copyfile(image, bad)
    with open(bad, "r+b") as handle:
        offset = 16 + 4 + 4 * NICFREE + 4 + 4 * 32 + 4 * 4 + 4
        handle.seek(offset)
        tfree = struct.unpack("<I", handle.read(4))[0]
        handle.seek(offset)
        handle.write(struct.pack("<I", tfree - 1))
    status, output, counts, _ = run_fsck(fsck, shim, bad, bad + ".tmp", bad + ".rep")
    if "FREE BLK COUNT WRONG" not in output or counts[2] == 0:
        failures.append(f"known bad: a lowered free count drew no repair: {output.strip()}")
    large = os.path.join(work, "cal-large.img")
    make_image(fsutil, large, 65536)
    status, output, counts, _ = run_fsck(fsck, shim, large, large + ".tmp",
                                         large + ".rep")
    if counts[6] == 0 or counts[4] == 0:
        failures.append(f"known bad: maps over the pool opened no scratch: {counts}")
    return failures


def main(argv):
    if len(argv) != 6:
        print(__doc__, file=sys.stderr)
        return 2
    fsutil, fsck, shim, content, work, out = (os.path.abspath(a) for a in argv)
    os.makedirs(work, exist_ok=False)
    failures = calibrate(fsutil, fsck, shim, work)
    for failure in failures:
        print(f"fsck_io_model: CALIBRATION FAIL {failure}", file=sys.stderr)
    if failures:
        return 1
    print("fsck_io_model: calibration passed (known good and both known bad)")
    with open(out, "w", encoding="utf-8") as table:
        table.write("size_kb\tfill\tisize\tfsize\tinodes\tfiles\tused\tfree\t"
                    "chain_blocks\timage_reads\timage_read_kb\ttemp_reads\t"
                    "temp_appends\ttemp_rewrites\tx86_instructions\tstatus\n")
        for size_kb in SIZES_KB:
            for fill in ("empty", "half"):
                image = os.path.join(work, f"fs-{size_kb}-{fill}.img")
                directory = None
                if fill == "half":
                    directory = os.path.join(work, f"stage-{size_kb}")
                    os.makedirs(directory)
                    stage(content, directory, size_kb * BLOCK // 2,
                          size_kb // 16 * 8 // 10)
                make_image(fsutil, image, size_kb, directory)
                isize, fsize, nfree, _ = superblock(image)
                status, output, counts, insns = run_fsck(
                    fsck, shim, image, image + ".tmp", image + ".rep")
                match = SUMMARY.search(output)
                files, used, free = match.groups() if match else ("?", "?", "?")
                row = (size_kb, fill, isize, fsize, (isize - 1) * INODES_PER_BLOCK,
                       files, used, free, chain_blocks(image, nfree), counts[0],
                       counts[1] // BLOCK, counts[3], counts[4], counts[5], insns,
                       status)
                table.write("\t".join(str(field) for field in row) + "\n")
                table.flush()
                print("fsck_io_model: " + " ".join(str(field) for field in row),
                      flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
