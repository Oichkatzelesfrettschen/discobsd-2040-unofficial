#!/usr/bin/env python3
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its
#    contributors may be used to endorse or promote products derived from
#    this software without specific prior written permission.
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

"""Deliver signals and switch processes from the clock tick under Renode.

The flash image this driver boots is the shipped root with
tests/rp2040/preempt/preempt added under /tmp (check-preempt.sh composes
it). Every case runs a loop that makes no system call, so only the
asynchronous system trap -- the return to the process through userret from
SysTick -- can deliver its signal or switch it out. A kernel without that
path leaves the emulated machine running one loop forever, so each case
boots its own machine, and a timed-out case cannot hide the next.

alarm
    "preempt alarm" calls alarm(1) and loops; the shell must report
    status 142 (128 + SIGALRM).
share
    two looping children and a parent in sleep(3): "PREEMPT share parent
    woke", one count line per child with a nonzero count, and "PREEMPT
    share OK".
intr
    "preempt spin" loops; the driver types the interrupt character
    (ttychars.h CINTR, control-C) into UART0, the shell prompt must return
    and `echo $?` must print 130 (128 + SIGINT).
regs
    sentinels in r1-r12 and lr survive 200 SIGALRM handlers that overwrite
    r8-r12, at sp 0 and 4 modulo 8, in two processes: "PREEMPT regs OK".
divide
    two processes divide on the SIO divider while their handlers divide
    other operands and a third process forces switches every 2 ms; every
    result equals a software division: "PREEMPT divide OK".
altstack
    a handler on an alternate stack of exactly MINSIGSTKSZ bytes, its base
    7 bytes past an eight-byte boundary, finds its sp inside that stack and
    leaves the guard bytes below the base unwritten: "PREEMPT altstack OK".

The claim is the emulated kernel's AST path. The third-party peripheral
models carry the boot, and Renode's divider computes in zero cycles; a
Pico's result is a separate evidence class (sys/arch/rp2040/doc/TESTING.md).

Usage: check-preempt.py RENODE UNIX_BIN FLASH_BIN OUTPUT_DIR [CASE...]
Exit status: 0 pass, 1 an assertion failed, 2 the emulator did not answer.
"""

import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from renode_monitor import Renode, RenodeError  # noqa: E402

CONFIG = "PICO_UART"
CASE_BUDGET_SECONDS = 120.0
INTR_DELAY_SECONDS = 2.0
CINTR = 0x03

# Each case: the command typed, the lines required in its output, and the
# exit status the shell must print.
CASES = {
    "alarm": ("/tmp/preempt alarm", ["PREEMPT alarm start"], 142),
    "share": ("/tmp/preempt share", ["PREEMPT share parent woke", "PREEMPT share OK"], 0),
    "intr": ("/tmp/preempt spin", ["PREEMPT spin start"], 130),
    "regs": ("/tmp/preempt regs", ["PREEMPT regs OK"], 0),
    "divide": ("/tmp/preempt divide", ["PREEMPT divide OK"], 0),
    "altstack": ("/tmp/preempt altstack", ["PREEMPT altstack OK"], 0),
}


def load_boot():
    """Import check-boot.py, whose name is not an identifier."""
    spec = importlib.util.spec_from_file_location("check_boot", os.path.join(HERE, "check-boot.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def reach_shell(console, boot) -> None:
    console.wait_lines([f"DiscoBSD 2.7 ({CONFIG})"], "kernel banner", boot.PROBE_BUDGET_SECONDS)
    console.wait_lines(boot.LOGIN_LINES, "file system check", boot.LOGIN_BUDGET_SECONDS)
    console.wait_lines(["login:"], "login prompt", boot.LOGIN_BUDGET_SECONDS)
    console.type_line("operator")
    console.wait_lines(["$"], "shell prompt", boot.COMMAND_BUDGET_SECONDS)


def run_case(console, boot, name: str) -> None:
    command, required, status = CASES[name]
    start = console.cursor
    try:
        if name == "intr":
            # An interactive shell abandons the rest of a command line on
            # SIGINT, so the status is asked for once the prompt is back.
            console.type_line(command)
            console.wait_for("\nPREEMPT spin start", "spin start", CASE_BUDGET_SECONDS)
            for _ in range(int(INTR_DELAY_SECONDS / boot.SLICE_SECONDS)):
                console.advance()
            console.renode.command(f"sysbus.uart0 WriteChar {CINTR:#04x}")
            console.wait_for("$", "prompt after the interrupt character", CASE_BUDGET_SECONDS)
            console.type_line("echo rc=$?")
        else:
            console.type_line(f"{command}; echo rc=$?")
        # The typed command echoes "rc=$?"; the status line is "rc=" and digits.
        console.wait_for("\nrc=", f"{name} exit status", CASE_BUDGET_SECONDS)
        console.wait_for("$", f"prompt after {name}", boot.COMMAND_BUDGET_SECONDS)
    finally:
        lines = console.text()[start:].replace("\r", "").split("\n")
        for line in lines:
            if line.startswith(("PREEMPT", "FAIL", "rc=")):
                print(f"check-preempt: {name}: {line}")
    for line in required:
        if line not in lines:
            console.fail(f"{name}: no {line!r} line")
    if f"rc={status}" not in lines:
        console.fail(f"{name}: exit status is not {status}")
    if any(line.startswith("FAIL") for line in lines):
        console.fail(f"{name}: a check printed FAIL")


def boot_case(boot, renode_path: str, unix_bin: str, flash_bin: str, outdir: str, name: str) -> int:
    """Boot one machine for case NAME; return its failure count, or -1 on emulator loss."""
    transcript = os.path.join(outdir, f"uart0-{name}.txt")
    if os.path.exists(transcript):
        os.remove(transcript)
    renode = Renode(renode_path, os.path.join(outdir, f"renode-{name}.log"), boot.WALL_SECONDS)
    console = boot.Console(renode, transcript)
    try:
        renode.command(f"$unix_bin=@{unix_bin}")
        renode.command(f"$flash_bin=@{flash_bin}")
        renode.command(f"include @{os.path.join(HERE, 'machine.resc')}")
        renode.command(f"sysbus.uart0 CreateFileBackend @{transcript} true")
        reach_shell(console, boot)
        run_case(console, boot, name)
    except boot.StageFailed:
        print(f"check-preempt: {name}: transcript in {transcript}", file=sys.stderr)
    except RenodeError as error:
        print(f"check-preempt: {name}: {error}", file=sys.stderr)
        renode.close()
        return -1
    renode.close()
    print(
        f"check-preempt: {name}: {'pass' if not console.failures else 'FAIL'} "
        f"after {console.virtual:.1f} virtual seconds"
    )
    return len(console.failures)


def main(argv: list[str]) -> int:
    if len(argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    renode_path, unix_bin, flash_bin, outdir = argv[:4]
    names = argv[4:] or list(CASES)
    unknown = [name for name in names if name not in CASES]
    if unknown:
        print(
            f"check-preempt: unknown case {unknown[0]}; cases are {', '.join(CASES)}",
            file=sys.stderr,
        )
        return 2
    boot = load_boot()
    os.makedirs(outdir, exist_ok=True)
    failed = []
    for name in names:
        result = boot_case(boot, renode_path, unix_bin, flash_bin, outdir, name)
        if result < 0:
            return 2
        if result:
            failed.append(name)
    if failed:
        print(f"check-preempt: FAIL {' '.join(failed)}")
        return 1
    print("check-preempt: pass")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
