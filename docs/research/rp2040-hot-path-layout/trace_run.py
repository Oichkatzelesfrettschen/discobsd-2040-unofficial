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
ended before the trace finished, 2 Renode stopped answering.
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RENODE_DIR = os.environ["RENODE_TOOLS"]
sys.path.insert(0, RENODE_DIR)
spec = importlib.util.spec_from_file_location(
    "check_boot", os.path.join(RENODE_DIR, "check-boot.py"))
cb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cb)


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
    size = os.path.getsize(trace) if os.path.exists(trace) else -1
    print(f"trace_run: virtual {console.virtual:.1f} s, trace {size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
