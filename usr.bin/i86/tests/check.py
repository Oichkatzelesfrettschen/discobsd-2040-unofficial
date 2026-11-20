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
"""Host test of i86: the guest of guest.asm on every processor model.

    check.py DIR [--no-mutants]
    check.py DIR --vectors COMMAND

DIR is usr.bin/i86. The test builds i86-host with HOSTCC, makes a 360 KB
floppy image from guest.hex, and runs it

  - on the 8086, the 80186 and the 80286, with 8 resident pages, with 64
    and with all 640, and with 256 KB of guest memory, comparing the
    display `-d` prints with the lines the guest must produce and the page
    statistics with the traffic each size implies;
  - with the display drawn, replaying the terminal output on a model
    terminal (cursor addressing, scroll region, line feed) and comparing
    that screen with the `-d` display.

It also executes the processor vectors of sst8088-sample.txt.gz on the 8086
model and of sst80286-sample.txt.gz on the 80286 model (README.md)
on cpu.c through tests/sstrun and compares registers, defined flags and
memory.

Then every mutant below must fail the same test. When nasm is installed,
guest.hex must equal the assembly of guest.asm; without nasm that
comparison is reported as not run.

The second form runs the processor vectors alone through COMMAND, a built
tests/sstrun with the words that start it, such as a qemu-arm prefix.
"""

import gzip
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

FLAGS = [
    "-std=c17",
    "-pedantic-errors",
    "-D_DEFAULT_SOURCE",
    "-DHOSTBUILD",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wshadow",
    "-O1",
    "-I.",
]
SOURCES = ["main.c", "pc.c", "cpu.c", "mem.c"]
HEADERS = ["i86.h", "mem.h", "pc.h"]
KEYS = "aB1"
TAIL = [
    "MEM %04X OK",
    "IDIV %s",
    "WRAP OK",
    "DISK OK 27090101",
    "TICK OK",
    "SPIN OK",
    "HOOK OK",
    "VID OK",
    "a=1E61 B=3042 1=0231",
    "DONE",
]

MUTANTS = [
    ("cpu.c", "(fl & F_AF) && c->model != I86_286 ? 0x9f : 0x99;", "(fl & F_AF) ? 0x9f : 0x99;"),
    ("cpu.c", "if (c->model == I86_286 && al < 6)\n\t\t\t\tfl |= F_CF;", ";"),
    ("cpu.c", "UNDEFINED_286(d.reg > 3 || d.reg == S_CS);", ";"),
    ("cpu.c", "UNDEFINED_286(d.mod == 3);\n\t\tc->r[d.reg] = d.off;", "c->r[d.reg] = d.off;"),
    (
        "cpu.c",
        "if (c->model == I86_286) {\n\t\t\t\tunsigned ax",
        "if (c->model == I86_8086) {\n\t\t\t\tunsigned ax",
    ),
    ("cpu.c", "fault(c, 0, ip0, c->model == I86_286);\n}", "fault(c, 0, ip0, 0);\n}"),
    (
        "cpu.c",
        "a = c->r[R_SP];\n\t\tfor (b = 0; b < 8; b++)",
        "a = c->r[R_SP] - 2;\n\t\tfor (b = 0; b < 8; b++)",
    ),
    ("cpu.c", "level = fetch8(c) & 31;", "level = fetch8(c);"),
    ("mem.c", "stored[pg >> 3] |= (uint8_t)(1u << (pg & 7));", ";"),
    ("mem.c", "ftab[i].flags |= FR_DIRTY;", ";"),
    ("mem.c", "if (w[i])\n\t\t\treturn (0);", "if (w[i] == 1)\n\t\t\treturn (0);"),
    (
        "mem.c",
        "mem_rmap[pg] = mem_wmap[pg] = 0;\n\t\t\tcontinue;",
        "mem_rmap[pg] = 0;\n\t\t\tcontinue;",
    ),
    ("mem.c", "(off_t)page << MEM_PGSHIFT", "(off_t)page << (MEM_PGSHIFT - 1)"),
    (
        "mem.h",
        "if (b && (a & MEM_PGMASK) != MEM_PGMASK) {\n\t\tuint8_t *p",
        "if (b) {\n\t\tuint8_t *p",
    ),
    (
        "cpu.c",
        "if (op == 0x50 + R_SP && c->model != I86_286)",
        "if (op == 0x50 + R_SP && c->model == I86_286)",
    ),
    (
        "cpu.c",
        "if (c->model != I86_8086)\n\t\tcnt &= 31;",
        "if (c->model == I86_286)\n\t\tcnt &= 31;",
    ),
    ("cpu.c", "c->fl &= (uint16_t)~(F_IF | F_TF);\n\tc->ip", "c->fl &= (uint16_t)~F_TF;\n\tc->ip"),
    ("cpu.c", "if ((a ^ res) & (b ^ res) & sign)", "if ((a ^ b) & sign)"),
    ("cpu.c", "if (!((0x6996 >> (p & 15)) & 1))", "if ((0x6996 >> (p & 15)) & 1)"),
    ("cpu.c", "(fl & F_AF) && c->model != I86_286 ? 0x9f : 0x99;", "0x99;"),
    ("cpu.c", "!= (d->rep == 0xf3))\n\t\t\tbreak;", "!= (d->rep == 0xf2))\n\t\t\tbreak;"),
    (
        "cpu.c",
        "case 2:\toff = c->r[R_BP] + c->r[R_SI]; seg = S_SS; break;",
        "case 2:\toff = c->r[R_BP] + c->r[R_SI]; break;",
    ),
    (
        "cpu.c",
        "neg_q = neg_n ^ (rep && c->model == I86_8086);",
        "neg_q = neg_n ^ (rep && c->model == I86_286);",
    ),
    (
        "cpu.c",
        "if (off == 0xffff)\n\t\treturn (mem_rb(",
        "if (off == 0x1ffff)\n\t\treturn (mem_rb(",
    ),
    ("cpu.c", "if (off == 0xffff) {\n\t\tmem_wb(", "if (off == 0x1ffff) {\n\t\tmem_wb("),
    (
        "cpu.c",
        "if (q > (w ? 0x7fffu : 0x7fu) + (neg_q && c->model != I86_8086))",
        "if (q > (w ? 0x7fffu : 0x7fu) + (unsigned)neg_q)",
    ),
    (
        "cpu.c",
        "a = w ? 0 : fetch16(c);\n\t\tc->ip = (uint16_t)pop(c);\n\t\tc->r[R_SP] += a;",
        "a = w ? 0 : fetch16(c);\n\t\tc->ip = (uint16_t)pop(c);",
    ),
    ("cpu.c", "if (c->events) {", "if (c->halted) {"),
    ("pc.c", "+ sec - 1;", "+ sec;"),
    ("pc.c", "if (fn == 3 && d->ro)", "if (fn == 3)"),
    ("pc.c", "memmove(shadow, shadow + 2 * cols * n, 2 * cols * (h - n));", ";"),
    ("pc.c", "onum(col + 1);\n\toputc('H');", "onum(col);\n\toputc('H');"),
    ("pc.c", "tx = col + 1 < cols ? (int)col + 1 : -1;", "tx = (int)col + 2;"),
    (
        "pc.c",
        "return ((unsigned)(p - keys_hi + 2) << 8 | ch);",
        "return ((unsigned)(p - keys_hi + 3) << 8 | ch);",
    ),
    ("pc.c", "if (++t >= 0x1800b0) {", "if (t >= 0x1800b0) {"),
    ("pc.c", "mem_ww(4 * 8, ROM_OFF + ROM_TIMER);", ";"),
    ("main.c", "(!cpu.halted || cpu.irq); i++)", "!cpu.halted; i++)"),
    ("main.c", "mem_failed = store_failed;", "(void)store_failed;"),
    ("main.c", "if (caught)\n\t\t\thost_exit(128 + (int)caught);", ";"),
    ("main.c", '"/tmp/i86.%u", (unsigned)getpid() & 0xffffffu', '"/tmp/i86.%u", 0u'),
    ("mem.c", "O_RDWR | O_CREAT | O_EXCL", "O_RDWR | O_CREAT | O_TRUNC"),
    ("mem.c", "unlink(store_path);", ";"),
    ("pc.c", "mbr[0x1be + 5] != 0xff) {", "1) {"),
    ("pc.c", "d->spt = mbr[0x1be + 6] & 63;", "d->spt = mbr[0x1be + 6] & 31;"),
    (
        "pc.c",
        "else if (dl >= 0x80 && dl < 0x82)\n\t\td = &disks[dl - 0x80 + 2];",
        "else if (dl >= 0x80 && dl < 0x82)\n\t\td = &disks[dl - 0x80];",
    ),
]


class Failure(Exception):
    pass


def build(src, out, cc, sources=None):
    cmd = [cc] + FLAGS + ["-o", out] + (sources or SOURCES)
    done = subprocess.run(cmd, cwd=src, capture_output=True, text=True, check=False)
    if done.returncode != 0:
        raise Failure("build failed:\n" + done.stderr)


def guest_image(src, path):
    with open(os.path.join(src, "tests", "guest.hex")) as handle:
        data = bytes.fromhex("".join(handle.read().split()))
    if len(data) % 512 or data[510:512] != b"\x55\xaa":
        raise Failure("guest.hex is not a boot program")
    with open(path, "wb") as handle:
        handle.write(data)
        handle.truncate(360 * 1024)
    return data


def run(binary, work, args, timeout=10):
    image = os.path.join(work, "run.img")
    shutil.copyfile(os.path.join(work, "guest.img"), image)
    store = os.path.join(work, "page")
    cmd = [binary, "-T", "-v", "-s", store, "-a", image, "-k", KEYS] + args
    try:
        done = subprocess.run(cmd, capture_output=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        raise Failure("timeout: " + " ".join(args)) from None
    if done.returncode != 0:
        raise Failure("exit %d: %s" % (done.returncode, " ".join(args)))
    if os.path.exists(store):
        raise Failure("page file left behind: " + " ".join(args))
    stats = re.search(
        rb"pages: (\d+) soft, (\d+) zero, (\d+) in, (\d+) out, "
        rb"(\d+) elided, (\d+) clean",
        done.stderr,
    )
    if stats is None:
        raise Failure("no statistics: " + " ".join(args))
    names = ["soft", "zero", "in", "out", "elided", "clean"]
    return done.stdout, dict(zip(names, (int(x) for x in stats.groups())))


def hard_disk(work, heads_byte, spt):
    """The guest program as the boot sector of a 2 MB hard disk image whose
    first partition entry ends on the given head byte and sector."""
    with open(os.path.join(work, "guest.img"), "rb") as handle:
        data = bytearray(handle.read(4 * 512))
    data[0x1BE + 5] = heads_byte
    data[0x1BE + 6] = spt
    path = os.path.join(work, "hard.img")
    with open(path, "wb") as handle:
        handle.write(data)
        handle.truncate(2048 * 1024)
    return path


def sector(path, lba):
    with open(path, "rb") as handle:
        handle.seek(lba * 512)
        return handle.read(512)


PATTERN = bytes((3 + 7 * i) & 0xFF for i in range(512))


def test_disks(binary, work):
    """Geometry: the sector the guest writes at cylinder 1 lands where the
    image's geometry puts it, and the reported geometry is that one."""
    store = os.path.join(work, "page")
    image = os.path.join(work, "run.img")
    shutil.copyfile(os.path.join(work, "guest.img"), image)
    subprocess.run(
        [binary, "-q", "-T", "-s", store, "-a", image, "-k", KEYS], timeout=10, check=True
    )
    if sector(image, 18) != PATTERN:
        raise Failure("floppy: cylinder 1 is not sector 18 of a 360 KB image")
    # 4 heads of 40 sectors: 25 cylinders in 2 MB, cylinder 1 at 160. A
    # head byte of FFh names no geometry: 16 heads of 63 sectors apply.
    for heads_byte, spt, lba, shown in ((3, 40, 160, "18280301"), (0xFF, 40, 1008, "033F0F01")):
        image = hard_disk(work, heads_byte, spt)
        done = subprocess.run(
            [binary, "-d", "-T", "-s", store, "-h", image, "-k", KEYS],
            capture_output=True,
            timeout=10,
            check=False,
        )
        what = "hard disk, head byte %02X" % heads_byte
        if done.returncode != 0:
            raise Failure("%s: exit %d" % (what, done.returncode))
        if ("DISK OK " + shown) not in done.stdout.decode("latin-1"):
            raise Failure("%s: geometry line is not %s" % (what, shown))
        if sector(image, lba) != PATTERN:
            raise Failure("%s: cylinder 1 is not sector %d" % (what, lba))


def test_endings(binary, work):
    """Every way out removes the page file and reports itself."""
    image = os.path.join(work, "run.img")
    shutil.copyfile(os.path.join(work, "guest.img"), image)
    # A page file path that exists is refused and left as it was.
    store = os.path.join(work, "occupied")
    with open(store, "w") as handle:
        handle.write("not a page file\n")
    done = subprocess.run(
        [binary, "-q", "-T", "-f", "8", "-s", store, "-a", image, "-k", KEYS],
        capture_output=True,
        timeout=10,
        check=False,
    )
    kept = False
    if os.path.exists(store):
        with open(store) as handle:
            kept = handle.read() == "not a page file\n"
        os.remove(store)
    if done.returncode != 3 or not kept or b"page file I/O failed" not in done.stderr:
        raise Failure(
            "occupied page file: exit %d, file kept: %s, said %r"
            % (done.returncode, kept, done.stderr)
        )
    # A termination signal while the guest waits for a key.
    store = os.path.join(work, "page")
    with subprocess.Popen(
        [binary, "-q", "-T", "-f", "8", "-s", store, "-a", image], stdin=subprocess.PIPE
    ) as child:
        for _ in range(200):
            if os.path.exists(store):
                break
            time.sleep(0.01)
        else:
            child.kill()
            raise Failure("signal: no page file appeared")
        time.sleep(0.1)
        child.send_signal(signal.SIGTERM)
        try:
            status = child.wait(timeout=10)
        except subprocess.TimeoutExpired:
            child.kill()
            raise Failure("signal: the emulator ignores SIGTERM") from None
    if status != 128 + signal.SIGTERM or os.path.exists(store):
        raise Failure("signal: exit %d, page file left: %s" % (status, os.path.exists(store)))
    # Two emulators started without -s do not share a page file.
    stores = set()
    for _ in range(2):
        with subprocess.Popen(
            [binary, "-q", "-T", "-f", "8", "-a", image], stdin=subprocess.PIPE
        ) as child:
            name = "/tmp/i86.%d" % child.pid
            for _ in range(200):
                if os.path.exists(name):
                    stores.add(name)
                    break
                time.sleep(0.01)
            child.send_signal(signal.SIGTERM)
            child.wait(timeout=10)
            if os.path.exists(name):
                raise Failure("default page file %s left behind" % name)
    if len(stores) != 2:
        raise Failure("default page file is not named by process: %r" % sorted(stores))


def check_screen(lines, cpu, kbytes, what):
    lines = [line.rstrip() for line in lines]
    while lines and lines[-1] == "":
        lines.pop()
    idiv = "TRAP" if cpu == "8086" else "80"
    want = ["CPU " + cpu] + [t.replace("%04X", "%04X" % kbytes).replace("%s", idiv) for t in TAIL]
    if lines[-len(want) :] != want:
        raise Failure(
            "%s: display ends\n  %s\nwant\n  %s"
            % (what, "\n  ".join(lines[-len(want) :]), "\n  ".join(want))
        )
    if not lines[0].startswith("DIRECT") or lines[1][5:8] != "ZZZ":
        raise Failure("%s: display memory cells missing" % what)
    if "LINE 0001" not in lines:
        raise Failure("%s: scrolled lines missing" % what)


def terminal(stream, cols=80, rows=25):
    """The screen a terminal shows after the emulator's output."""
    screen = [[" "] * cols for _ in range(rows)]
    row = col = 0
    top, bottom = 0, rows - 1
    i = 0
    text = stream.decode("latin-1")
    while i < len(text):
        ch = text[i]
        i += 1
        if ch == "\x1b":
            match = re.match(r"\[([?0-9;]*)([A-Za-z])", text[i:])
            if match is None:
                raise Failure("terminal: unknown escape at %d" % i)
            i += match.end()
            args, final = match.groups()
            nums = [int(x) for x in args.strip("?").split(";") if x]
            if final == "H":
                row = (nums[0] if nums else 1) - 1
                col = (nums[1] if len(nums) > 1 else 1) - 1
                if not (0 <= row < rows + 1 and 0 <= col < cols):
                    raise Failure("terminal: cursor to %d,%d" % (row, col))
            elif final == "J":
                screen = [[" "] * cols for _ in range(rows)]
            elif final == "r":
                top = (nums[0] - 1) if nums else 0
                bottom = (nums[1] - 1) if len(nums) > 1 else rows - 1
            elif final not in "mhl":
                raise Failure("terminal: escape %s" % final)
        elif ch == "\n":
            if row == bottom:
                del screen[top]
                screen.insert(bottom, [" "] * cols)
            elif row < rows - 1:
                row += 1
        elif ch == "\r":
            col = 0
        elif ch == "\a":
            pass
        else:
            if row >= rows or col >= cols:
                raise Failure("terminal: write at %d,%d" % (row, col))
            screen[row][col] = ch
            col += 1
    return ["".join(line) for line in screen]


def test_vectors(sstrun, src, work, frames="1024", suite="8088"):
    """cpu.c against the sampled vectors of one processor."""
    path = os.path.join(src, "tests", "sst%s-sample.txt.gz" % suite)
    model = "0" if suite == "8088" else "2"
    with gzip.open(path, "rt") as handle:
        vectors = [line.rstrip("\n").split("|") for line in handle]
    lines = []
    for _, before, _, store, _, compare in vectors:
        pairs = [int(x, 16) for x in store.split()]
        triples = [int(x, 16) for x in compare.split()]
        parts = [int(x, 16) for x in before.split()]
        parts += [len(pairs) // 2] + pairs
        parts += [len(triples) // 3] + triples[0::3]
        lines.append(" ".join(str(x) for x in parts))
    try:
        done = subprocess.run(
            sstrun.split() + [model, frames, os.path.join(work, "sstpage")],
            input="\n".join(lines) + "\n",
            capture_output=True,
            text=True,
            timeout=120,
            check=False,
        )
    except subprocess.TimeoutExpired:
        raise Failure("vectors: timeout") from None
    replies = done.stdout.splitlines()
    if done.returncode != 0 or len(replies) != len(vectors):
        raise Failure(
            "vectors: sstrun exit %d, %d of %d replies"
            % (done.returncode, len(replies), len(vectors))
        )
    for (form, _, mask, _, after, compare), reply in zip(vectors, replies):
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
            raise Failure("%s vector of form %s fails" % (suite, form))
    return len(vectors)


def test(binary, work):
    for cpu in ("8086", "186", "286"):
        for frames, kbytes in ((8, 640), (64, 640), (640, 640), (8, 256)):
            what = "cpu %s, %d frames, %d KB" % (cpu, frames, kbytes)
            out, stats = run(binary, work, ["-d", "-c", cpu, "-f", str(frames), "-m", str(kbytes)])
            check_screen(out.decode("latin-1").split("\n"), cpu, kbytes, what)
            if frames < kbytes:
                # The guest's zero pass leaves modified, empty pages.
                if not (
                    stats["out"] > 100
                    and stats["in"] > 100
                    and stats["elided"] > 100
                    and stats["clean"] > 100
                ):
                    raise Failure("%s: page traffic %r" % (what, stats))
            elif stats["in"] or stats["out"] or stats["elided"]:
                raise Failure("%s: page traffic %r" % (what, stats))
    plain, _ = run(binary, work, ["-d", "-f", "64"])
    drawn, _ = run(binary, work, ["-f", "64"])
    # Thirty-odd short lines: a frame that redraws the screen on every
    # scroll sends more than twice this.
    if len(drawn) > 3500:
        raise Failure("terminal output is %d bytes" % len(drawn))
    shown = terminal(drawn)
    want = (plain.decode("latin-1").split("\n") + [""] * 25)[:25]
    for number, (a, b) in enumerate(zip(shown, want)):
        if a.rstrip() != b.rstrip():
            raise Failure(
                "terminal line %d shows %r, display holds %r" % (number + 1, a.rstrip(), b.rstrip())
            )
    test_disks(binary, work)
    test_endings(binary, work)
    # A write-protected image refuses the guest's sector write.
    out, _ = run(binary, work, ["-d", "-r", "-f", "64"])
    if "DISK BAD" not in out.decode("latin-1"):
        raise Failure("read-only image accepted a write")


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    src = os.path.abspath(argv[1])
    cc = os.environ.get("HOSTCC", "cc")
    if len(argv) == 4 and argv[2] == "--vectors":
        with tempfile.TemporaryDirectory() as work:
            try:
                print(
                    "%d processor vectors pass through %s"
                    % (
                        test_vectors(argv[3], src, work)
                        + test_vectors(argv[3], src, work, suite="80286"),
                        argv[3],
                    )
                )
            except Failure as failure:
                sys.stderr.write("i86 check: %s\n" % failure)
                return 1
        return 0
    with tempfile.TemporaryDirectory() as work:
        try:
            data = guest_image(src, os.path.join(work, "guest.img"))
            nasm = shutil.which("nasm")
            if nasm:
                built = os.path.join(work, "guest.bin")
                subprocess.run(
                    [nasm, "-f", "bin", "-o", built, os.path.join(src, "tests", "guest.asm")],
                    check=True,
                )
                with open(built, "rb") as handle:
                    if handle.read() != data:
                        raise Failure("guest.hex differs from guest.asm")
                print("guest.hex matches guest.asm")
            else:
                print("NOT RUN guest.hex against guest.asm: nasm (package nasm) is not installed")
            binary = os.path.join(work, "i86-host")
            build(src, binary, cc)
            test(binary, work)
            print("i86 host test passed")
            vsources = ["tests/sstrun.c", "cpu.c", "mem.c"]
            sstrun = os.path.join(work, "sstrun")
            build(src, sstrun, cc, vsources)
            print("%d 8088 vectors pass" % test_vectors(sstrun, src, work))
            print("%d 80286 vectors pass" % test_vectors(sstrun, src, work, suite="80286"))
            # Sixteen resident pages: the same vectors through the page
            # store, which every instruction's memory then crosses.
            test_vectors(sstrun, src, work, "16")
            print("the vectors pass with 16 resident pages")
            if "--no-mutants" in argv:
                return 0
            for number, (name, old, new) in enumerate(MUTANTS, 1):
                tree = os.path.join(work, "mutant")
                shutil.rmtree(tree, ignore_errors=True)
                os.mkdir(tree)
                os.mkdir(os.path.join(tree, "tests"))
                for item in SOURCES + HEADERS + ["tests/sstrun.c"]:
                    shutil.copyfile(os.path.join(src, item), os.path.join(tree, item))
                with open(os.path.join(tree, name)) as handle:
                    text = handle.read()
                if text.count(old) != 1:
                    raise Failure(
                        "mutant %d: anchor occurs %d times in %s" % (number, text.count(old), name)
                    )
                with open(os.path.join(tree, name), "w") as handle:
                    handle.write(text.replace(old, new))
                mutant = os.path.join(work, "i86-mutant")
                build(tree, mutant, cc)
                build(tree, sstrun + "-mutant", cc, vsources)
                try:
                    test(mutant, work)
                    test_vectors(sstrun + "-mutant", src, work)
                    test_vectors(sstrun + "-mutant", src, work, suite="80286")
                except Failure:
                    continue
                raise Failure("mutant %d survives: %s: %r" % (number, name, old))
            print("%d mutants fail the test" % len(MUTANTS))
        except Failure as failure:
            sys.stderr.write("i86 check: %s\n" % failure)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
