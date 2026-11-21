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
"""Find a negative determiner or pronoun that follows a verb in prose.

The port's voice rule (AGENTS.md, Source comments; the agent reference's
mechanism voice) moves a clause's negation back onto the verb or replaces it
with the action that runs: "passes no old buffer" becomes "passes NULL for
the old-value pointer". The finder reports each "<verb> no|none|nothing"
from a closed verb list built from the forms measured in the tree; the verb
and determiner form the phrase key, so rewording around a kept phrase leaves
its key unchanged. Fixed quantity and time bounds ("no longer",
"no more than", "no later than") stay. Text inside backticks, double quotes
and fenced code blocks is blanked first, keeping offsets, so code, messages
and quoted upstream text stay out of reach.

The finder is lexical. It reads top-level fences, code spans and one-line
double quotations as code; a fence nested in a list or a quotation, a
quotation that wraps or escapes its quote marks, and a table row without a
leading pipe read as prose. A finding there names its line, and the author
rephrases or marks the text as code.

excess() compares two versions of one text as multisets of phrase keys and
reports each key the newer version holds more often, with every line that
holds it, so a gate over changed files flags new wording and leaves the
inherited wording a change moves or keeps to the sweep.
"""

import re
from collections import Counter

VERBS = frozenset(
    """
    accept accepts add adds adding added allow allows apply applies arm arms
    ask asks be been being boot boots booted build builds built buy buys call calls carry
    carries carrying change changes changed check checks claim claims clear
    clears consume consumes contain contains cost costs costing create
    creates define defines did do does doing draw draws drew emit emits enter
    enters establish establishes execute executes expect expects find finds
    found gave get gets give gives got had has have having hold holds import
    imports include includes is are was were keep keeps kept leave leaves
    leaving left list lists load loads make makes made map maps mark marks
    match matches mean means move moves name names need needs open opens own
    owns pass passes print prints printed produce produces produced provide
    provides raise raises reach reaches read reads record records report
    reports require requires return returns returned route routes run runs
    ran say says see sees select selects send sends sent set sets share
    shares ship ships show shows start starts store stores supply supplies
    take takes taking took touch touches type types use uses used wake wakes
    wait waits write writes wrote written
    """.split()
)
DETERMINERS = ("no", "none", "nothing")
# "no" before these names a bound or an idiom, not an absent operand.
BOUNDS = frozenset(("longer", "later", "earlier", "sooner", "more", "less", "fewer", "matter"))
# Verb and determiner sit in one block: blanks, or one line break that
# neither leaves a heading or table line nor starts a new block (a blank
# line, heading, list item, table row or quotation).
_BREAK = r"[ \t]*\n(?![ \t]*(?:\n|#|[-*+][ \t]|\d+[.)][ \t]|\||>))[ \t]*"
# The verb is a whole word (a hyphenated compound's tail is a noun part);
# the word after the determiner is read by lookahead only, so a verb that
# follows the determiner starts its own match.
PHRASE = re.compile(
    r"(?<![\w-])([A-Za-z]+)(?:[ \t]+|" + _BREAK + r")(no|none|nothing)(?![\w-])"
    r"(?=(?:[ \t]+|" + _BREAK + r")([A-Za-z][\w-]*))?",
    re.IGNORECASE,
)
_SINGLE_LINE_BLOCK = re.compile(r"^[ \t]*(?:#|\|)")
# A code span opens and closes on backtick runs of one length (CommonMark 6.1)
# and stays inside its paragraph.
# A backslash-escaped backtick is a literal (CommonMark 2.4) and opens no span.
CODE_SPAN = re.compile(r"(?<![`\\])(`+)(?!`)(?:(?!\n[ \t]*\n)[\s\S])*?(?<!`)\1(?!`)")
QUOTED = re.compile(r'"[^"\n]*"')
# A backtick fence's info string holds no backtick (CommonMark 4.5), so a
# line that opens with an inline code span stays prose.
FENCE = re.compile(r"^ {0,3}(`{3,}(?=[^`\n]*$)|~{3,})", re.M)
# A closing fence carries only blanks after its marker (CommonMark 4.5).
CLOSING_FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})[ \t]*$")


# Code and quotations are filled with a non-blank placeholder, so the words
# on their two sides stay apart and never form a phrase across them.
_FILL = "\x01"


def _blank(match):
    return re.sub(r"[^\n]", _FILL, match.group())


def blank_code(text, fenced=False):
    """text with code spans, quotations and, when fenced is set, Markdown
    fenced blocks replaced by spaces; newlines and offsets stay."""
    if fenced:
        out = []
        opener = None
        for line in text.splitlines(keepends=True):
            match = FENCE.match(line)
            if opener is None and match:
                opener = match.group(1)
                out.append(_blank(re.match(r"[\s\S]*", line)))
                continue
            if opener is not None:
                close = CLOSING_FENCE.match(line.rstrip("\r\n"))
                if close and close.group(1)[0] == opener[0] and len(close.group(1)) >= len(opener):
                    opener = None
                out.append(_blank(re.match(r"[\s\S]*", line)))
                continue
            out.append(line)
        text = "".join(out)
    text = CODE_SPAN.sub(_blank, text)
    return QUOTED.sub(_blank, text)


def normalize(text):
    """text with CRLF and lone CR line ends turned into LF."""
    return text.replace("\r\n", "\n").replace("\r", "\n")


def phrases(text, fenced=False):
    """(key, offset) for each determiner after a listed verb, in order;
    offsets index normalize(text)."""
    found = []
    blanked = blank_code(normalize(text), fenced)
    for match in PHRASE.finditer(blanked):
        verb, determiner, follower = match.group(1), match.group(2), match.group(3)
        if "\n" in blanked[match.start(1) : match.start(2)]:
            line_start = blanked.rfind("\n", 0, match.start(1)) + 1
            if _SINGLE_LINE_BLOCK.match(blanked, line_start):
                continue
        if follower and "\n" in blanked[match.end(2) : match.start(3)]:
            line_start = blanked.rfind("\n", 0, match.start(2)) + 1
            if _SINGLE_LINE_BLOCK.match(blanked, line_start):
                follower = None
        if verb.lower() not in VERBS:
            continue
        if determiner.lower() == "no" and follower and follower.lower() in BOUNDS:
            continue
        key = f"{verb.lower()} {determiner.lower()}"
        found.append((key, match.start(2)))
    return found


def main(argv=None):
    """List the phrases of each Markdown file named: the sweep worklist."""
    import argparse
    import sys

    parser = argparse.ArgumentParser(description=main.__doc__)
    parser.add_argument("paths", nargs="+")
    arguments = parser.parse_args(argv)
    total = 0
    for path in arguments.paths:
        with open(path, encoding="utf-8", errors="replace") as source:
            text = normalize(source.read())
        for key, offset in phrases(text, fenced=path.endswith(".md")):
            total += 1
            print(f"{path}:{text.count(chr(10), 0, offset) + 1}: {key}")
    print(f"{total} phrases in {len(arguments.paths)} files", file=sys.stderr)
    return 0


def excess(before, after, fenced=False):
    """[(key, extra, lines)] for each key after holds more often than before.

    The verdict is a whole-text count, so a phrase a change keeps, moves or
    rewords around cancels against itself wherever it lands. extra is how
    many more copies after holds; lines lists every line of after that holds
    the key, since which copy is the added one is a question of intent the
    counts cannot settle.
    """
    found = phrases(after, fenced)
    counts = Counter(key for key, _ in found)
    counts.subtract(key for key, _ in phrases(before, fenced))
    text = normalize(after)
    result = []
    for key in sorted(key for key, extra in counts.items() if extra > 0):
        lines = sorted({text.count("\n", 0, offset) + 1 for k, offset in found if k == key})
        result.append((key, counts[key], lines))
    return result


if __name__ == "__main__":
    raise SystemExit(main())
