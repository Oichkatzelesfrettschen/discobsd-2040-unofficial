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

"""Time the boot-time fsck of the flash root under Renode, clean and after a cut.

The driver boots the PICO_UART kernel on tools/renode/machine.resc, the
machine check-renode boots, and advances virtual time in short slices so the
console lines of /etc/rc carry a virtual timestamp to within one slice. It
then logs in, becomes root, and ends each trial with "reboot -n", which
skips sync(2), the buffer flush and the shutdown checkpoint: the next boot
sees the flash root as a processor reset at that instant would leave it.
The flash model is the same memory object across the watchdog reset, so the
written root survives into the next boot.

Slices are 0.25 virtual s outside the measured spans and 0.02 s from the
kernel banner to fsck's outcome, so each span is known to within 0.02 s.

Trials, in order on one flash image:
  clean     the first boot of the built image;
  floor     at the root shell, fsck -p on a node that does not exist (exec,
            start-up and exit, the cost a clean flag cannot remove), then
            "sync; fsck -p /dev/fl0a" on the mounted root, which openi()
            refuses at securelevel 1 and so gives a second floor;
  synced    a metadata workload, sync, then the cut (the negative control:
            fsck must find nothing);
  unlinked  a synced file held open by a background reader and removed,
            sync, then the cut (the positive control: the next boot holds
            an allocated inode with no link, which fsck must repair);
  cut-now   the same workload under new names, then the cut at once;
  cut-late  the workload, a two-second pause, then the cut;
  overwrite a file written and synced, then rewritten in place by
            dd conv=notrunc (no allocation), then the cut.
Every boot's fsck span (from the rc banner to the summary line), its output
and its outcome (clean, repaired-and-rebooted, failed) go to a TSV row.

Usage: renode_fsck_timing.py RENODE TOOLS_RENODE UNIX_BIN FLASH_BIN OUTDIR
Exit status: 0 measured, 1 a stage never arrived, 2 the emulator stopped.
The controls decide whether a row counts: the first boot and the synced
trial must check clean and the unlinked trial must be repaired; a run where
any of them fails is reported as uncalibrated (exit 1) and its rows are not
evidence.
"""

import importlib.util
import os
import re
import sys

FINE = 0.02
COARSE = 0.25
BOOT_BUDGET = 60.0
COMMAND_BUDGET = 30.0

SUMMARY = re.compile(r"/dev/fl0a: (\d+) files, (\d+) used, (\d+) free")
OUTCOMES = (
    ("login:", "clean"),
    ("Errors repaired, rebooting", "repaired"),
    ("Critical errors detected", "critical"),
    ("Filesystem check failed", "failed"),
    ("Unknown error in fsck", "unknown"),
)
BANNER = "DiscoBSD 2.7 (PICO_UART)"
RC_START = "Automatic boot in progress: starting file system checks."

# One short command a line: the shell's line editor truncates a typed line
# near 130 columns, and each line also carries its marker echo.
WORKLOAD = ("mkdir /o12{n}a /o12{n}b", "cp /etc/rc /o12{n}a/rc",
            "cp /bin/sh /o12{n}b/sh", "echo hello > /o12{n}c",
            "ln /o12{n}b/sh /o12{n}b/sh2", "rm /o12{n}a/rc")


def workload(tag):
    return [line.format(n=tag) for line in WORKLOAD]


class Stopped(Exception):
    """An expected console line did not arrive inside its budget."""


class Clock:
    """Virtual time advanced in fixed slices, with the UART0 transcript."""

    def __init__(self, renode, transcript):
        self.renode = renode
        self.transcript = transcript
        self.virtual = 0.0
        self.cursor = 0
        self.slice = COARSE

    def text(self):
        try:
            with open(self.transcript, encoding="utf-8", errors="replace") as handle:
                return handle.read().replace("\r", "")
        except FileNotFoundError:
            return ""

    def advance(self):
        self.renode.command(f'emulation RunFor "{self.slice}"')
        self.virtual += self.slice

    def wait_any(self, needles, budget):
        """Return (needle, end offset, virtual time) of the first needle found."""
        spent = 0.0
        while True:
            text = self.text()
            best = None
            for needle in needles:
                if isinstance(needle, re.Pattern):
                    match = needle.search(text, self.cursor)
                    found = (match.start(), match.end()) if match else None
                else:
                    index = text.find(needle, self.cursor)
                    found = (index, index + len(needle)) if index >= 0 else None
                if found and (best is None or found[0] < best[1][0]):
                    best = (needle, found)
            if best is not None:
                self.cursor = best[1][1]
                return best[0], best[1], self.virtual
            if spent >= budget:
                raise Stopped(f"none of {needles!r} after {self.virtual:.2f} virtual s")
            self.advance()
            spent += self.slice

    def type_line(self, line):
        for byte in (line + "\r").encode():
            self.renode.command(f"sysbus.uart0 WriteChar {byte:#04x}")

    def run(self, line, marker):
        """Type a command followed by an echo whose output alone spells marker."""
        head, tail = marker[:3], marker[3:]
        # A background job ends its list with "&", which takes no ";" after it.
        separator = " " if line.rstrip().endswith("&") else "; "
        self.type_line(f'{line}{separator}echo "{head}""{tail}"')
        self.wait_any([marker + "\n"], COMMAND_BUDGET)


def boot(clock):
    """Follow one boot from the banner through rc's fsck to its outcome.

    The summary and every outcome line are awaited together, because a
    preen run that stops (exit 8) prints no summary.
    """
    rows = []
    outcomes = [n for n, _ in OUTCOMES]
    while True:
        clock.slice = COARSE
        _, _, t_banner = clock.wait_any([BANNER], BOOT_BUDGET)
        # Fine from the banner on: rc's line and fsck's summary can fall in
        # one coarse slice, which would read as a zero span.
        clock.slice = FINE
        _, (start, _), t_rc = clock.wait_any([RC_START], BOOT_BUDGET)
        found, (s0, s1), t_first = clock.wait_any([SUMMARY] + outcomes, BOOT_BUDGET)
        summary = None
        if found is SUMMARY:
            summary = SUMMARY.search(clock.text(), s0)
            t_sum = t_first
            found, _, t_end = clock.wait_any(outcomes, BOOT_BUDGET)
        else:
            t_sum = t_end = t_first
        clock.slice = COARSE
        outcome = dict(OUTCOMES)[found]
        text = clock.text()
        output = text[start + len(RC_START):clock.cursor].strip().replace("\n", " | ")
        rows.append({
            "banner": t_banner, "rc": t_rc, "summary": t_sum, "end": t_end,
            "fsck_span": round(t_sum - t_rc, 3), "outcome": outcome,
            "files": summary.group(1) if summary else "-",
            "used": summary.group(2) if summary else "-",
            "free": summary.group(3) if summary else "-", "output": output,
        })
        if outcome != "repaired":
            return rows


def timed(clock, line, marker):
    """Virtual seconds from typing line to its marker, at the fine slice."""
    clock.slice = FINE
    start = clock.virtual
    clock.run(line, marker)
    clock.slice = COARSE
    return round(clock.virtual - start, 3)


def root_shell(clock):
    clock.type_line("operator")
    clock.wait_any(["$"], COMMAND_BUDGET)
    clock.type_line("su")
    clock.wait_any(["#"], COMMAND_BUDGET)


def trial(clock, name, commands, writer):
    root_shell(clock)
    for index, command in enumerate(commands):
        clock.run(command, f"o12-{name}-{index}")
    clock.type_line("reboot -n")
    for number, row in enumerate(boot(clock)):
        writer(name, number, row)


def main(argv):
    if len(argv) != 5:
        print(__doc__, file=sys.stderr)
        return 2
    renode_path, tools_renode, unix_bin, flash_bin, outdir = argv
    sys.path.insert(0, tools_renode)
    spec = importlib.util.spec_from_file_location(
        "renode_monitor", os.path.join(tools_renode, "renode_monitor.py"))
    monitor = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(monitor)
    os.makedirs(outdir, exist_ok=True)
    transcript = os.path.join(outdir, "uart0.txt")
    if os.path.exists(transcript):
        os.remove(transcript)
    table = open(os.path.join(outdir, "fsck-timing.tsv"), "w", encoding="utf-8")
    table.write("trial\tboot\tbanner_s\trc_s\tsummary_s\tend_s\tfsck_span_s\t"
                "outcome\tfiles\tused\tfree\toutput\n")
    outcomes = {}

    def writer(name, number, row):
        outcomes.setdefault(name, []).append(row["outcome"])
        table.write(f"{name}\t{number}\t{row['banner']:.2f}\t{row['rc']:.2f}\t"
                    f"{row['summary']:.2f}\t{row['end']:.2f}\t{row['fsck_span']:.2f}\t"
                    f"{row['outcome']}\t{row['files']}\t{row['used']}\t{row['free']}\t"
                    f"{row['output']}\n")
        table.flush()
        print(f"o12: {name} boot {number}: fsck {row['fsck_span']:.2f} virtual s, "
              f"{row['outcome']}", flush=True)

    renode = monitor.Renode(renode_path, os.path.join(outdir, "renode.log"), 7200.0)
    clock = Clock(renode, transcript)
    status = 0
    try:
        renode.command(f"$unix_bin=@{unix_bin}")
        renode.command(f"$flash_bin=@{flash_bin}")
        renode.command(f"include @{os.path.join(tools_renode, 'machine.resc')}")
        renode.command(f"sysbus.uart0 CreateFileBackend @{transcript} true")
        print("o12: cpu0 PerformanceInMips "
              + renode.command("sysbus.cpu0 PerformanceInMips").strip(), flush=True)
        for number, row in enumerate(boot(clock)):
            writer("clean", number, row)
        root_shell(clock)
        clock.run("sync", "o12-presync")
        floor = timed(clock, "fsck -p /dev/o12none", "o12-floor")
        shell = timed(clock, "sync; fsck -p /dev/fl0a", "o12-shell")
        print(f"o12: floor {floor:.2f} virtual s, shell check {shell:.2f} virtual s",
              flush=True)
        table.write(f"floor\t0\t-\t-\t-\t-\t{floor:.2f}\tfloor\t-\t-\t-\t"
                    "fsck -p on a missing node\n")
        table.write(f"shell\t0\t-\t-\t-\t-\t{shell:.2f}\tshell\t-\t-\t-\t"
                    "sync; fsck -p /dev/fl0a at the root shell\n")
        clock.type_line("exit")
        clock.type_line("exit")
        clock.wait_any(["login:"], COMMAND_BUDGET)
        trial(clock, "synced", workload("s") + ["sync"], writer)
        trial(clock, "unlinked", ["cp /etc/rc /o12u; sync",
                                  "sleep 100 < /o12u &",
                                  "rm /o12u; sync"], writer)
        trial(clock, "cut-now", workload("n"), writer)
        trial(clock, "cut-late", workload("l") + ["sleep 2"], writer)
        trial(clock, "overwrite", ["cp /etc/rc /o12w; sync",
                                   "dd if=/etc/fstab of=/o12w conv=notrunc"],
              writer)
    except Stopped as error:
        print(f"o12: stopped: {error}", file=sys.stderr)
        status = 1
    except monitor.RenodeError as error:
        print(f"o12: {error}", file=sys.stderr)
        status = 2
    renode.close()
    table.close()
    if outcomes.get("clean", [None])[0] != "clean" or \
            outcomes.get("synced", [None])[0] != "clean" or \
            outcomes.get("unlinked", [None])[0] != "repaired":
        print("o12: UNCALIBRATED: a control boot did not give its expected outcome",
              file=sys.stderr)
        status = status or 1
    print(f"o12: RESULT status={status}", flush=True)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
