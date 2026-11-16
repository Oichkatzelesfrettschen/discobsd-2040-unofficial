#!/usr/bin/env python3
# Copyright (c) 2026 GizmoBSD
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of the copyright holder nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Boot a UART0-descriptor kernel under Renode, start a UnixBench program from
the operator shell and record the executed program counters and memory
accesses for a slice of virtual time while it runs.

Usage: RENODE_TOOLS=tools/renode trace_run.py RENODE UNIX_BIN FLASH_BIN OUTDIR "COMMAND"
           WARM_SECONDS TRACE_SECONDS

UNIX_BIN is a kernel image whose board descriptor names the UART0 console
(distrib/rp2040/mkboarduf2.py --flat --board=PICO_UART), FLASH_BIN a root
image holding COMMAND. The driver reuses tools/renode/check-boot.py's machine
and console, logs in as operator, types COMMAND, lets WARM_SECONDS of virtual
time pass and traces the next TRACE_SECONDS. RP2040_MODELS overrides the
Renode_RP2040 model tree. Writes OUTDIR/uart0.txt, OUTDIR/renode.log and
OUTDIR/trace.txt. COMMAND runs between two echoed markers: the start marker
must reach the console, and the end marker must not reach it before tracing
starts or before it stops, so a command that is missing or exits early fails
with status 1 and leaves no trace.
Exit status: 0 traced while the command ran, 1 the command never started or
ended before the trace finished, or the trace file is missing, empty or holds
no instruction, 2 Renode stopped answering or the arguments are incomplete.
`trace_run.py --self-test` checks the trace-file verdict without Renode.
"""
import importlib.util
import os
import re
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
RENODE_DIR = os.environ.get("RENODE_TOOLS", "")
cb = None

# The first line of a PCAndOpcode trace: an address, a colon and an opcode.
TRACE_LINE = re.compile(rb"^0x[0-9A-F]+: 0x[0-9A-F]+\r?$", re.M)


def load_check_boot():
    """Import tools/renode/check-boot.py, whose Renode and Console this reuses."""
    global cb
    sys.path.insert(0, RENODE_DIR)
    spec = importlib.util.spec_from_file_location(
        "check_boot", os.path.join(RENODE_DIR, "check-boot.py"))
    cb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cb)


def trace_problem(path):
    """Return why path holds no recorded instruction, or None when it holds one.

    Renode answers a rejected monitor command with text rather than a failure,
    so a tracer that never started leaves no file or an empty one."""
    if not os.path.exists(path):
        return "no trace file was written"
    with open(path, "rb") as handle:
        head = handle.read(4096)
    if not head:
        return "the trace file is empty"
    if not TRACE_LINE.search(head):
        return "the trace file holds no instruction line"
    return None


def self_test():
    with tempfile.TemporaryDirectory() as scratch:
        path = os.path.join(scratch, "trace.txt")
        cases = [("a missing trace", None, False), ("an empty trace", b"", False),
                 ("a monitor error text", b"There was an error executing command\n", False),
                 ("a recorded instruction", b"0x10010C12: 0x601D\n", True)]
        status = 0
        for what, content, want in cases:
            if os.path.exists(path):
                os.remove(path)
            if content is not None:
                with open(path, "wb") as handle:
                    handle.write(content)
            got = trace_problem(path) is None
            print(f"self-test: {what}: {'accepted' if got else 'refused'}, "
                  f"{'ok' if got == want else 'WRONG'}")
            status |= got != want
    print("trace_run self-test: " + ("pass" if status == 0 else "FAIL"))
    return status


# Shell output that brackets COMMAND on the console. Each is typed with an
# empty quoted pair inside, so the echoed command line never holds the literal
# text and only the shell's own output matches.
START = "o14-trace-start"
END = "o14-trace-end"


def typed(text):
    return f"echo {text[:4]}''{text[4:]}"


def still_running(console, when):
    """Fail when COMMAND has ended (its end marker reached the console)."""
    if END in console.text()[console.cursor:]:
        console.fail(f"{when}: the command ended, so the trace would not show it running")
        raise cb.StageFailed(when)


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) != 7 or not RENODE_DIR:
        print(__doc__, file=sys.stderr)
        return 2
    load_check_boot()
    renode_path, unix_bin, flash_bin, outdir, command, warm, span = argv
    os.makedirs(outdir, exist_ok=True)
    transcript = os.path.join(outdir, "uart0.txt")
    trace = os.path.join(outdir, "trace.txt")
    for path in (transcript, trace):
        if os.path.exists(path):
            os.remove(path)
    renode = cb.Renode(renode_path, os.path.join(outdir, "renode.log"), 3000.0)
    console = cb.Console(renode, transcript)
    try:
        renode.command(f"$unix_bin=@{unix_bin}")
        renode.command(f"$flash_bin=@{flash_bin}")
        if os.environ.get("RP2040_MODELS"):
            renode.command(f"$repo_dir=@{os.environ['RP2040_MODELS']}")
        renode.command(f"include @{os.path.join(RENODE_DIR, 'machine.resc')}")
        renode.command(f"sysbus.uart0 CreateFileBackend @{transcript} true")
        console.wait_lines(["login:"], "login prompt", 400.0)
        console.type_line("operator")
        console.wait_lines(["$"], "shell prompt", 60.0)
        console.type_line(f"{typed(START)}; {command}; {typed(END)}")
        console.wait_for(START, "command start", 30.0)
        t = 0.0
        while t < float(warm):
            console.advance()
            t += cb.SLICE_SECONDS
        still_running(console, "before tracing")
        print(renode.command(f'sysbus.cpu0 CreateExecutionTracing "tr" @{trace} PCAndOpcode'))
        print(renode.command("tr TrackMemoryAccesses"))
        print(renode.command(f'emulation RunFor "{span}"'))
        print(renode.command("sysbus.cpu0 DisableExecutionTracing"))
        print(renode.command("sysbus.cpu0 PC"))
        still_running(console, "after tracing")
    except cb.StageFailed:
        renode.close()
        # A trace recorded while the command was not running is not evidence
        # of it, so it is removed rather than left beside a failed status.
        if os.path.exists(trace):
            os.remove(trace)
        return 1
    except cb.RenodeError as error:
        print(f"trace_run: {error}", file=sys.stderr)
        renode.close()
        return 2
    renode.close()
    problem = trace_problem(trace)
    if problem:
        print(f"trace_run: {problem}", file=sys.stderr)
        return 1
    print(f"trace_run: virtual {console.virtual:.1f} s, trace {os.path.getsize(trace)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
