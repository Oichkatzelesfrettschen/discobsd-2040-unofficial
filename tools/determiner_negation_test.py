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
"""Calibrate tools/determiner_negation.py on known-bad and known-good prose.

The known-bad lines are the wording PR #798 and PR #799 replaced; the
known-good lines are their replacements. Each must sort as stated before a
gate's verdict built on the finder counts.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import determiner_negation as finder  # noqa: E402

KNOWN_BAD = (
    "caller supplies a new one, so a write passes no old buffer and costs one",
    "the hosted run boots the PICO board's image, which carries no backlight",
    "the target shell's read; the gate boots no such image, and the panel's",
    "target that skips an optional variant establishes no execution for that",
    "input it should reject reports nothing.",
    "file is absent or holds no one-to-three-digit number",
    "Apple's own Python 3.9 has\n   no pyserial",
    "the firmware keeps it within 16-240, so the panel never goes dark and adds nothing",
    "the map contains none of the freed blocks",
    "The Handler Returns Nothing",
    "Python has\r\nno pyserial.",
    "```foo`bar``` is inline code.\n\nIt has no effect.",
    "## The cache is no\nlonger valid.",
    "Otherwise nothing has no effect.",
    "It passes no `x` longer than 4 bytes.",
    "Use \\` to open. It returns nothing. Use \\` to close.",
)
KNOWN_GOOD = (
    "register only when the caller supplies an old-value buffer, and writes it",
    "bl_set() passes NULL for the old-value pointer, so a write costs one register exchange",
    "the hosted run boots the PICO board's image, which omits `backlight`",
    "target that skips an optional variant establishes execution only for the",
    "variants it ran.",
    "a pass means the subject is good only from a gate that rejects its known-bad input",
    "The cache is no longer valid after the write.",
    "The bound is no more than 240 and no later than the first tick.",
    'It prints "has no effect" and the `returns nothing` stub stays.',
    "With no board attached, the tool says so.",
    "## Not run\n\nNo board execution.",
    "## Not run\nNo board execution.",
    "| Runs | Notes |\n| No | board |",
    "The step runs.\n\n- None of the cases fails.",
    "The instruction is no-op and uses no-cache.",
    "During a dry-run no files are modified.",
    "It uses `x` no differently.",
    "With copy-on-write no pages are duplicated.",
)
FENCED = "Text.\n\n```\nit returns nothing\n```\nThe map holds none.\n"
# A fence line with an info string inside a block leaves the block open.
FENCED_INFO = "```text\n```python\nit returns nothing\n```\nThe map holds none.\n"
DOUBLE_SPAN = "The ``returns nothing`` stub and ``a `tick` has none`` stay code.\n"


def main():
    failures = []
    for line in KNOWN_BAD:
        if len(finder.phrases(line)) != 1:
            failures.append(f"known-bad missed or doubled: {line!r}: {finder.phrases(line)}")
    for line in KNOWN_GOOD:
        if finder.phrases(line):
            failures.append(f"known-good flagged: {line!r}: {finder.phrases(line)}")
    fenced = finder.phrases(FENCED, fenced=True)
    if [key for key, _ in fenced] != ["holds none"]:
        failures.append(f"fenced block not blanked: {fenced}")
    fenced = finder.phrases(FENCED_INFO, fenced=True)
    if [key for key, _ in fenced] != ["holds none"]:
        failures.append(f"fence line with an info string closed the block: {fenced}")
    if finder.phrases(DOUBLE_SPAN):
        failures.append(f"double-backtick span read as prose: {finder.phrases(DOUBLE_SPAN)}")
    before = "The board carries no V6 pack.\nIt prints nothing.\n"
    moved = "It prints nothing.\nMore text here.\nThe board carries no V6 pack.\n"
    if finder.excess(before, moved):
        failures.append("moved inherited phrases counted as new")
    prepended = "It prints nothing.\n" + before
    if finder.excess(before, prepended) != [("prints nothing", 1, [1, 3])]:
        failures.append(f"prepended copy miscounted: {finder.excess(before, prepended)}")
    crlf = before.replace("\n", "\r\n") + "A second copy prints nothing.\r\n"
    if finder.excess(before, crlf) != [("prints nothing", 1, [2, 3])]:
        failures.append(f"CRLF copy miscounted: {finder.excess(before, crlf)}")
    two = finder.phrases("A stub that does nothing returns nothing.")
    if [key for key, _ in two] != ["does nothing", "returns nothing"]:
        failures.append(f"a verb after a determiner was swallowed: {two}")
    if finder.excess("The handler returns nothing at all.\n", "The handler returns nothing.\n"):
        failures.append("rewording after an inherited phrase counted as new")
    for failure in failures:
        print(f"FAIL {failure}")
    if failures:
        print(f"determiner_negation_test: {len(failures)} failures")
        return 1
    print(
        f"determiner_negation_test: {len(KNOWN_BAD)} known-bad and {len(KNOWN_GOOD)} "
        "known-good lines sorted, fences blanked, multiset difference holds"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
