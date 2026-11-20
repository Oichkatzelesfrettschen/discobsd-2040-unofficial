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
floppy image from guest.hexdump, and runs it

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
guest.hexdump must equal the assembly of guest.asm; without nasm that
comparison is reported as not run.

The second form runs the processor vectors alone through COMMAND, a built
tests/sstrun with the words that start it, such as a qemu-arm prefix.
"""

import gzip
import os
import pty
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import termios
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
    "DISK OK 270901010109",
    "TICK OK",
    "SPIN OK",
    "KBD OK",
    "MASK OK",
    "REMAP OK",
    "HOOK OK",
    "VID OK",
    "MODE OK",
    "a=1E61 B=3042 1=0231",
    "WAIT 0000",
    "IRQ 0882",
    "DONE",
]

MUTANTS = [
    ("pc.c", "c->ip -= 3;\n\t\t\t\tc->fl |= F_IF;", "c->ip -= 3;"),
    (
        "pc.c",
        "if (key_eof && kb_n == 0 && !kb_full)\n\t\t\thost_exit(0);\n\t\tpc_idle = 1;",
        "pc_idle = 1;",
    ),
    ("pc.c", "(cf.keys != NULL || key_eof))", "cf.keys != NULL)"),
    (
        "mem.c",
        "\treturn (store_unname());\n}",
        "\t(void)store_unname;\n\treturn (0);\n}",
    ),
    (
        "mem.c",
        "memcpy(dir, store_path, n);\n\tdir[n] = '\\0';",
        "memcpy(dir, store_path, n > 63 ? 63 : n);\n\tdir[n > 63 ? 63 : n] = '\\0';",
    ),
    (
        "mem.c",
        "named.st_dev == held.st_dev && named.st_ino == held.st_ino)",
        "named.st_dev != held.st_dev)",
    ),
    ("cpu.c", "if (tf && !c->inhibit && !faulted)", "if (tf && !c->inhibit)"),
    ("pc.c", "pic_base = v & 0xf8;", "pic_base = 8;"),
    ("pc.c", "pic_aeoi = v >> 1 & 1;", "pic_aeoi = 0;"),
    ("pc.c", "pic_irr = pic_isr = imask = pic_aeoi = 0;", "pic_irr = pic_isr = imask = 0;"),
    ("pc.c", "(c->irq && (c->fl & F_IF)))\n\t\t\tbreak;", "c->irq)\n\t\t\tbreak;"),
    (
        "pc.c",
        "if (c->halted && !(c->fl & F_IF) &&\n\t\t    (cf.keys != NULL || key_eof))\n"
        "\t\t\thost_exit(0);",
        ";",
    ),
    ("pc.c", "host_exit(4);", "host_exit(0);"),
    ("pc.c", "mem_rw((sp + 2) & (MEM_SIZE - 1)),", "mem_rw((sp + 4) & (MEM_SIZE - 1)),"),
    ("cpu.c", "(d.reg == 7 || (!w && d.reg > 1))", "(d.reg == 7 && !w)"),
    ("cpu.c", "if (c->model != I86_8086 && (d.reg == 7", "if (c->model == I86_286 && (d.reg == 7"),
    ("pc.c", "p[4] = (uint8_t)spt;", ";"),
    ("pc.c", "c->r[R_BX] = d->spt == 36 ? 6 : d->spt == 18 ?", "c->r[R_BX] = d->spt == 18 ?"),
    ("pc.c", "kb_put(scan | 0x80, 0);", ";"),
    ("pc.c", "pic_isr &= (uint8_t)(pic_isr - 1);", "pic_isr &= pic_isr;"),
    (
        "pc.c",
        "c->irq = (pic_irr & ~(unsigned)imask & above) != 0;",
        "c->irq = (pic_irr & above) != 0;",
    ),
    ("pc.c", "flags &= ~(unsigned)mod[i + 1];", "flags &= flags;"),
    ("pc.c", "if (key & K_SHIFT)\n\t\tkb_put(0x2a, 0);", ";"),
    ("pc.c", "mem_ww(4 * 9, ROM_OFF + ROM_KEY);", ";"),
    ("pc.c", "if (kb_n == 0 || kb_full || ((pic_irr | pic_isr) & 2))", "if (kb_n == 0)"),
    ("main.c", "signal(SIGPIPE, on_signal);", ";"),
    (
        "main.c",
        "if (cf.keys == NULL)\n\t\ttty_raw();",
        "if (!cf.quiet && cf.keys == NULL)\n\t\ttty_raw();",
    ),
    ("pc.c", "mode = (mode & 0x80) | 3;", "mode = (mode & 0x80) | 7;"),
    ("cpu.c", "if (b == 2)\n\t\t\t\t\ta |= F_IF;", ";"),
    ("pc.c", "if (cf.quiet)\n\t\t\t\thost_exit(0);\n\t\t\tmenu();", "menu();"),
    ("pc.c", "disks[i] = old;\n\t\t\t\tdisk_path[i] = slash;", "disk_path[i] = slash;"),
    ("pc.c", "if (ch == 'q' || ch < 0)\n\t\thost_exit(0);\n}", "}"),
    ("pc.c", "tx = ty = tattr = -1;\n\tif (ch == 'q' || ch < 0)", "if (ch == 'q' || ch < 0)"),
    ("pc.c", 'if (n) {\n\t\t\t\tn--;\n\t\t\t\toputs("\\b \\b");\n\t\t\t}', ";"),
    ("pc.c", "(name[n - 3] | 0x20) == 'i'", "(name[n - 3] | 0x20) == 'j'"),
    ("pc.c", 'memset(shadow, 0, sizeof(shadow));\n\toputs("\\033[2J");', 'oputs("\\033[2J");'),
    ("main.c", "t.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG);", ";"),
    ("main.c", "(void)tcsetattr(0, TCSANOW, &saved);", ";"),
    ("cpu.c", "(fl & F_AF) && c->model != I86_286 ? 0x9f : 0x99;", "(fl & F_AF) ? 0x9f : 0x99;"),
    ("cpu.c", "if (c->model == I86_286 && al < 6)\n\t\t\t\tfl |= F_CF;", ";"),
    ("cpu.c", "UNDEFINED_286(d.reg > 3 || d.reg == S_CS);", ";"),
    ("cpu.c", "UNDEFINED_286(d.mod == 3);\n\t\t/* The earlier", "/* The earlier"),
    ("cpu.c", "if (d.mod == 3)\n\t\t\tbreak;\n\t\tc->r[d.reg] = d.off;", "c->r[d.reg] = d.off;"),
    ("cpu.c", "if (d.mod == 3)\t\t/* as for 8Dh: no address, no effect */\n\t\t\tbreak;", ";"),
    ("main.c", "if (leaving) {\n\t\tpc_restore(0);\n\t\ttty_restore();", "if (leaving) {"),
    ("cpu.c", "if (++done == REP_BURST) {", "if (++done == 0x10000) {"),
    (
        "cpu.c",
        "c->ip = (uint16_t)ip0;\n\t\t\tbreak;\n\t\t}\n\t}",
        "c->ip = (uint16_t)(ip0 + 1);\n\t\t\tbreak;\n\t\t}\n\t}",
    ),
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
        "return (K_SHIFT | (unsigned)(p - keys_hi + 2) << 8 | ch);",
        "return (K_SHIFT | (unsigned)(p - keys_hi + 3) << 8 | ch);",
    ),
    ("pc.c", "if (++t >= 0x1800b0) {", "if (t >= 0x1800b0) {"),
    ("pc.c", "mem_ww(4 * 8, ROM_OFF + ROM_TIMER);", ";"),
    ("main.c", "(!cpu.halted ||\n\t\t    (cpu.irq && (cpu.fl & F_IF))); i++)", "!cpu.halted; i++)"),
    ("main.c", "mem_failed = store_failed;", "(void)store_failed;"),
    ("main.c", "if (caught)\n\t\t\thost_exit(128 + (int)caught);", ";"),
    ("main.c", '"/tmp/i86.%u", (unsigned)getpid() & 0xffffffu', '"/tmp/i86.%u", 0u'),
    ("mem.c", "O_RDWR | O_CREAT | O_EXCL", "O_RDWR | O_CREAT | O_TRUNC"),
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
    with open(os.path.join(src, "tests", "guest.hexdump")) as handle:
        data = bytes.fromhex("".join(handle.read().split()))
    if len(data) % 512 or data[510:512] != b"\x55\xaa":
        raise Failure("guest.hexdump is not a boot program")
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
    # Each floppy format reports its own geometry, drive type and
    # parameter table.
    for kbytes, shown in (
        (720, "4F0901010309"),
        (1200, "4F0F0101020F"),
        (1440, "4F1201010412"),
        (2880, "4F2401010624"),
    ):
        with open(image, "r+b") as handle:
            handle.truncate(kbytes * 1024)
        done = subprocess.run(
            [binary, "-d", "-T", "-s", store, "-a", image, "-k", KEYS],
            capture_output=True,
            timeout=10,
            check=False,
        )
        if ("DISK OK " + shown) not in done.stdout.decode("latin-1"):
            raise Failure("%d KB floppy: parameters are not %s" % (kbytes, shown))
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
    # A page file deep in a long directory path pages and leaves nothing.
    deep = os.path.join(work, "d" * 70, "e" * 70)
    os.makedirs(deep)
    done = subprocess.run(
        [binary, "-q", "-T", "-f", "8", "-s", os.path.join(deep, "page"), "-a", image, "-k", KEYS],
        capture_output=True,
        timeout=10,
        check=False,
    )
    if done.returncode != 0 or os.listdir(deep):
        raise Failure(
            "page file in a long path: exit %d, left %r" % (done.returncode, os.listdir(deep))
        )
    shutil.rmtree(os.path.join(work, "d" * 70))
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
        time.sleep(0.5)
        child.send_signal(signal.SIGTERM)
        try:
            status = child.wait(timeout=10)
        except subprocess.TimeoutExpired:
            child.kill()
            raise Failure("signal: the emulator ignores SIGTERM") from None
    if status != 128 + signal.SIGTERM or os.path.exists(store):
        raise Failure("signal: exit %d, page file left: %s" % (status, os.path.exists(store)))
    # An invalid opcode with no guest handler ends the session and names
    # the instruction; returning would run it again without end.
    bad = os.path.join(work, "bad.img")
    with open(bad, "wb") as handle:
        handle.write(b"\x0f\x0b" + bytes(508) + b"\x55\xaa")
        handle.truncate(360 * 1024)
    try:
        done = subprocess.run(
            [binary, "-q", "-T", "-c", "286", "-s", store, "-a", bad, "-k", KEYS],
            capture_output=True,
            timeout=10,
            check=False,
        )
    except subprocess.TimeoutExpired:
        raise Failure("invalid opcode: the emulator runs on") from None
    if done.returncode != 4 or b"undefined instruction at 0000:7C00" not in done.stderr:
        raise Failure("invalid opcode: exit %d, said %r" % (done.returncode, done.stderr))
    # A reader that closes the output: the session ends on SIGPIPE through
    # the same exit, so no page file stays.
    with subprocess.Popen(
        [binary, "-T", "-f", "8", "-s", store, "-a", image, "-k", KEYS],
        stdout=subprocess.PIPE,
    ) as child:
        child.stdout.close()
        try:
            status = child.wait(timeout=10)
        except subprocess.TimeoutExpired:
            child.kill()
            raise Failure("closed output: the emulator runs on") from None
    if status != 128 + signal.SIGPIPE or os.path.exists(store):
        raise Failure(
            "closed output: exit %d, page file left: %s" % (status, os.path.exists(store))
        )
    # Without a display the keys still arrive one at a time: Control-] ends
    # a quiet session with no Enter behind it.
    master, slave = pty.openpty()
    with subprocess.Popen(
        [binary, "-q", "-T", "-s", store, "-a", image],
        stdin=slave,
        stdout=subprocess.DEVNULL,
    ) as child:
        time.sleep(0.5)
        os.write(master, b"\x1d")
        try:
            status = child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            status = None
    os.close(master)
    os.close(slave)
    if status != 0:
        raise Failure("quiet session: Control-] did not end it (exit %r)" % status)
    # A guest that halts with interrupts off: a scripted session ends, and
    # an interactive one sleeps instead of spinning until Control-].
    dead = os.path.join(work, "dead.img")
    with open(dead, "wb") as handle:
        handle.write(b"\xfa\xf4" + bytes(508) + b"\x55\xaa")
        handle.truncate(360 * 1024)
    try:
        done = subprocess.run(
            [binary, "-q", "-T", "-s", store, "-a", dead, "-k", KEYS],
            timeout=10,
            check=False,
        )
    except subprocess.TimeoutExpired:
        raise Failure("halted guest: the scripted session runs on") from None
    if done.returncode != 0:
        raise Failure("halted guest: exit %d" % done.returncode)
    master, slave = pty.openpty()
    child = subprocess.Popen(
        [binary, "-q", "-s", store, "-a", dead], stdin=slave, stdout=subprocess.DEVNULL
    )
    time.sleep(1.0)
    os.write(master, b"\x1d")
    _, status, usage = os.wait4(child.pid, 0)
    child.returncode = os.waitstatus_to_exitcode(status)
    os.close(master)
    os.close(slave)
    spent = usage.ru_utime + usage.ru_stime
    if child.returncode != 0 or spent > 0.4:
        raise Failure(
            "halted guest: exit %d after %.2f s of processor time in one second"
            % (child.returncode, spent)
        )
    # At the end of input the session ends where the guest waits for the
    # keyboard, not before: the tests ahead of the first wait have run.
    try:
        done = subprocess.run(
            [binary, "-d", "-T", "-s", store, "-a", image],
            stdin=subprocess.DEVNULL,
            capture_output=True,
            timeout=10,
            check=False,
        )
    except subprocess.TimeoutExpired:
        raise Failure("end of input: the session runs on") from None
    if done.returncode != 0 or b"SPIN OK" not in done.stdout:
        raise Failure("end of input: exit %d before the keyboard wait" % done.returncode)
    # A dead guest whose input is at its end has nothing to wait for.
    try:
        done = subprocess.run(
            [binary, "-q", "-s", store, "-a", dead],
            stdin=subprocess.DEVNULL,
            timeout=10,
            check=False,
        )
    except subprocess.TimeoutExpired:
        raise Failure("halted guest: the session runs on after its input ended") from None
    if done.returncode != 0:
        raise Failure("halted guest at end of input: exit %d" % done.returncode)
    # The page file has no name while it is in use, so nothing at its path
    # is ever this program's to remove: a file put there during the
    # session stays. Without -s the name it is created under carries the
    # process number, which the open file's link shows where /proc exists.
    with subprocess.Popen(
        [binary, "-q", "-T", "-f", "8", "-a", image], stdin=subprocess.PIPE
    ) as child:
        time.sleep(0.5)
        links = "/proc/%d/fd" % child.pid
        held = []
        listed = os.path.isdir(links)
        if listed:
            held = [os.readlink(os.path.join(links, n)) for n in os.listdir(links)]
        name = "/tmp/i86.%d" % (child.pid & 0xFFFFFF)
        present = os.path.exists(name)
        if not present:
            with open(name, "w") as handle:
                handle.write("another program's file\n")
        child.send_signal(signal.SIGTERM)
        child.wait(timeout=10)
    if present:
        os.remove(name)
        raise Failure("page file %s keeps its name while in use" % name)
    kept = os.path.exists(name)
    if kept:
        os.remove(name)
    else:
        raise Failure("a file put at the page file's path was removed")
    if not listed:
        print("NOT RUN page file name by process: no /proc on this host")
    elif not any(link.startswith(name + " ") for link in held):
        raise Failure("default page file is not named by process: %r" % held)


def test_interactive(binary, work):
    """The emulator on a terminal: raw mode, keys as they are typed, the
    display drawn, the Control-] menu and the terminal mode restored. Each
    step waits for a text on the terminal, then types. The guest answers r
    by reading its boot sector again and printing the label there with a
    count, so the steps see a refused image leave the disk in the drive and
    an accepted one replace it."""
    image = os.path.join(work, "run.img")
    shutil.copyfile(os.path.join(work, "guest.img"), image)
    with open(image, "rb") as handle:
        other = bytearray(handle.read())
    if other[0x1F8:0x1FE] != b"DISK-A":
        raise Failure("guest image carries no label")
    other[0x1F8:0x1FE] = b"DISK-B"
    with open(os.path.join(work, "b.img"), "wb") as handle:
        handle.write(other)
    steps = [
        (b"VID", KEYS.encode()),
        (b"DONE", b"r"),
        (b"DISK-A1", b"\x1d"),
        (b"quit", b"a"),
        (b"b.img", b""),
        (b"image:", b"nosuch.img\r"),
        (b"cannot", b"x"),
        (b"DISK-A1", b"r"),
        (b"DISK-A2", b"\x1d"),
        (b"quit", b"a"),
        (b"image:", b"b.imx\x7fg\r"),
        (b"DISK-A2", b"r"),
        (b"DISK-B3", b"\x1d"),
        (b"quit", b"q"),
    ]
    master, slave = pty.openpty()
    before = termios.tcgetattr(slave)
    child = subprocess.Popen(
        [binary, "-T", "-s", os.path.join(work, "page"), "-a", image],
        stdin=slave,
        stdout=slave,
        stderr=subprocess.DEVNULL,
    )
    seen = b""
    at = step = 0
    deadline = time.time() + 30
    try:
        while time.time() < deadline and child.poll() is None:
            ready, _, _ = select.select([master], [], [], 0.05)
            if ready:
                seen += os.read(master, 4096)
            plain = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", seen)
            while step < len(steps):
                found = plain.find(steps[step][0], at)
                if found < 0:
                    break
                at = found + len(steps[step][0])
                if step == 0:
                    time.sleep(0.3)
                os.write(master, steps[step][1])
                step += 1
        status = child.wait(timeout=5) if step == len(steps) else None
    except subprocess.TimeoutExpired:
        status = None
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
    after = termios.tcgetattr(slave)
    os.close(master)
    os.close(slave)
    plain = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", seen)
    if step != len(steps):
        raise Failure("terminal session: %r never appeared" % steps[step][0])
    if status != 0:
        raise Failure("terminal session: exit %r" % status)
    if b"1E61" not in plain or b"3042" not in plain or b"0231" not in plain:
        raise Failure("terminal session: typed keys did not arrive")
    if b"DISK-B3" not in plain[plain.rfind(b"quit") :]:
        raise Failure("terminal session: the guest's screen is not drawn on leaving")
    # The guest counted timer ticks across its wait for the keys. With the
    # clock an instruction count, a third of a second of waiting is far
    # more than the four ticks the machine holds back for a guest that
    # keeps interrupts off.
    waited = re.search(rb"WAIT ([0-9A-F]{4})", plain)
    if waited is None or int(waited.group(1), 16) <= 16:
        raise Failure("terminal session: the clock stood still while the guest waited")
    if before != after:
        raise Failure("terminal session: terminal mode not restored")


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
        triples = [int(x, 16) for x in compare.split()]
        # A reply short of a field would drop that comparison unnoticed.
        if len(got) != 14 + len(triples) // 3:
            raise Failure(
                "%s vector of form %s: %d fields in the reply, expected %d"
                % (suite, form, len(got), 14 + len(triples) // 3)
            )
        got[13] &= mask
        want[13] &= mask
        memory = all(
            (have ^ value) & m == 0
            for have, value, m in zip(got[14:], triples[1::3], triples[2::3])
        )
        if got[:14] != want or not memory:
            raise Failure("%s vector of form %s fails" % (suite, form))
    return len(vectors)


def test_short_replies(sstrun, src, work):
    """The vector comparison is calibrated on a runner whose replies lack
    the memory fields: it must not report them as passing."""
    wrapper = os.path.join(work, "sstrun-short")
    with open(wrapper, "w") as handle:
        handle.write('#!/bin/sh\n%s "$@" | cut -d" " -f1-14\n' % sstrun)
    os.chmod(wrapper, 0o755)
    try:
        test_vectors(wrapper, src, work)
    except Failure:
        return
    raise Failure("vectors: replies without their memory fields pass")


def test_exit_store_failure(binary, work):
    """The page store fails while the last frame is drawn on the way out:
    status 3, and the terminal has its mode and margins back.

    The guest turns interrupts off and jumps into zero memory, which
    executes as ADD [BX+SI], AL through 64 pages without end and without
    one read from the page file. With eight frames the firmware's data
    page is in the page file except for an instant after each timer tick,
    when the display reads the cursor from it. The emulator is stopped,
    its page file cut to nothing through /proc, and it is continued with
    a termination signal pending, so the next read of that page is the
    one the exit makes."""
    if not os.path.isdir("/proc/self/fd"):
        print("NOT RUN page store failure at exit: no /proc")
        return
    code = bytes.fromhex("fa b80030 8ed8 31db 31f6 30c0 ea00000020")
    image = os.path.join(work, "zero.img")
    with open(image, "wb") as handle:
        handle.write(code + bytes(510 - len(code)) + b"\x55\xaa")
        handle.truncate(360 * 1024)
    store = os.path.join(work, "page")
    for _ in range(8):
        master, slave = pty.openpty()
        before = termios.tcgetattr(slave)
        child = subprocess.Popen(
            [binary, "-f", "8", "-s", store, "-a", image],
            stdin=slave,
            stdout=slave,
            stderr=subprocess.DEVNULL,
        )
        seen = b""
        try:
            time.sleep(0.4)
            os.kill(child.pid, signal.SIGSTOP)
            time.sleep(0.05)
            cut = False
            for name in os.listdir("/proc/%d/fd" % child.pid):
                path = "/proc/%d/fd/%s" % (child.pid, name)
                if os.readlink(path).startswith(store):
                    os.truncate(path, 0)
                    cut = True
            if not cut:
                raise Failure("page store failure at exit: no page file to cut")
            os.kill(child.pid, signal.SIGTERM)
            os.kill(child.pid, signal.SIGCONT)
            deadline = time.time() + 5
            while time.time() < deadline and child.poll() is None:
                ready, _, _ = select.select([master], [], [], 0.05)
                if ready:
                    seen += os.read(master, 4096)
            ready, _, _ = select.select([master], [], [], 0.1)
            if ready:
                seen += os.read(master, 4096)
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
                status = None
            else:
                status = child.returncode
            after = termios.tcgetattr(slave)
            os.close(master)
            os.close(slave)
        if status == 128 + signal.SIGTERM:
            continue  # the page was resident at that instant
        if status != 3:
            raise Failure("page store failure at exit: exit %r" % status)
        if before != after or not seen.endswith(b"\x1b[0m\x1b[r\x1b[?7h\x1b[26;1H"):
            raise Failure("page store failure at exit: terminal not restored")
        return
    raise Failure("page store failure at exit: the exit never read the page file")


def test_rep_burst(sstrun, work):
    """A repeated string instruction gives up the step after 256
    iterations with the instruction pointer on its prefix, so that
    interrupts and the host are served, and finishes in later steps."""
    regs = [0xAB, 0, 0x300, 0, 0x1000, 0x2000, 0, 0x3000, 0x200, 0, 0, 0x10, 0x100, 2]
    ram = [0x10100, 0xF3, 0x10101, 0xAA]
    first = " ".join(str(x) for x in regs + [len(ram) // 2] + ram + [3, 0x30010, 0x3010F, 0x30110])
    more = regs[:]
    more[2], more[11] = 0x200, 0x110
    second = " ".join(str(x) for x in more + [0, 3, 0x3010F, 0x3020F, 0x30210])
    third = more[:]
    third[2], third[11] = 0x100, 0x210
    last = " ".join(str(x) for x in third + [0, 2, 0x3030F, 0x30310])
    done = subprocess.run(
        sstrun.split() + ["0", "64", os.path.join(work, "sstpage")],
        input=first + "\n" + second + "\n" + last + "\n",
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    replies = [[int(x) for x in line.split()] for line in done.stdout.splitlines()]
    if done.returncode != 0 or len(replies) != 3:
        raise Failure("repeated instruction: sstrun exit %d" % done.returncode)
    # count, destination index, instruction pointer, then the bytes asked for
    want = (
        [0x200, 0x110, 0x100, 0xAB, 0xAB, 0],
        [0x100, 0x210, 0x100, 0xAB, 0xAB, 0],
        [0, 0x310, 0x102, 0xAB, 0],
    )
    for got, expect in zip(replies, want):
        if [got[2], got[11], got[12]] + got[14:] != expect:
            raise Failure("repeated instruction: %r, expected %r" % (got, expect))


def test_undefined(sstrun, work):
    """The undefined members of the FE and FF groups raise interrupt 6 on
    the 80186 and the 80286, with the stack frame of the instruction."""
    regs = [0, 0, 0, 0, 0x1000, 0x2000, 0, 0, 0x200, 0, 0, 0, 0x100, 2]
    lines = []
    forms = ((0xFE, 0xD0), (0xFE, 0x30), (0xFF, 0xF8), (0xFF, 0x38))
    for first, second in forms:
        ram = [0x10100, first, 0x10101, second, 0x18, 0x34, 0x19, 0x12, 0x1A, 0x78, 0x1B, 0x56]
        lines.append(" ".join(str(x) for x in regs + [len(ram) // 2] + ram + [0]))
    for model in ("1", "2"):
        done = subprocess.run(
            sstrun.split() + [model, "64", os.path.join(work, "sstpage")],
            input="\n".join(lines) + "\n",
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        replies = [[int(x) for x in line.split()] for line in done.stdout.splitlines()]
        if done.returncode != 0 or len(replies) != len(forms):
            raise Failure("undefined forms: sstrun exit %d" % done.returncode)
        for (first, second), got in zip(forms, replies):
            if (got[4], got[12], got[8]) != (0x5678, 0x1234, 0x1FA):
                raise Failure(
                    "model %s executes %02X %02X: cs:ip %04X:%04X sp %04X"
                    % (model, first, second, got[4], got[12], got[8])
                )
    # With the trace flag set, an instruction that faults enters its
    # handler alone: one frame, and no trace interrupt ahead of it.
    traced = [0, 0, 0, 0, 0x1000, 0x2000, 0, 0, 0x200, 0, 0, 0, 0x100, 0x102]
    ram = [0x10100, 0xFE, 0x10101, 0xD0, 0x18, 0x34, 0x19, 0x12, 0x1A, 0x78, 0x1B, 0x56]
    ram += [0x04, 0xF0, 0x05, 0xDE, 0x06, 0xBC, 0x07, 0x9A]
    done = subprocess.run(
        sstrun.split() + ["2", "64", os.path.join(work, "sstpage")],
        input=" ".join(str(x) for x in traced + [len(ram) // 2] + ram + [0]) + "\n",
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    got = [int(x) for x in done.stdout.split()]
    if done.returncode != 0 or (got[4], got[12], got[8]) != (0x5678, 0x1234, 0x1FA):
        raise Failure("a traced fault: cs:ip %04X:%04X sp %04X" % (got[4], got[12], got[8]))
    # The register forms of LEA, LES and LDS name no address. The 80286
    # raises interrupt 6; the 8086 and 80186 models do nothing, twice
    # over, so that a value left by one instruction cannot pass for the
    # effect of the next.
    regs = [0x1111, 0x2222, 0x3333, 0x4444, 0x1000, 0x2000, 0x5555, 0x6666]
    regs += [0x200, 0x7777, 0x8888, 0x9999, 0x100, 2]
    forms = ((0x8D, 0xC0), (0xC4, 0xC8), (0xC5, 0xD0), (0x8D, 0xD8))
    lines = []
    for first, second in forms:
        ram = [0x10100, first, 0x10101, second, 0x18, 0x34, 0x19, 0x12, 0x1A, 0x78, 0x1B, 0x56]
        lines.append(" ".join(str(x) for x in regs + [len(ram) // 2] + ram + [0]))
    for model in ("0", "1", "2"):
        done = subprocess.run(
            sstrun.split() + [model, "64", os.path.join(work, "sstpage")],
            input="\n".join(lines) + "\n",
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        replies = [[int(x) for x in line.split()] for line in done.stdout.splitlines()]
        if done.returncode != 0 or len(replies) != len(forms):
            raise Failure("register forms: sstrun exit %d" % done.returncode)
        for (first, second), got in zip(forms, replies):
            if model == "2":
                good = (got[4], got[12], got[8]) == (0x5678, 0x1234, 0x1FA)
            else:
                good = got[:14] == regs[:12] + [0x102, got[13]]
            if not good:
                raise Failure("model %s on %02X %02X: %r" % (model, first, second, got[:14]))


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
    test_exit_store_failure(binary, work)
    test_interactive(binary, work)
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
                        raise Failure("guest.hexdump differs from guest.asm")
                print("guest.hexdump matches guest.asm")
            else:
                print("NOT RUN guest.hexdump against guest.asm: nasm (package nasm) is absent")
            binary = os.path.join(work, "i86-host")
            build(src, binary, cc)
            test(binary, work)
            print("i86 host test passed")
            vsources = ["tests/sstrun.c", "cpu.c", "mem.c"]
            sstrun = os.path.join(work, "sstrun")
            build(src, sstrun, cc, vsources)
            print("%d 8088 vectors pass" % test_vectors(sstrun, src, work))
            print("%d 80286 vectors pass" % test_vectors(sstrun, src, work, suite="80286"))
            test_undefined(sstrun, work)
            test_rep_burst(sstrun, work)
            test_short_replies(sstrun, src, work)
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
                    test_undefined(sstrun + "-mutant", work)
                    test_rep_burst(sstrun + "-mutant", work)
                except (Failure, subprocess.TimeoutExpired, subprocess.CalledProcessError):
                    # A mutant that hangs or dies has failed like any other.
                    continue
                raise Failure("mutant %d survives: %s: %r" % (number, name, old))
            print("%d mutants fail the test" % len(MUTANTS))
        except (Failure, subprocess.TimeoutExpired, subprocess.CalledProcessError) as failure:
            sys.stderr.write("i86 check: %s\n" % failure)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
