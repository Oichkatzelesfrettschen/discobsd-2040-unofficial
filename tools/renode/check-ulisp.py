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

"""Run the uLisp interpreter on the DiscoBSD kernel under Renode.

Usage: check-ulisp.py RENODE UNIX_BIN FLASH_BIN OUTDIR

Boots the PICO_UART kernel the way check-boot.py does, logs in as operator
and drives /usr/bin/ulisp over UART0. The flash image must carry the ulisp
closure (IMAGE_WITH=ulisp). The stages cover what the qemu loader of
tests/qemu_aout cannot serve: exec of the a.out by the kernel, the
workspace taken with the kernel's break, the directory read, the timer,
the tty with its interrupt character, signal delivery for a fault the MPU
raises, and a second process that loads the first one's image.

The kernel has no PicoCalc device, so the drawing, pin and analogue
functions are held to their error messages; no panel, key or sound code
runs here.
"""

import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STEP_SECONDS = 120.0

# A line typed at the uLisp prompt and text that must follow it. The tty
# echoes the line, so each expectation is text the line does not contain.
LISP_STEPS = [
    ("(* 1111 3)", "\n3333\n"),
    ("(defun deep (n) (if (= n 0) 0 (+ 1 (deep (- n 1)))))", "\ndeep\n"),
    ("(deep 123)", "\n123\n"),
    ("(deep 100000)", "Error: stack overflow"),
    ("(deep 124)", "\n124\n"),
    ('(with-sd-card (s "t.tmp" 2) (print (list 7 8 9) s))', "\n(7 8 9)\n"),
    ('(with-sd-card (s "t.tmp") (cdr (read s)))', "\n(8 9)\n"),
    ('(if (member "t.tmp" (directory) :test string=) (* 1111 6) 0)', "\n6666\n"),
    ("(documentation 'cdr)", "Returns a list with the first item removed"),
    ("(delay 250)", "\n250\n"),
    ("(defcode add1 (x) #x3001 #x4770)", "\nadd1\n"),
    ("(add1 4241)", "\n4242\n"),
    ("(register :gpio-in)", "Error: memory fault"),
    ("(register :gpio-in)", "Error: memory fault"),
    ("(* 1111 4)", "\n4444\n"),
    ("(draw-pixel 1 1)", "Error: no pixel display"),
    ("(pinmode 9 :output)", "Error: the pins need the superuser"),
    ("(analogwrite 9 100)", "Error: the pins need the superuser"),
    ("(integerp (analogread 26))", "\nt\n"),
    ("(list 3.5 1e-4 (expt 2 0.5) (sin 1.0))", "(3.5 1.0e-4 1.41421 0.841471)"),
    ('(defvar *kept* (list \'a-long-symbol-name "text" 2.5))', "\n*kept*\n"),
    ('(save-image "t.img")', "> "),
]

# A shell command and the exit status it must report.
SHELL_STEPS = [
    ("echo '(* 1111 7)' | ulisp; echo piped=$?", "piped=0"),
    ("ulisp < /dev/null; echo empty=$?", "empty=0"),
    ("ulisp -w 5; echo small=$?", "small=1"),
    ("ulisp -x; echo usage=$?", "usage=2"),
]


def load_boot():
    spec = importlib.util.spec_from_file_location("check_boot", os.path.join(HERE, "check-boot.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_stages(boot, console) -> None:
    console.wait_lines(["login:"], "login prompt", boot.LOGIN_BUDGET_SECONDS)
    console.type_line("operator")
    console.wait_lines(["$"], "shell prompt", boot.COMMAND_BUDGET_SECONDS)
    console.type_line("ulisp")
    console.wait_lines(["uLisp 4.8f"], "ulisp starts", STEP_SECONDS)
    for line, expected in LISP_STEPS:
        console.type_line(line)
        console.wait_lines([expected], f"ulisp {line[:44]}", STEP_SECONDS)
    # The tty's interrupt character ends a loop that would never return.
    console.type_line("(loop)")
    for _ in range(4):
        console.advance()
    console.renode.command("sysbus.uart0 WriteChar 0x03")
    console.wait_lines(["escape!"], "interrupt ends (loop)", STEP_SECONDS)
    console.type_line("(* 1111 5)")
    console.wait_lines(["\n5555\n"], "evaluation after the interrupt", STEP_SECONDS)
    # End of input on the tty ends the interpreter.
    console.renode.command("sysbus.uart0 WriteChar 0x04")
    console.wait_lines(["$"], "end of input returns to the shell", STEP_SECONDS)
    for line, expected in SHELL_STEPS:
        console.type_line(line)
        console.wait_lines([expected], f"shell {line[:44]}", STEP_SECONDS)
    console.type_line("ulisp -i t.img")
    console.wait_lines(["uLisp 4.8f"], "second process", STEP_SECONDS)
    console.type_line("*kept*")
    console.wait_lines(
        ['(a-long-symbol-name "text" 2.5)'], "image loaded by a second process", STEP_SECONDS
    )
    console.type_line("(deep 50)")
    console.wait_lines(["\n50\n"], "function from the image", STEP_SECONDS)


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    renode_path, unix_bin, flash_bin, outdir = argv
    boot = load_boot()
    os.makedirs(outdir, exist_ok=True)
    transcript = os.path.join(outdir, "uart0.txt")
    if os.path.exists(transcript):
        os.remove(transcript)
    renode = boot.Renode(renode_path, os.path.join(outdir, "renode.log"), boot.WALL_SECONDS)
    console = boot.Console(renode, transcript)
    try:
        renode.command(f"$unix_bin=@{unix_bin}")
        renode.command(f"$flash_bin=@{flash_bin}")
        renode.command(f"include @{os.path.join(HERE, 'machine.resc')}")
        renode.command(f"sysbus.uart0 CreateFileBackend @{transcript} true")
        run_stages(boot, console)
    except boot.StageFailed:
        print(f"check-renode: transcript in {transcript}", file=sys.stderr)
    except boot.RenodeError as error:
        print(f"check-renode: {error}", file=sys.stderr)
        renode.close()
        return 2
    renode.close()
    if console.failures:
        return 1
    print("check-renode: ulisp pass")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
