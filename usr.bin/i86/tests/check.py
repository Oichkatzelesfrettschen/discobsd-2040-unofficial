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

It builds the emulator a second time around tests/panel.c, a model of the
PicoCalc's pixel device, and runs tests/panel.py on it: the panel display
of the text and graphics modes against frames computed there.

Then every mutant below must fail the first test and every mutant of
tests/panel.py the panel display test. When nasm is installed,
guest.hexdump and panel.hexdump must equal the assembly of guest.asm and
panel.asm; without nasm those comparisons are reported as not run.

The second form runs the processor vectors alone through COMMAND, a built
tests/sstrun with the words that start it, such as a qemu-arm prefix.
"""

import gzip
import importlib.util
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
    # <sys/lcdgfx.h> is the tree's, found after the host's own headers.
    "-idirafter",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "sys"),
]
SOURCES = ["main.c", "pc.c", "gfx.c", "serial.c", "ems.c", "cpu.c", "mem.c", "clock.c"]
HEADERS = ["clock.h", "ems.h", "font.h", "gfx.h", "i86.h", "mem.h", "pc.h", "serial.h"]
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
    "STUFF OK",
    "HOOK OK",
    "VID OK",
    "MODE OK",
    "a=1E61 B=3042 1=0231",
    "WAIT 0000",
    "IRQ 0882",
    "DONE",
]

MUTANTS = [
    # clock.c, each through tests/clocktest.c: a step back counted as a
    # second, a 5 s gap counted whole, a clock set back moving the tick
    # backwards, -S sleeping out a clock set back, -S keeping no credit
    # limit, and the 8254's fraction of a clock dropped.
    ("clock.c", "if ((long)d < 0)\n\t\td = 0;\n\telse if (d > CLK_GAP_US)", "if (d > CLK_GAP_US)"),
    ("clock.c", "\telse if (d > CLK_GAP_US)\n\t\td = CLK_GAP_US;\n", ""),
    ("clock.c", "return ((long)d > 0 ? d : 0);", "return (d);"),
    ("clock.c", "\telse if ((long)(*due - now) > CLK_BACK_US)\n\t\t*due = now;\n", ""),
    (
        "clock.c",
        "if ((long)(now - *due) > CLK_CREDIT_US)\n"
        "\t\t*due = now - (unsigned long)CLK_CREDIT_US;\n\telse if",
        "if",
    ),
    ("clock.c", "n = k * CLK_PIT_HZ + *rem;", "n = k * CLK_PIT_HZ;"),
    (
        "clock.c",
        "unsigned cap = period ? (CLK_CREDIT_CLOCKS - 1) / period + 1 : 0;",
        "unsigned cap = period ? CLK_CREDIT_CLOCKS / period : 0;",
    ),
    ("clock.c", "\tif (cap < CLK_TICKS_MIN)\n\t\tcap = CLK_TICKS_MIN;\n", ""),
    ("clock.c", "return (due < cap ? due + 1 : due);", "return (due + 1);"),
    ("clock.c", "(CLK_CREDIT_CLOCKS - 1) / period + 1", "CLK_CREDIT_CLOCKS / period + 1"),
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
    ("pc.c", "if (cf.model != I86_8086 && c->fault) {", "if (cf.model != I86_8086) {"),
    ("cpu.c", "i86_int(c, n);\n\tc->fault = 1;", "i86_int(c, n);"),
    ("pc.c", "!bq_put(c->r[R_CX]));", "0 * bq_put(c->r[R_CX]));"),
    (
        "main.c",
        "if (caught)\n\t\t\thost_exit(128 + (int)caught);\n\tif (read",
        "(void)0;\n\tif (read",
    ),
    ("pc.c", "} else\n\t\t\t\tknown = 0;", "}"),
    ("pc.c", "oputc(*s >= 0x20 && *s < 0x7f ? *s : '?');", "oputc(*s);"),
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
    ("pc.c", "pic_isr &= (uint8_t)(pic_isr - 1);", "pic_isr &= 0xFF;"),
    (
        "pc.c",
        "c->irq = (pic_irr & ~(unsigned)imask & above) != 0;",
        "c->irq = (pic_irr & above) != 0;",
    ),
    ("pc.c", "flags &= ~(unsigned)mod[i + 1];", "flags &= ~0u;"),
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
    ("pc.c", "\t\ttx = ty = tattr = -1;\n\t}\n\tif (ch == 'q'", "\t}\n\tif (ch == 'q'"),
    ("pc.c", 'if (n) {\n\t\t\t\tn--;\n\t\t\t\toputs("\\b \\b");\n\t\t\t}', ";"),
    ("pc.c", "(name[n - 3] | 0x20) == 'i'", "(name[n - 3] | 0x20) == 'j'"),
    ("pc.c", 'memset(shadow, 0, VRAM_SIZE);\n\t\toputs("\\033[2J");', 'oputs("\\033[2J");'),
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
    (
        "mem.c",
        "\tif (wr) {\n\t\tftab[i].flags |= FR_DIRTY;\n\t\tmem_wmap[pg]",
        "\tif (wr) {\n\t\t;\n\t\tmem_wmap[pg]",
    ),
    ("mem.c", "if (w[i])\n\t\t\treturn (0);", "if (w[i] == 1)\n\t\t\treturn (0);"),
    (
        "mem.c",
        "\t\tmem_rmap[lp] = mem_wmap[lp] = 0;\n\t\treturn (ref);",
        "\t\tmem_rmap[lp] = 0;\n\t\treturn (ref);",
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
    (
        "main.c",
        "(!cpu.halted || (cpu.irq && (cpu.fl & F_IF))); pc_step++)",
        "!cpu.halted; pc_step++)",
    ),
    ("main.c", "mem_failed = store_failed;", "(void)store_failed;"),
    ("main.c", "nsteps++;\n\t\tif (caught)\n\t\t\thost_exit(128 + (int)caught);", "nsteps++;"),
    ("main.c", '"/tmp/i86.%u", (unsigned)getpid() & 0xffffffu', '"/tmp/i86.%u", 0u'),
    ("mem.c", "O_RDWR | O_CREAT | O_EXCL", "O_RDWR | O_CREAT | O_TRUNC"),
    ("pc.c", "mbr[0x1be + 5] != 0xff) {", "1) {"),
    ("pc.c", "d->spt = mbr[0x1be + 6] & 63;", "d->spt = mbr[0x1be + 6] & 31;"),
    (
        "pc.c",
        "else if (dl >= 0x80 && dl < 0x82)\n\t\td = &disks[dl - 0x80 + 2];",
        "else if (dl >= 0x80 && dl < 0x82)\n\t\td = &disks[dl - 0x80];",
    ),
    ("serial.c", "return (ready && (ier & 1) && (mcr & 8));", "return (ready && (ier & 1));"),
    ("serial.c", "v = ready && (ier & 1) ? 0x04 : 0x01;", "v = 0x01;"),
    (
        "serial.c",
        "if (POWER(mcr))\n\t\t\t\tsend(&ident, 1);",
        "if (POWER(mcr))\n\t\t\t\t(void)ident;",
    ),
    ("serial.c", "\tready = 1;\n\tdue = now + BYTE_MS;", "\tready = 1;\n\tdue = now;"),
    ("serial.c", "v = 0x60 | (unsigned)ready;", "v = 0x60;"),
    ("serial.c", "(y >> 6) << 2 | x >> 6);", "x >> 6);"),
    ("serial.c", "((buttons & MOUSE_LEFT) ? 0x20 : 0)", "((buttons & MOUSE_LEFT) ? 0x10 : 0)"),
    ("serial.c", "(mcr & 2u) << 3", "(mcr & 2u) << 4"),
    ("serial.c", "\t\telse if (mcr & 0x10) {", "\t\telse if (mcr & 0x20) {"),
    ("pc.c", "if (level && !ser_level)", "if (level)"),
    ("pc.c", "mk_step = mk_step < MK_MAX ? 2 * mk_step : MK_MAX;", "mk_step = MK_STEP;"),
    ("pc.c", "if (dir == mk_dir && nticks - mk_tick <= MK_TICKS)", "if (dir == mk_dir)"),
    ("pc.c", "if (mouse_held & b)\n\t\tmouse_held &= ~b;", "if (mouse_held & b)\n\t\t(void)b;"),
    (
        "pc.c",
        "\telse if (ch == '{' || ch == '}')\n\t\tmouse_held |= b;",
        "\telse if (ch == '{' || ch == '}')\n\t\t(void)b;",
    ),
    ("pc.c", "\t\t\tmouse_keys = !mouse_keys;", "\t\t\tmouse_keys = 1;"),
    ("pc.c", "mem_ww(BDA + 0x00, SER_BASE);", "mem_ww(BDA + 0x00, 0);"),
    ("pc.c", "(cf.cols == 40 ? 0x10 : 0x20) | 0x200 |", "(cf.cols == 40 ? 0x10 : 0x20) |"),
    (
        "pc.c",
        "\t\tpc_out(c, (port + 1) & 0xffff, v >> 8 & 0xff, 0);",
        "\t\t(void)c;",
    ),
    ("serial.c", "\tif (QN - qn < 3)\n\t\tbehind = 1;", "\tif (QN - qn < 3)\n\t\tbehind = 0;"),
    ("serial.c", "#define POWER(m)\t(((m) & 0x12) == 2)", "#define POWER(m)\t(((m) & 0x02) == 2)"),
    (
        "ems.c",
        "\t\tcase 0:\n\t\t\tif (alt_seg | alt_off)\n\t\t\t\tput_map(alt_area);\n",
        "\t\tcase 0:\n",
    ),
    (
        "ems.c",
        "\t\t\tif (alt_seg | alt_off)\n\t\t\t\tput_map(alt_area);\n\t\t\talt_seg = c->s[S_ES];",
        "\t\t\talt_seg = c->s[S_ES];",
    ),
    ("ems.c", "st = bx == 0 ? OK : E_FREE;", "st = E_HANDLE;"),
    ("ems.c", "else if (hd[dx].saved)\n\t\t\tst = 0x86;", "else if (0)\n\t\t\tst = 0x86;"),
    ("ems.c", "else if (hd[dx].saved)\n\t\t\tst = E_SAVED;", "else if (0)\n\t\t\tst = E_SAVED;"),
    ("ems.c", "if (n == 0 && !zero_ok)", "if (n == 0 && zero_ok < 0)"),
    ("ems.c", "n += owner[k] == 0;", "n += 1;"),
    ("ems.c", "(unsigned)blk[p] << 10 != seg", "(unsigned)blk[p] << 9 != seg"),
    ("ems.c", "off % PAGE / MEM_PGSIZE", "off / MEM_PGSIZE"),
    (
        "ems.c",
        "if (n != 0xffff && (base = page_base(h, n)) == NOBASE)",
        "if ((base = page_base(h, n)) == NOBASE)",
    ),
    ("pc.c", "if (ems_on)\n\t\t\tems_int(c);", "if (0)\n\t\t\tems_int(c);"),
    ("mem.c", "lp - pbase[p] < MEM_EMSPG) {", "lp - pbase[p] < MEM_EMSPG && p != 59) {"),
    ("mem.c", "\tb += pg % MEM_EMSPG;", "\tb += 0;"),
    ("mem.c", "mem_rmap[g] = mem_wmap[g] = 0;\n\tpbase[phys]", "(void)g;\n\tpbase[phys]"),
    ("ems.c", "\tmem_ww(a & (MEM_SIZE - 1), v);", "\tmem_ww(a, v);"),
    (
        "ems.c",
        "\t\tif (m + lo < xs + len && xs < m + hi)",
        "\t\tif (m + lo < xs + len && xs < m + hi && 0)",
    ),
    ("ems.c", "st = bx & 0xff ? 0x9c : 0x9e;", "st = bx & 0xff ? 0x9c : OK;"),
    ("ems.c", "else if (key_out ? ((uint32_t)bx << 16 | cx) != key :", "else if (key_out ? 0 :"),
    ("ems.c", "if (os_off && ((ah == 0x59 && al == 0) || ah == 0x5b))", "if (0)"),
    ("ems.c", "if (al < 2 && grw(dssi) > nphys) {", "if (0) {"),
    ("ems.c", "else if (cx > nphys)\n\t\t\tst = E_PPAGE;", "else if (0)\n\t\t\tst = E_PPAGE;"),
    (
        "ems.c",
        "\t\t\tn = grw(dssi);\n\t\t\ttake(dssi + 2, 2 * n);\n",
        "\t\t\tn = grw(dssi);\n\t\t\ttake(dssi + 2, 2 * n);\n\t\t\tif (n > 1 && base_ok(arg[1]))\n"
        "\t\t\t\tshow(phys_at(arg[0]) % nphys, arg[1]);\n",
    ),
    ("ems.c", "!= key : al == 2)", "!= key : 0)"),
    (
        "ems.c",
        "\t\t\tn = grw(dssi);\n"
        "\t\t\ttake(dssi + 2, n);\n"
        "\t\t\tfor (k = 0; k < n; k++)\n"
        "\t\t\t\tif (phys_at(arg[k]) == nphys) {\n"
        "\t\t\t\t\tst = E_PPAGE;\n"
        "\t\t\t\t\tbreak;\n"
        "\t\t\t\t}\n"
        "\t\t\tgww(esdi, n);\n"
        "\t\t\tfor (k = 0; k < n && st == OK; k++) {\n"
        "\t\t\t\tgww(esdi + 2 + 4 * k, arg[k]);\n"
        "\t\t\t\tgww(esdi + 4 + 4 * k, cur[phys_at(arg[k])]);\n"
        "\t\t\t}",
        "\t\t\tn = grw(dssi);\n"
        "\t\t\tgww(esdi, n);\n"
        "\t\t\tfor (k = 0; k < n; k++) {\n"
        "\t\t\t\tunsigned seg = grw(dssi + 2 + 2 * k);\n"
        "\n"
        "\t\t\t\tif ((p = phys_at(seg)) == nphys) {\n"
        "\t\t\t\t\tst = E_PPAGE;\n"
        "\t\t\t\t\tbreak;\n"
        "\t\t\t\t}\n"
        "\t\t\t\tgww(esdi + 2 + 4 * k, seg);\n"
        "\t\t\t\tgww(esdi + 4 + 4 * k, cur[p]);\n"
        "\t\t\t}",
    ),
    (
        "ems.c",
        "\t\t\t\tshow(phys_at(arg[2 * k]), arg[2 * k + 1]);",
        "\t\t\t\ttake(dssi + 2, 2 * n), show(phys_at(arg[2 * k]), arg[2 * k + 1]);",
    ),
    ("ems.c", "\t\t\tn = arg[2 * k];", "\t\t\ttake(dssi, 2 * cx);\n\t\t\tn = arg[2 * k];"),
    (
        "ems.c",
        "\t\t\tif ((c->s[S_ES] | c->r[R_DI]) &&\n\t\t\t    (st = load_map(esdi)) != OK)\n"
        "\t\t\t\tbreak;\n\t\t\tif (alt_seg | alt_off)\n\t\t\t\tput_map(alt_area);",
        "\t\t\tif (alt_seg | alt_off)\n\t\t\t\tput_map(alt_area);\n"
        "\t\t\tif ((c->s[S_ES] | c->r[R_DI]) &&\n\t\t\t    (st = load_map(esdi)) != OK)\n"
        "\t\t\t\tbreak;",
    ),
    (
        "ems.c",
        "\t\tif ((al == 1 || al == 2) && st == OK)\n\t\t\tapply_map();",
        "\t\tif ((al == 1 || al == 2) && st == OK)\n"
        "\t\t\tfor (k = 0; k < nphys; k++) {\n"
        "\t\t\t\tshow(k, arg[1 + k]);\n\t\t\t\ttake(dssi, 1 + nphys);\n\t\t\t}",
    ),
    (
        "ems.c",
        "\t\tif (al == 1 || al == 2)\n\t\t\tst = load_map(dssi);\n"
        "\t\tif (al == 0 || al == 2)\n\t\t\tput_map(esdi);",
        "\t\tif (al == 0 || al == 2)\n\t\t\tput_map(esdi);\n"
        "\t\tif (al == 1 || al == 2)\n\t\t\tst = load_map(dssi);",
    ),
    ("pc.c", "\t\t\tirq_at += pit[0].count;", "\t\t\tirq_at += 0x10000;"),
    ("pc.c", "pit_out(2, pit_view(c)) << 5;", "0 << 5;"),
    ("pc.c", "\t\t\t\tpit[k].slatched = 1;", "\t\t\t\tpit[k].slatched = 0;"),
    ("pc.c", "uint32_t t = pit_t + (pc_step * pit_step >> 8);", "uint32_t t = pit_t;"),
    ("pc.c", "(pit_now() / 18 & 1) << 4", "0 << 4"),
    ("pc.c", "if ((n != 2 || (port61 & 1)) && e / p->count != p->shown) {", "if (0) {"),
    (
        "pc.c",
        "hi = (c + 1) / 2;\n\t\tv = c - (c & 1) - 2 * (e < hi ? e : e - hi);",
        "hi = c / 2 ? c / 2 : 1;\n\t\tv = c - 2 * (e % hi);",
    ),
    ("pc.c", "bin_bcd(v % 10000) :", "(bin_bcd(0) | v % 10000) :"),
    ("pc.c", "if (p->mode == 0) {\n\t\t\tp->armed = 0;", "if (0) {\n\t\t\tp->armed = 0;"),
    ("pc.c", "pit[n].latched = 0;\n\tpit[n].slatched = 0;", "pit[n].latched = 0;"),
    ("pc.c", "else if ((pc_idle || c->halted) && slice_left() < PC_STEPS) {", "else if (0) {"),
    (
        "pc.c",
        "if ((ms = clk_pace(&thr_due, now)) != 0)\n\t\thost_sleep(ms);",
        "ms = clk_pace(&thr_due, now);\n\t(void)ms;",
    ),
    ("pc.c", "return (d < SLICE_MIN ? SLICE_MIN : d);", "return (PC_STEPS);"),
    (
        "pc.c",
        "\t\tp->low = (uint8_t)v;\n\t\tp->wnext = 1;",
        "\t\tp->low = (uint8_t)v;\n\t\tp->wnext = 0;",
    ),
    ("ems.c", "(uint8_t)mem_rb((e->at + i) & (MEM_SIZE - 1)) :", "0xff :"),
    (
        "ems.c",
        "\treturn (shared ? fwd | rev << 1 : 0);",
        "\treturn ((shared ? fwd | rev << 1 : 0) & 0);",
    ),
    ("ems.c", "if (bq + a->at < bp + b->at)", "if (0)"),
    (
        "ems.c",
        "if (dirs == 3 || (self1 && back)) {\n\t\tstaged(e, len, one);",
        "if (0) {\n\t\tstaged(e, len, one);",
    ),
    (
        "ems.c",
        "dirs |= alias0(&e[0], &e[1], len);",
        "dirs = alias0(&e[0], &e[1], len) ? alias0(&e[0], &e[1], len) : dirs;",
    ),
    (
        "ems.c",
        "if (e[k].at >= MEM_SIZE || e[k].at + len > MEM_SIZE)",
        "if (e[k].at + len > MEM_SIZE)",
    ),
    ("ems.c", "c->r[R_DI] = alt_off;", "c->r[R_DI] = 0;"),
    ("ems.c", "if (xchg && (self0 || self1))", "if (xchg && 0 && (self0 || self1))"),
    ("ems.c", "if (dirs == 3 || (self1 && back)) {", "if (dirs == 3 || (self1 && 0)) {"),
    ("ems.c", "if (e[k].page >= hd[e[k].h].pages)", "if (0)"),
    (
        "ems.c",
        "e[k].at = ((uint32_t)grw(d + 5) << 4) + grw(d + 3);",
        "e[k].at = lin(grw(d + 5), grw(d + 3));",
    ),
    ("ems.c", "if (hd[h].used && memcmp(hd[h].name,", "if (hd[h].used || memcmp(hd[h].name,"),
    (
        "ems.c",
        "c->s[S_ES] = alt_seg;\n\t\t\tc->r[R_DI] = alt_off;",
        "c->s[S_ES] = (uint16_t)(alt_area >> 4);\n\t\t\tc->r[R_DI] = (uint16_t)(alt_area & 15);",
    ),
    (
        "pc.c",
        "(cf.keys != NULL || key_eof))\n\t\t\thost_exit(0);",
        "(cf.keys != NULL || key_eof))\n\t\t\thost_exit(ems_on ? 3 : 0);",
    ),
]


class Failure(Exception):
    pass


def build(src, out, cc, sources=None):
    cmd = [cc] + FLAGS + ["-o", out] + (sources or SOURCES)
    done = subprocess.run(cmd, cwd=src, capture_output=True, text=True, check=False)
    if done.returncode != 0:
        raise Failure("build failed:\n" + done.stderr)


def load_panel(src):
    """tests/panel.py; the interpreter runs isolated, without the script's
    directory on its path."""
    spec = importlib.util.spec_from_file_location("panel", os.path.join(src, "tests", "panel.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def mutate(src, tree, number, name, old, new):
    """A copy of the sources in tree with one fault in file name."""
    for item in (
        SOURCES
        + HEADERS
        + [
            "tests/sstrun.c",
            "tests/panel.c",
            "tests/clocktest.c",
            "tests/draintest.c",
        ]
    ):
        shutil.copyfile(os.path.join(src, item), os.path.join(tree, item))
    with open(os.path.join(tree, name)) as handle:
        text = handle.read()
    if text.count(old) != 1:
        raise Failure("mutant %d: anchor occurs %d times in %s" % (number, text.count(old), name))
    with open(os.path.join(tree, name), "w") as handle:
        handle.write(text.replace(old, new))


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
    # A program's own INT 6 on the 80286 returns from the firmware; only a
    # failing instruction ends the session there.
    soft = os.path.join(work, "int6.img")
    code = bytes.fromhex("cd06 b84b0e cd10 fa f4")
    with open(soft, "wb") as handle:
        handle.write(code + bytes(510 - len(code)) + b"\x55\xaa")
        handle.truncate(360 * 1024)
    done = subprocess.run(
        [binary, "-d", "-T", "-c", "286", "-s", os.path.join(work, "page"), "-a", soft, "-k", ""],
        capture_output=True,
        timeout=10,
        check=False,
    )
    if done.returncode != 0 or not done.stdout.startswith(b"K"):
        raise Failure(
            "software interrupt 6: exit %d, screen %r" % (done.returncode, done.stdout[:8])
        )
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
    if termios_modes(before) != termios_modes(after):
        raise Failure(
            "terminal session: terminal mode not restored: "
            + termios_diff(termios_modes(before), termios_modes(after))
        )


def termios_modes(attrs):
    """The attributes with PENDIN cleared.

    PENDIN (retype pending input) is a status flag of the line discipline, not a
    mode: macOS sets it in c_lflag when a mode change finds input queued, and
    nothing the program does clears it. Every other bit, speed and control
    character is still compared.
    """
    attrs = list(attrs)
    attrs[3] &= ~getattr(termios, "PENDIN", 0)
    return attrs


def termios_diff(before, after):
    """Name the termios fields that differ, with both values, for a failure message."""
    names = ("iflag", "oflag", "cflag", "lflag", "ispeed", "ospeed")
    parts = [
        "%s %#x -> %#x" % (name, b, a) for name, b, a in zip(names, before[:6], after[:6]) if b != a
    ]
    parts += [
        "cc[%d] %r -> %r" % (i, b, a) for i, (b, a) in enumerate(zip(before[6], after[6])) if b != a
    ]
    return "; ".join(parts) or "no field differs"


def load_ptywait():
    """tests/ptywait.py, beside this file; the interpreter runs isolated,
    without the script's directory on its path."""
    spec = importlib.util.spec_from_file_location(
        "ptywait", os.path.join(os.path.dirname(os.path.abspath(__file__)), "ptywait.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ptywait = load_ptywait()


def test_wait_draining():
    """The wait of menu_session against a writer of 1 MiB, more than any
    pseudo-terminal holds: a plain wait blocks on it, the draining wait
    returns every byte with the writer's status, a child that does not exit
    is reported as running, and so is one that never stops writing, within
    the time given."""
    size = 1 << 20
    writer = "import sys; sys.stdout.buffer.write(b'x' * %d); sys.stdout.buffer.flush()" % size
    sleeper = "import time; time.sleep(60)"
    chatter = "import sys\nwhile 1: sys.stdout.buffer.write(b'x' * 4096)"

    def run(code, drain, seconds):
        master, slave = pty.openpty()
        child = subprocess.Popen(
            [sys.executable, "-I", "-c", code], stdin=slave, stdout=slave, stderr=subprocess.DEVNULL
        )
        try:
            if drain:
                got, status = ptywait.wait_draining(child, master, seconds)
                return len(got), status
            try:
                return child.wait(timeout=seconds)
            except subprocess.TimeoutExpired:
                return None
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
            os.close(master)
            os.close(slave)

    def late(signum, frame):
        raise Failure("wait_draining: a child that never stops writing held the wait past its time")

    if run(writer, False, 0.5) is not None:
        raise Failure(
            "wait_draining: a plain wait returned on a 1 MiB writer, so the case blocks nothing"
        )
    got = run(writer, True, 20)
    if got != (size, 0):
        raise Failure("wait_draining: the writer gave %r, want (%d, 0)" % (got, size))
    got = run(sleeper, True, 0.5)
    if got != (0, None):
        raise Failure("wait_draining: the sleeper gave %r, want (0, None)" % (got,))
    before = signal.signal(signal.SIGALRM, late)
    signal.alarm(10)
    try:
        got = run(chatter, True, 0.5)
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, before)
    if got[0] == 0 or got[1] is not None:
        raise Failure("wait_draining: the chatterer gave %r, want output and None" % (got,))


def menu_session(binary, work, image, steps, cwd=None, kill=None):
    """A session on a pseudo-terminal: each step waits for a text and
    types. With kill, that signal follows the last step. Returns the raw
    output, the exit status and whether the terminal mode came back."""
    master, slave = pty.openpty()
    before = termios.tcgetattr(slave)
    child = subprocess.Popen(
        [binary, "-T", "-s", os.path.join(work, "page"), "-a", image],
        stdin=slave,
        stdout=slave,
        stderr=subprocess.DEVNULL,
        cwd=cwd,
    )
    seen = b""
    at = step = 0
    deadline = time.time() + 30
    status = None
    try:
        while time.time() < deadline and child.poll() is None and step < len(steps):
            ready, _, _ = select.select([master], [], [], 0.05)
            if ready:
                seen += os.read(master, 4096)
            plain = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", seen)
            found = plain.find(steps[step][0], at)
            if found < 0:
                continue
            at = found + len(steps[step][0])
            time.sleep(0.3)
            os.write(master, steps[step][1])
            step += 1
        if step == len(steps):
            if kill is not None:
                time.sleep(0.5)
                child.send_signal(kill)
            more, status = ptywait.wait_draining(child, master, 5)
            seen += more
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        ready, _, _ = select.select([master], [], [], 0.2)
        if ready:
            seen += os.read(master, 65536)
        after = termios.tcgetattr(slave)
        os.close(master)
        os.close(slave)
    if step != len(steps):
        raise Failure("menu session: %r never appeared" % steps[step][0])
    if status is None:
        print(
            "i86 check: menu session: the program did not exit after its last key; "
            "the output ended with %r" % (seen[-400:],),
            file=sys.stderr,
        )
    if termios_modes(before) != termios_modes(after):
        print(
            "i86 check: menu session: terminal mode not restored: "
            + termios_diff(termios_modes(before), termios_modes(after)),
            file=sys.stderr,
        )
    return seen, status, termios_modes(before) == termios_modes(after)


RIGHT, DOWN, LEFT, UP, HOME = b"\x1b[C", b"\x1b[B", b"\x1b[D", b"\x1b[A", b"\x1b[H"


def boot_image(src, name, path):
    with open(os.path.join(src, "tests", name + ".hexdump")) as handle:
        data = bytes.fromhex("".join(handle.read().split()))
    if len(data) % 512 or data[510:512] != b"\x55\xaa":
        raise Failure("%s.hexdump is not a boot program" % name)
    with open(path, "wb") as handle:
        handle.write(data)
        handle.truncate(360 * 1024)
    return data


def mouse_image(src, path):
    with open(os.path.join(src, "tests", "mouse.hexdump")) as handle:
        data = bytes.fromhex("".join(handle.read().split()))
    if len(data) % 512 or data[510:512] != b"\x55\xaa":
        raise Failure("mouse.hexdump is not a boot program")
    with open(path, "wb") as handle:
        handle.write(data)
        handle.truncate(360 * 1024)
    return data


def test_mouse(binary, work):
    """COM1 and the mouse keys, through the guest of tests/mouse.asm on a
    pseudo-terminal. Each key waits for the guest's line about the last
    one, so a repeat is the four arrows typed in one write."""
    image = os.path.join(work, "mouse-run.img")
    shutil.copyfile(os.path.join(work, "mouse.img"), image)
    steps = [
        (b"ready", RIGHT),
        (b"k:4D00", b"\x1c"),
        (b"", RIGHT),
        (b"p:..:08:00", RIGHT),
        (b"p:..:08:00", DOWN),
        (b"p:..:00:08", LEFT),
        (b"p:..:F8:00", UP),
        (b"p:..:00:F8", HOME),
        (b"k:4700", b"["),
        (b"p:..:00:00", b"]"),
        (b"p:..:00:00", b"{"),
        (b"p:L.:00:00", RIGHT),
        (b"p:L.:08:00", b"{"),
        (b"p:..:00:00", RIGHT * 4),
        (b"p:..:40:00", RIGHT * 15 + b"["),
        (b"p:L.:00:00", b"\x1c"),
        (b"", b"["),
        (b"k:1A5B", b"\x1d"),
        (b"quit", b"q"),
    ]
    seen, status, restored = menu_session(binary, work, image, steps)
    # The guest's output up to the menu; the screen drawn again after it
    # repeats lines and could stand in for missing ones.
    plain = re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", seen).split(b"i86: q quit")[0]
    want = [
        # COM1 in the data area, the UART's registers, the identification
        # a byte time after the power and held back until OUT2.
        b"com:03F8",
        b"ports:0200",
        b"scr:5A",
        b"msr:90",
        b"lsr:61",
        b"echo:A5",
        b"quiet:60",
        b"lsr:60",
        b"lsr:61",
        b"irqs:00",
        b"word:600B",
        b"irqs:01",
        b"iir:04",
        b"id:4D",
        b"ready",
        # Mouse keys off, then on: arrows, Home, the buttons, a drag.
        b"k:4D00:01",
        b"p:..:08:00",
        b"p:..:08:00",
        b"p:..:00:08",
        b"p:..:F8:00",
        b"p:..:00:F8",
        b"k:4700",
        b"p:L.:00:00",
        b"p:..:00:00",
        b"p:.R:00:00",
        b"p:..:00:00",
        b"p:L.:00:00",
        b"p:L.:08:00",
        b"p:..:00:00",
        # A held arrow doubles its step up to 64 mickeys.
        b"p:..:08:00",
        b"p:..:10:00",
        b"p:..:20:00",
        b"p:..:40:00",
        # Fifteen arrows fill the sixteen reports the mouse holds; the
        # click's release does not fit and is sent once one fits.
        b"p:L.:00:00",
        b"p:..:00:00",
        b"k:1A5B:",
    ]
    at = 0
    for token in want:
        found = plain.find(token, at)
        if found < 0:
            raise Failure("mouse: %r missing after %r" % (token, plain[max(0, at - 40) : at]))
        at = found + len(token)
    # The bytes of the burst arrive a byte time apart: at most six in one
    # timer tick of 55 ms.
    rate = int(plain[at : at + 2], 16)
    if not 3 <= rate <= 6:
        raise Failure("mouse: %d bytes in one tick" % rate)
    if status != 0 or not restored:
        raise Failure("mouse: exit %r, terminal restored %r" % (status, restored))


def ems_expected():
    """The lines of tests/ems.asm with 256 KB of expanded memory. The page
    sums follow from the fill rule alone: byte i of page n is
    (11h + 2n + i) mod 256, summed as words with a rotate before each add."""

    def page_sum(n):
        data = bytes((0x11 + 2 * n + i) & 255 for i in range(16384))
        s = 0
        for k in range(0, 16384, 2):
            s = ((s << 1) | (s >> 15)) & 0xFFFF
            s = (s + data[k] + 256 * data[k + 1]) & 0xFFFF
        return s

    return (
        "st:00 frame:E000 free:0010 total:0010 ver:40 phys:0012 alloc:00 handle:0001"
        " free:000D e87:87 e88:88 e89:89".split()
        + ["sumx:%04X" % page_sum(2)]
        + ["sum:%04X" % page_sum(n) for n in range(3)]
        + "alias:AB alias2:AB disk:AA55 save:00 e8d:8D restore:11 multi:00 c000:11"
        " e50l:8B e000l:31 move:00 xchg:00"
        " xgot:45 moved:4D45 landed:4D45 set1:00 alt:0010 get:FFF0 h0:00 e88h0:88 e83:83"
        " e8a:8A e8b:8B e84:84 wrap:0001 e94:94 e94no:00 e9e:9E os:00 osbad:A4 osoff:00"
        " e5b:A4 e59:A4 osret:00 osnone:A4 oson:00 e4f0:A3 e4f1:A3 e4fb:A3 kept:13 unmap:FF"
        " e86:86 toc:00 tocp:04 ovl:00 ovlp:FF p4o:00 p4t:00 p4tp:04 m50:00 m50p:04 alo:00"
        " alop:04 altes:0000 altdi:0000 rom:00 romb:2F ea2:A2 e8am:8A amovs:00 amov:00"
        " axch:97 acyc:00 acycd:1122 lalias:CCAA sxch:97 sdst:11 ea2z:A2 nul1:A1 nul0:A0"
        " dealloc:00 free:0010 done".split()
    )


def pit_run(binary, work, args):
    """tests/pit.asm under args; its tokens from the screen dump."""
    try:
        done = subprocess.run(
            [binary, "-d", "-a", os.path.join(work, "pit.img"), "-k", ""] + args,
            stdin=subprocess.DEVNULL,
            capture_output=True,
            timeout=20,
        )
    except subprocess.TimeoutExpired:
        raise Failure("timer %s: timeout, counter 2's output never rose" % " ".join(args)) from None
    if done.returncode != 0:
        raise Failure("timer %s: exit %d" % (" ".join(args), done.returncode))
    return done.stdout.decode("latin-1").split()


def test_clock(clocktest):
    """clock.c's steps from one host clock reading to the next, through
    tests/clocktest.c: a clock set back, set forward and wrapping."""
    done = subprocess.run([clocktest], capture_output=True, text=True, timeout=10, check=False)
    if done.returncode != 0 or not done.stdout.startswith("clock: "):
        raise Failure("clock steps:\n" + done.stdout + done.stderr)
    return done.stdout.strip()


def test_drain(draintest):
    """gfx.c's owed graphics lines through tests/draintest.c: refreshes mark,
    drains send at most 16 lines, and no line starves."""
    done = subprocess.run([draintest], capture_output=True, text=True, timeout=10, check=False)
    if done.returncode != 0 or not done.stdout.startswith("drain: "):
        raise Failure("owed lines:\n" + done.stdout + done.stderr)
    return done.stdout.strip()


def test_pit(binary, work):
    """The 8254 through tests/pit.asm. Under -T every count is exact: the
    latched counter moved 2B0h in mode 3, counter 0 at 1 kHz raised 54
    interrupts while counter 2 counted 65535 clocks, counter 2 latched just
    after the guest saw it expire reads FFF0h (16 clocks past its end),
    counter 2's read-back status is B0h, 20 halts at 1 kHz took 57xxh
    clocks, the refresh bit changed 39h times in 64 reads, a poller saw mode 2's low pulse, all 40
    reads of an odd mode 3 count were even, the first byte of a new mode 0
    count drove OUT low, a new control word cleared a latched status, and a
    BCD count reads as decimal digits. On the host's clock the interrupts
    are 52 to 55 whatever the host's speed, since both counters count the
    same clocks."""
    want = (
        "dn:02B0 irq:0036 c2:FFF0 st2:00B0 hlt:0057 rf:0039 m2:0000 m3e:0028 m0s:0000 stat:0012"
        " bcd:0099 tick:ok done".split()
    )
    got = pit_run(binary, work, ["-T"])
    if got != want:
        raise Failure("timer -T: %r" % got)
    host_clock_irqs(lambda: pit_run(binary, work, []), "timer, host clock")


# Counter 0 at 1 kHz raises 52 to 55 interrupts while counter 2 counts 65535
# clocks (tests/pit.asm), 34h to 37h. Under -S the emulator sleeps whole
# milliseconds between polls. A sleep that overruns leaves the interrupts it
# covers waiting, late and not lost, when counter 2 closes the window, and
# clk_owe() keeps at most 21 of them waiting at 1 kHz, so a stalled host
# counts at least 52 - 21 = 31 interrupts, 1Fh. The bound holds while each
# stall stays within CLK_CREDIT_US (20 ms); a longer one drops the interrupts
# past the 21 and no floor holds. The unpaced run has no sleep and keeps the
# unstalled floor.
HOST_CLOCK_IRQ_UNSTALLED = 0x34
HOST_CLOCK_IRQ_MAX = 0x37
HOST_CLOCK_IRQ_STALLED = 0x1F


def host_clock_irqs(run, what, floor=HOST_CLOCK_IRQ_UNSTALLED, note=print):
    """A run of tests/pit.asm on the host's clock must finish whole and
    count at most 37h interrupts while counter 2 counts 65535 clocks. A
    host never counts more than the clocks allow, so a run of 34h to 37h
    passes alone, without a repeat, and a loaded host counts fewer: the best
    of three runs must reach floor. A paced run passes HOST_CLOCK_IRQ_STALLED
    as its floor. The -T run holds the exact count of 36h, which a lost
    final interrupt moves; this judge cannot see one inside its band."""
    seen = []
    for _ in range(3):
        got = run()
        fields = dict(t.split(":", 1) for t in got if ":" in t)
        irq = int(fields.get("irq", "0"), 16)
        if (
            irq > HOST_CLOCK_IRQ_MAX
            or fields.get("st2") != "00B0"
            or fields.get("tick") != "ok"
            or got[-1:] != ["done"]
        ):
            raise Failure("%s: %r" % (what, got))
        if irq >= HOST_CLOCK_IRQ_UNSTALLED:
            return
        seen.append(irq)
    counts = ["%02Xh" % n for n in seen]
    if max(seen) == 0:
        raise Failure("%s: the timer raised no interrupt in three runs" % what)
    if max(seen) < floor:
        raise Failure(
            "%s: fewer than %02Xh interrupts in three runs: %s" % (what, floor, ", ".join(counts))
        )
    note("%s: stalled host, %s interrupts, floor %02Xh" % (what, ", ".join(counts), floor))


def test_host_clock_judge():
    """host_clock_irqs() on counts it must pass and reject, each reject for
    its own reason: an unstalled host (34h), a stalled paced host (the floor
    1Fh, and 31h as hosted macOS counted), a stalled unpaced host (all runs
    below 34h), a paced count under the floor in all three runs (1Eh), a
    timer that raised nothing (0), more interrupts than the clocks allow
    (38h), a run that does not finish and a run whose counter 2 status is
    wrong."""

    def runs(counts, tail="done", st2="00B0"):
        queue = list(counts)

        def one():
            return ["irq:%04X" % queue.pop(0), "st2:" + st2, "tick:ok", tail]

        return one

    paced = HOST_CLOCK_IRQ_STALLED
    accepted = (
        ([0x36], paced),
        ([0x20, 0x34], paced),
        ([0x31, 0x31, 0x33], paced),
        ([0x1F, 0x1F, 0x1F], paced),
        ([0x20, 0x34], HOST_CLOCK_IRQ_UNSTALLED),
    )
    rejected = (
        ([0x1E, 0x1E, 0x1E], paced, "fewer than 1Fh"),
        ([0, 0, 0], paced, "no interrupt"),
        ([0x31, 0x31, 0x33], HOST_CLOCK_IRQ_UNSTALLED, "fewer than 34h"),
        ([0x38], paced, "irq:0038"),
        ([0x36], paced, "done"),
        ([0x36], paced, "st2:0000"),
    )
    for counts, floor in accepted:
        try:
            host_clock_irqs(runs(counts), "judge", floor, note=lambda text: None)
        except Failure as failure:
            raise Failure("host clock judge rejected %r: %s" % (counts, failure)) from None
    for counts, floor, reason in rejected:
        kwargs = {}
        if reason == "done":
            kwargs["tail"] = "x"
        if reason == "st2:0000":
            kwargs["st2"] = "0000"
        try:
            host_clock_irqs(runs(counts, **kwargs), "judge", floor, note=lambda text: None)
        except Failure as failure:
            if reason not in str(failure) and reason not in ("done", "st2:0000"):
                raise Failure(
                    "host clock judge rejected %r for another reason: %s" % (counts, failure)
                ) from None
            continue
        raise Failure("host clock judge accepted %r at floor %02Xh" % (counts, floor))


# A host whose every sleep runs 15 ms long, inside the 20 ms of interrupts
# clk_owe() keeps waiting: the fixed timer counts 32h there in every run and
# a build that keeps only four interrupts waiting counts 10h to 11h, far on either
# side of HOST_CLOCK_IRQ_STALLED (8 ms puts that build exactly on it).
OVERSLEEP = ("main.c", "\tusleep(ms * 1000UL);\n}", "\tusleep(ms * 1000UL + 15000UL);\n}")
OVERSLEEP_BAD = (
    "clock.c",
    "unsigned cap = period ? (CLK_CREDIT_CLOCKS - 1) / period + 1 : 0;",
    "unsigned cap = period ? 0 : 0;",
)


def oversleep_build(src, work, cc, edits):
    """A build of the emulator's sources in src with edits, each of which
    must match exactly once; returns the binary."""
    tree = os.path.join(work, "oversleep")
    binary = os.path.join(work, "i86-oversleep")
    shutil.rmtree(tree, ignore_errors=True)
    os.mkdir(tree)
    for item in SOURCES + HEADERS:
        shutil.copyfile(os.path.join(src, item), os.path.join(tree, item))
    for name, old, new in edits:
        path = os.path.join(tree, name)
        with open(path) as handle:
            text = handle.read()
        if text.count(old) != 1:
            raise Failure("oversleep: %s holds %r %d times" % (name, old, text.count(old)))
        with open(path, "w") as handle:
            handle.write(text.replace(old, new))
    build(tree, binary, cc)
    return binary


def host_sleep_overrun_ms():
    """The longest time, in milliseconds, by which twenty 1 ms sleeps of
    this host run long."""
    over = []
    for _ in range(20):
        start = time.monotonic()
        time.sleep(0.001)
        over.append((time.monotonic() - start) * 1000 - 1)
    return max(over)


def test_oversleep(src, work, cc, calibrate=False):
    """-S 4.77 on a host whose every sleep runs 15 ms long (OVERSLEEP): the
    paced judge passes, since the interrupts that came due during a sleep
    wait and arrive late rather than being lost. With calibrate, the build
    that keeps four interrupts waiting (OVERSLEEP_BAD) must count under
    HOST_CLOCK_IRQ_STALLED in at least three of five single runs."""

    def paced(binary):
        return lambda: pit_run(binary, work, ["-S", "4.77", "-c", "8088"])

    # The fixture's 15 ms and the host's own overrun together must stay
    # under the 20 ms of interrupts clk_owe() keeps waiting; a host whose
    # sleeps already run 2 ms long (the hosted macOS runner's do) drops
    # interrupts with or without a defect, so the fixture cannot judge it.
    overrun = host_sleep_overrun_ms()
    if overrun > 2:
        print("NOT RUN oversleep fixture: this host's 1 ms sleeps run %.1f ms long" % overrun)
        return
    good = oversleep_build(src, work, cc, [OVERSLEEP])
    try:
        host_clock_irqs(paced(good), "-S 4.77, sleeps 15 ms long", HOST_CLOCK_IRQ_STALLED)
    except Failure:
        # A host that stalled during the runs, though not before them,
        # judges itself rather than the build.
        overrun = host_sleep_overrun_ms()
        if overrun > 2:
            print("NOT RUN oversleep fixture: 1 ms sleeps now run %.1f ms long" % overrun)
            return
        raise
    if not calibrate:
        return
    bad = paced(oversleep_build(src, work, cc, [OVERSLEEP, OVERSLEEP_BAD]))
    counts = []
    for _ in range(5):
        got = bad()
        fields = dict(t.split(":", 1) for t in got if ":" in t)
        if (
            "irq" not in fields
            or fields.get("st2") != "00B0"
            or fields.get("tick") != "ok"
            or got[-1:] != ["done"]
        ):
            raise Failure("the four-waiting build did not finish whole: %r" % got)
        counts.append(int(fields["irq"], 16))
    if sum(1 for n in counts if n < HOST_CLOCK_IRQ_STALLED) < 3:
        raise Failure(
            "the paced judge accepts the four-waiting build: %s"
            % ", ".join("%02Xh" % n for n in counts)
        )
    print(
        "oversleep check rejects the four-waiting build: %s"
        % ", ".join("%02Xh" % n for n in counts)
    )


def test_speed(binary, work):
    """-S paces the guest: under -S 4.77 -c 8088 the 131072 instructions
    before "done" alone take 0.36 s (cpu-timing.md: 13.1 clocks each), where
    the whole run takes about 0.1 s unpaced. A lower bound only; the timer's
    interrupts still count the same clocks."""

    def paced():
        start = time.monotonic()
        got = pit_run(binary, work, ["-S", "4.77", "-c", "8088"])
        took = time.monotonic() - start
        if took < 0.3:
            raise Failure("-S 4.77: %.2f s, %r" % (took, got))
        return got

    host_clock_irqs(paced, "-S 4.77", HOST_CLOCK_IRQ_STALLED)


def ems_run(binary, work, args, keys, env=None):
    """tests/ems.asm under args; the lines it wrote to head 1, sectors 1 and 2.
    The emulator must exit 0 and leave no page file."""
    image = os.path.join(work, "ems-run.img")
    shutil.copyfile(os.path.join(work, "ems.img"), image)
    store = os.path.join(work, "page")
    try:
        done = subprocess.run(
            [binary, "-T", "-s", store, "-a", image, "-k", keys] + args,
            capture_output=True,
            timeout=60,
            check=False,
            env=dict(os.environ, **(env or {})),
        )
    except subprocess.TimeoutExpired:
        raise Failure("ems %s: timeout" % " ".join(args)) from None
    if done.returncode != 0:
        raise Failure("ems %s: exit %d" % (" ".join(args), done.returncode))
    if os.path.exists(store):
        raise Failure("ems %s: page file left behind" % " ".join(args))
    with open(image, "rb") as handle:
        handle.seek(9 * 512)
        return handle.read(1024).split(b"\0")[0].decode("latin-1").split()


def test_ems(binary, work):
    """Expanded memory through interrupt 67h: with eight resident pages the
    pages of expanded memory go through the page file, and the lines must
    equal those with 64; without -e the service is absent."""
    want = ems_expected()
    for frames in ("8", "64"):
        got = ems_run(binary, work, ["-q", "-e", "256", "-f", frames], "0")
        if got != want:
            diff = [(g, w) for g, w in zip(got, want) if g != w][:3]
            raise Failure("ems, %s frames: %r (%d lines)" % (frames, diff, len(got)))
    got = ems_run(binary, work, ["-q"], "0")
    if got != ["st:40", "done"]:
        raise Failure("ems absent: %r" % got)


def test_menu_edges(binary, work):
    """The menu under a signal, beside a long directory name and beside a
    file name that carries a terminal command."""
    opened = [(b"VID", KEYS.encode()), (b"DONE", b"\x1d")]
    image = os.path.join(work, "run.img")
    shutil.copyfile(os.path.join(work, "guest.img"), image)
    # A termination signal while the menu waits for its key ends the
    # session at once, the terminal restored.
    _, status, restored = menu_session(binary, work, image, opened, kill=signal.SIGTERM)
    if status != 128 + signal.SIGTERM or not restored:
        raise Failure("signal in the menu: exit %r, terminal restored: %s" % (status, restored))
    # An image whose directory name the menu cannot hold: no other
    # directory's images are offered in its place.
    deep = os.path.join(work, "L" * 60, "M" * 60)
    os.makedirs(deep)
    shutil.copyfile(os.path.join(work, "guest.img"), os.path.join(deep, "run.img"))
    here = os.path.join(work, "here")
    os.mkdir(here)
    shutil.copyfile(os.path.join(work, "guest.img"), os.path.join(here, "decoy.img"))
    steps = opened + [(b"quit", b"a"), (b"too long", b"x"), (b"DONE", b"\x1d"), (b"quit", b"q")]
    seen, status, restored = menu_session(
        binary, work, os.path.join(deep, "run.img"), steps, cwd=here
    )
    if status != 0 or b"decoy.img" in seen or b"image:" in seen:
        raise Failure("long image directory: exit %r, another directory offered" % status)
    shutil.rmtree(os.path.join(work, "L" * 60))
    shutil.rmtree(here)
    # A file name with an escape sequence in it is listed without one.
    odd = os.path.join(work, "odd")
    os.mkdir(odd)
    shutil.copyfile(os.path.join(work, "guest.img"), os.path.join(odd, "run.img"))
    shutil.copyfile(os.path.join(work, "guest.img"), os.path.join(odd, "bad\x1b[7mz.img"))
    steps = opened + [(b"quit", b"a"), (b"image:", b"\x1b"), (b"DONE", b"\x1d"), (b"quit", b"q")]
    seen, status, restored = menu_session(binary, work, os.path.join(odd, "run.img"), steps)
    if status != 0 or b"bad?[7mz.img" not in seen or b"bad\x1b" in seen:
        raise Failure("file name with an escape sequence: exit %r, listed raw" % status)
    shutil.rmtree(odd)


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
        if termios_modes(before) != termios_modes(after) or not seen.endswith(
            b"\x1b[0m\x1b[r\x1b[?7h\x1b[26;1H"
        ):
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
    test_menu_edges(binary, work)
    test_mouse(binary, work)
    test_ems(binary, work)
    test_pit(binary, work)
    test_speed(binary, work)
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
            images = (
                ("guest", guest_image(src, os.path.join(work, "guest.img"))),
                ("mouse", mouse_image(src, os.path.join(work, "mouse.img"))),
                ("ems", boot_image(src, "ems", os.path.join(work, "ems.img"))),
                ("pit", boot_image(src, "pit", os.path.join(work, "pit.img"))),
            )
            nasm = shutil.which("nasm")
            for name, data in images:
                if nasm:
                    built = os.path.join(work, name + ".bin")
                    subprocess.run(
                        [nasm, "-f", "bin", "-o", built, os.path.join(src, "tests", name + ".asm")],
                        check=True,
                    )
                    with open(built, "rb") as handle:
                        if handle.read() != data:
                            raise Failure("%s.hexdump differs from %s.asm" % (name, name))
                    print("%s.hexdump matches %s.asm" % (name, name))
                else:
                    print(
                        "NOT RUN %s.hexdump against %s.asm: nasm (package nasm) is absent"
                        % (name, name)
                    )
            if nasm:
                built = os.path.join(work, "EMS.SYS")
                subprocess.run(
                    [nasm, "-f", "bin", "-o", built, os.path.join(src, "dos", "ems.asm")],
                    check=True,
                )
                with open(os.path.join(src, "dos", "ems.hexdump")) as handle:
                    listed = bytes.fromhex("".join(handle.read().split()))
                with open(built, "rb") as handle:
                    if handle.read() != listed:
                        raise Failure("dos/ems.hexdump differs from dos/ems.asm")
                print("dos/ems.hexdump matches dos/ems.asm")
            binary = os.path.join(work, "i86-host")
            build(src, binary, cc)
            csources = ["tests/clocktest.c", "clock.c"]
            clocktest = os.path.join(work, "clocktest")
            build(src, clocktest, cc, csources)
            print(test_clock(clocktest))
            # Independent of the emulator, so once, not for every mutant.
            test_wait_draining()
            test_host_clock_judge()
            test(binary, work)
            test_oversleep(src, work, cc, calibrate=True)
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
            panel = load_panel(src)
            if nasm:
                built = os.path.join(work, "panel.bin")
                subprocess.run(
                    [nasm, "-f", "bin", "-o", built, os.path.join(src, "tests", "panel.asm")],
                    check=True,
                )
                with open(os.path.join(src, "tests", "panel.hexdump")) as handle:
                    listed = bytes.fromhex("".join(handle.read().split()))
                with open(built, "rb") as handle:
                    if handle.read() != listed:
                        raise Failure("panel.hexdump differs from panel.asm")
                print("panel.hexdump matches panel.asm")
            else:
                print("NOT RUN panel.hexdump against panel.asm: nasm (package nasm) is absent")
            psources = SOURCES + ["tests/panel.c", "-DGFX_MODEL"]
            dsources = ["tests/draintest.c", "gfx.c", "-DGFX_MODEL"]
            draintest = os.path.join(work, "draintest")
            build(src, draintest, cc, dsources)
            print(test_drain(draintest))
            pbinary = os.path.join(work, "i86-panel")
            build(src, pbinary, cc, psources)
            try:
                panel.test(pbinary, src, work)
            except panel.Failure as failure:
                raise Failure(str(failure)) from None
            print("i86 panel display test passed")
            if "--no-mutants" in argv:
                return 0
            for number, (name, old, new) in enumerate(MUTANTS, 1):
                tree = os.path.join(work, "mutant")
                shutil.rmtree(tree, ignore_errors=True)
                os.mkdir(tree)
                os.mkdir(os.path.join(tree, "tests"))
                mutate(src, tree, number, name, old, new)
                mutant = os.path.join(work, "i86-mutant")
                build(tree, mutant, cc)
                build(tree, sstrun + "-mutant", cc, vsources)
                build(tree, clocktest + "-mutant", cc, csources)
                try:
                    test_clock(clocktest + "-mutant")
                    test(mutant, work)
                    test_vectors(sstrun + "-mutant", src, work)
                    test_vectors(sstrun + "-mutant", src, work, suite="80286")
                    test_undefined(sstrun + "-mutant", work)
                    test_rep_burst(sstrun + "-mutant", work)
                    test_oversleep(tree, work, cc)
                except (Failure, subprocess.TimeoutExpired, subprocess.CalledProcessError):
                    # A mutant that hangs or dies has failed like any other. The
                    # line keeps the step's idle timer fed through the stage.
                    print(
                        "i86 mutant %d of %d rejected: %s" % (number, len(MUTANTS), name),
                        flush=True,
                    )
                    continue
                raise Failure("mutant %d survives: %s: %r" % (number, name, old))
            print("%d mutants fail the test" % len(MUTANTS))
            for number, (name, old, new) in enumerate(panel.MUTANTS, 1):
                tree = os.path.join(work, "mutant")
                shutil.rmtree(tree, ignore_errors=True)
                os.mkdir(tree)
                os.mkdir(os.path.join(tree, "tests"))
                mutate(src, tree, number, name, old, new)
                mutant = os.path.join(work, "i86-mutant")
                build(tree, mutant, cc, psources)
                build(tree, draintest + "-mutant", cc, dsources)
                try:
                    test_drain(draintest + "-mutant")
                    panel.test(mutant, src, work)
                except (
                    Failure,
                    panel.Failure,
                    subprocess.TimeoutExpired,
                    subprocess.CalledProcessError,
                ):
                    print(
                        "i86 panel mutant %d of %d rejected: %s"
                        % (number, len(panel.MUTANTS), name),
                        flush=True,
                    )
                    continue
                raise Failure("panel mutant %d survives: %s: %r" % (number, name, old))
            print("%d mutants fail the panel display test" % len(panel.MUTANTS))
        except (Failure, subprocess.TimeoutExpired, subprocess.CalledProcessError) as failure:
            sys.stderr.write("i86 check: %s\n" % failure)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
