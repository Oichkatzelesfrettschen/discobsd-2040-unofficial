#!/usr/bin/env python3
"""Hold the Renode boot log to a fixed set of warnings.

Renode logs a warning wherever the RP2040 models fall short of the silicon:
an unimplemented register, a bus address no peripheral claims, a flash
opcode the W25QXX model does not decode. Most of them are inert for this
kernel, and a few mark real infidelity, so the gate cannot simply forbid
warnings. It forbids surprises instead.

warning-classes.txt names every class the boot is known to produce, with
the ceiling on how many times it may appear. A warning matching no class
fails the gate, and so does a class that appears more often than its
ceiling. Ceilings ratchet down as the models improve; raising one records
a regression in the file that carries it.

Counting needs the volatile parts of a message gone, so the addresses,
values and program counters Renode prints become a single token before a
line is matched or tallied.

Usage: check-warnings.py CLASSES LOG
"""

import re
import sys

WARNING = re.compile(r"\[WARNING\]\s+(.*?)\s*$")
PROGRAM_COUNTER = re.compile(r"\[cpu\d+: 0x[0-9A-Fa-f]+\]\s*")
REPEAT_COUNT = re.compile(r"\s*\(\d+\)\s*$")
HEX = re.compile(r"0x[0-9A-Fa-f]+")


# Phrases after which a hex literal names the class rather than carrying a
# payload: the bus address of an unmapped register, and the decoded operation
# W25QXX failed to handle, where 0x7 (a status read the model never serves)
# and 0x0 (a byte after an opcode it did not recognize) are separate defects.
IDENTIFYING = ("at ", "byte: ")


def normalize(text):
    """Drop what varies run to run and keep what names the class.

    Every hex number is a value, a program counter or a payload and becomes
    one token, except where IDENTIFYING says the literal is the identity.
    """
    text = REPEAT_COUNT.sub("", PROGRAM_COUNTER.sub("", text))
    pieces = []
    end = 0
    for found in HEX.finditer(text):
        before = text[:found.start()]
        identifies = any(before.endswith(phrase) for phrase in IDENTIFYING)
        pieces.append(text[end:found.start()])
        pieces.append(found.group(0) if identifies else "0xN")
        end = found.end()
    pieces.append(text[end:])
    return "".join(pieces).strip()


def read_classes(path):
    """Return [(ceiling, compiled pattern, source line number)] in file order."""
    classes = []
    with open(path, encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.rstrip()
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            fields = line.split(None, 1)
            if len(fields) != 2:
                raise SystemExit(f"{path}:{number}: want 'ceiling pattern'")
            ceiling, pattern = fields
            if not ceiling.isdigit():
                raise SystemExit(f"{path}:{number}: ceiling '{ceiling}' is not a count")
            classes.append((int(ceiling), re.compile(pattern), number))
    if not classes:
        raise SystemExit(f"{path}: no warning classes")
    return classes


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__.strip().splitlines()[-1])
    classes = read_classes(argv[1])
    counts = [0] * len(classes)
    unmatched = {}

    with open(argv[2], encoding="utf-8", errors="replace") as handle:
        for line in handle:
            found = WARNING.search(line)
            if not found:
                continue
            text = normalize(found.group(1))
            for index, (_, pattern, _) in enumerate(classes):
                if pattern.search(text):
                    counts[index] += 1
                    break
            else:
                unmatched[text] = unmatched.get(text, 0) + 1

    failures = []
    for index, (ceiling, pattern, number) in enumerate(classes):
        if counts[index] > ceiling:
            failures.append(
                f"warning-classes.txt:{number}: {pattern.pattern} "
                f"appeared {counts[index]} times, ceiling {ceiling}"
            )
    for text, count in sorted(unmatched.items(), key=lambda item: -item[1]):
        failures.append(f"unclassified warning ({count}x): {text}")

    total = sum(counts) + sum(unmatched.values())
    print(f"check-warnings: {total} warnings, {len(classes)} classes")
    for index, (ceiling, pattern, _) in enumerate(classes):
        print(f"  {counts[index]:6d} / {ceiling:6d}  {pattern.pattern}")

    if failures:
        for failure in failures:
            print(f"check-warnings: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
