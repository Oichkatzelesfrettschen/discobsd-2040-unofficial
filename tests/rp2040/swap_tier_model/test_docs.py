"""Document-derived oracles (README.md, "Oracles").

Each expected value is read from the document that states it, and the code
or the model is compared with it. A disagreement between a document and the
code is a finding: the test fails with both statements.
"""

import re
from pathlib import Path

import pytest

from swap_tier_model import model, variants

TOP = Path(__file__).resolve().parents[3]
DOC = TOP / "sys" / "arch" / "rp2040" / "doc"
TRAFFIC = DOC / "research" / "flash-traffic-per-workload.md"
ZSWAP = DOC / "research" / "zswap.md"
STORAGE = DOC / "STORAGE.md"


def text(path):
    return path.read_text(encoding="utf-8")


def find(path, pattern):
    """The first match of pattern in path, whitespace-normalized."""
    body = re.sub(r"\s+", " ", text(path))
    match = re.search(pattern, body)
    if match is None:
        raise AssertionError(f"{path.name}: no match for {pattern!r}")
    return match


def decide(condition, defined, numbers):
    """True or False for a preprocessor condition made of numbers, macros the
    file has given a number, defined(NAME) and the operators !, && and ||;
    None for any other condition."""
    alternatives = []
    for alternative in condition.split("||"):
        terms = []
        for term in alternative.split("&&"):
            term = term.strip()
            negated = term.startswith("!")
            if negated:
                term = term[1:].strip()
            operand = re.fullmatch(r"defined\s*\(\s*(\w+)\s*\)|defined\s+(\w+)", term)
            if operand is not None:
                truth = (operand.group(1) or operand.group(2)) in defined
            elif term.isdigit() or term in numbers:
                truth = int(numbers.get(term, term)) != 0
            else:
                return None
            terms.append(truth != negated)
        alternatives.append(all(terms))
    return any(alternatives)


def define(path, name, defined=frozenset()):
    """The numeric value of NAME in the arm a build with only DEFINED set
    compiles. #ifdef, #ifndef, #else and #endif are followed, and so is an
    #if or #elif that decide() can settle; any other condition around a
    definition of NAME is an error, so the reading never guesses which arm a
    kernel takes."""
    stack = []  # (taking, any arm taken, decidable) per open conditional
    numbers = {}  # macros given a number in a live arm so far
    value = None
    for line in text(path).splitlines():
        directive = re.match(r"\s*#\s*(\w+)\s*(\w*)", line)
        if directive is None:
            continue
        word, argument = directive.groups()
        live = all(taking for taking, _, _ in stack)
        if word in ("ifdef", "ifndef"):
            taking = (argument in defined) == (word == "ifdef")
            stack.append((taking, taking, True))
        elif word == "if":
            condition = line.split("if", 1)[1].split("/*", 1)[0].strip()
            taking = decide(condition, defined, numbers)
            if taking is None:
                stack.append((False, False, False))
            else:
                stack.append((taking, taking, True))
        elif word == "elif":
            _, taken, decidable = stack.pop()
            condition = line.split("elif", 1)[1].split("/*", 1)[0].strip()
            holds = decide(condition, defined, numbers)
            if not decidable or holds is None:
                stack.append((False, True, False))
            else:
                stack.append((holds and not taken, taken or holds, True))
        elif word == "else":
            _, taken, decidable = stack.pop()
            stack.append((not taken, True, decidable))
        elif word == "endif":
            stack.pop()
        elif word == "define" and live:
            number = re.match(rf"\s*#\s*define\s+{argument}\s+\(?(\d+)", line)
            if number is not None:
                numbers[argument] = int(number.group(1))
        if word == "define" and argument == name:
            if not all(decidable for _, _, decidable in stack):
                raise AssertionError(f"{path.name}: #define {name} under an #if")
            match = re.match(rf"\s*#\s*define\s+{name}\s+\(?(\d+)", line)
            if live and match is not None:
                value = int(match.group(1))
    if value is None:
        raise AssertionError(f"{path.name}: no #define {name}")
    return value


def test_define_follows_the_configured_arm():
    flash_h = TOP / "sys" / "arch" / "rp2040" / "dev" / "flash.h"
    assert define(flash_h, "FLASH_FS_OFFSET") == 128
    assert define(flash_h, "FLASH_FS_OFFSET", {"PICOCALC_SD"}) == 160
    assert define(flash_h, "FLASH_FS_OFFSET", {"EXEC_XIP_LIBC"}) == 176


def test_define_refuses_an_undecidable_arm(tmp_path):
    header = tmp_path / "arms.h"
    header.write_text("#if LIMIT > 3\n#define VALUE 1\n#else\n#define VALUE 2\n#endif\n")
    with pytest.raises(AssertionError, match="under an #if"):
        define(header, "VALUE")
    header.write_text("#ifdef A\n#define VALUE 1\n#elif LIMIT > 3\n#define VALUE 2\n#endif\n")
    with pytest.raises(AssertionError, match="under an #if"):
        define(header, "VALUE")
    header.write_text(
        "#if defined(A) && !defined(B)\n#define VALUE 1\n#elif defined(B) || defined(C)\n"
        "#define VALUE 2\n#else\n#define VALUE 3\n#endif\n"
    )
    assert define(header, "VALUE", {"A"}) == 1
    assert define(header, "VALUE", {"A", "B"}) == 2
    assert define(header, "VALUE", {"C"}) == 2
    assert define(header, "VALUE") == 3


def test_endurance_and_erase_time():
    found = find(TRAFFIC, r"minimum ([\d,]+) program-erase cycles per sector")
    assert int(found.group(1).replace(",", "")) == variants.ENDURANCE
    erase = find(TRAFFIC, r"4 KB sector erase (\d+) ms typical")
    header = find(
        TOP / "sys" / "arch" / "rp2040" / "include" / "swapram.h", r"a (\d+) ms sector erase"
    )
    assert erase.group(1) == header.group(1)


def test_pool_size():
    doc = find(ZSWAP, r"`compile/PICO/Config` enables a (\d+) KiB pool")
    config = find(
        TOP / "sys" / "arch" / "rp2040" / "compile" / "PICO" / "Config", r'"SWAPRAM_KB=(\d+)"'
    )
    traffic = find(TRAFFIC, r"The pool \(`SWAPRAM_KB=(\d+)`\)")
    assert (
        int(doc.group(1)) == int(config.group(1)) == int(traffic.group(1)) == model.Config().pool_kb
    )


def test_codec_window_and_lookahead():
    doc = find(ZSWAP, r"Window (\d+) and lookahead (\d+) come out of the sweep")
    cfg = TOP / "sys" / "arch" / "rp2040" / "heatshrink" / "heatshrink_config.h"
    assert int(doc.group(1)) == define(cfg, "HEATSHRINK_STATIC_WINDOW_BITS")
    assert int(doc.group(2)) == define(cfg, "HEATSHRINK_STATIC_LOOKAHEAD_BITS")


def test_u_area_reservation():
    doc = find(TRAFFIC, r"`SR_WORST\(USIZE\)`, ([\d,]+) bytes held")
    usize = define(TOP / "sys" / "arch" / "rp2040" / "include" / "machparam.h", "USIZE")
    assert int(doc.group(1).replace(",", "")) == model.sr_worst(usize)


def test_flash_layout():
    rows = {
        m.group(1): (int(m.group(2), 16), int(m.group(3)))
        for m in re.finditer(r"\| ([^|]+?) \| (0x[0-9a-f]+) \| (\d+) KB \|", text(STORAGE))
    }
    kernel = rows["boot2 and kernel"]
    root = rows["root, Dhara journal"]
    swap = rows["swap, raw"]
    flash_h = TOP / "sys" / "arch" / "rp2040" / "dev" / "flash.h"
    assert kernel[1] * 1024 == define(flash_h, "FLASH_FS_OFFSET") * 1024
    assert root[0] == 0x10000000 + kernel[1] * 1024
    assert swap[0] == root[0] + root[1] * 1024
    assert swap[1] == define(flash_h, "FLASH_SWAP_BYTES")
    assert swap[1] == model.Config().flash_kb
    assert swap[0] - 0x10000000 == model.FLASH_SWAP_OFFSET
    assert kernel[1] + root[1] + swap[1] == 2048


AFTER_BSS = (r"after ordinary data and BSS", r"follows ordinary kernel data and bss")


def output_sections(script):
    """Output section names of a linker script in the order they appear,
    with comments removed so a comment cannot stand in for a statement."""
    body = re.sub(r"/\*.*?\*/", " ", script, flags=re.S)
    return re.findall(r"^\s*(\.[A-Za-z_][\w.]*)\s*(?:\([A-Z]+\))?\s*:", body, flags=re.M)


def placement_verdict(script, doc):
    """'agrees' when the script places .swapram_pool directly after .bss
    and doc says so; anything else raises."""
    sections = output_sections(script)
    if (
        ".bss" not in sections
        or ".swapram_pool" not in sections
        or sections.index(".swapram_pool") != sections.index(".bss") + 1
    ):
        raise AssertionError(f"pool not directly after .bss: {sections}")
    flat = re.sub(r"\s+", " ", doc)
    if any(re.search(p, flat, flags=re.I) for p in AFTER_BSS):
        return "agrees"
    raise AssertionError("document states no pool placement the script supports")


SCRIPT = TOP / "sys" / "arch" / "rp2040" / "conf" / "kern.ldscript"


def test_placement_verdict_calibration():
    good = ".bss : { *(.bss*) } > RAM\n.swapram_pool (NOLOAD) : { } > RAM\n"
    before = ".swapram_pool (NOLOAD) : { } > RAM\n.bss : { } > RAM\n"
    commented = "/* .bss : */\n.swapram_pool (NOLOAD) : { } > RAM\n"
    assert placement_verdict(good, "after ordinary data and BSS") == "agrees"
    for script in (before, commented):
        with pytest.raises(AssertionError):
            placement_verdict(script, "after ordinary data and BSS")
    with pytest.raises(AssertionError):
        placement_verdict(good, "The pool lives somewhere in RAM.")


def test_storage_states_the_linked_placement():
    assert placement_verdict(text(SCRIPT), text(STORAGE)) == "agrees"


def test_pool_placement():
    """zswap.md states the placement kern.ldscript links: .swapram_pool
    directly after .bss."""
    assert placement_verdict(text(SCRIPT), text(ZSWAP)) == "agrees"
