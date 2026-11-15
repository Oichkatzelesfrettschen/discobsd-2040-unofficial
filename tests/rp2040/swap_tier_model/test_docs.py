"""Document-derived oracles (README.md, "Oracles").

Each expected value is read from the document that states it, and the code
or the model is compared with it. A disagreement between a document and the
code is a finding: the test reports it through pytest.xfail with both
statements, and neither file is edited to match.
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


def define(path, name):
    match = re.search(rf"#define\s+{name}\s+\(?(\d+)", text(path))
    if match is None:
        raise AssertionError(f"{path.name}: no #define {name}")
    return int(match.group(1))


def test_endurance_and_erase_time():
    found = find(TRAFFIC, r"minimum ([\d,]+) program-erase cycles per sector")
    assert int(found.group(1).replace(",", "")) == variants.ENDURANCE
    erase = find(TRAFFIC, r"4 KB sector erase (\d+) ms typical")
    header = find(TOP / "sys" / "arch" / "rp2040" / "include" / "swapram.h",
                  r"a (\d+) ms sector erase")
    assert erase.group(1) == header.group(1)


def test_pool_size():
    doc = find(ZSWAP, r"`compile/PICO/Config` enables a (\d+) KiB pool")
    config = find(TOP / "sys" / "arch" / "rp2040" / "compile" / "PICO" /
                  "Config", r'"SWAPRAM_KB=(\d+)"')
    traffic = find(TRAFFIC, r"The pool \(`SWAPRAM_KB=(\d+)`\)")
    assert int(doc.group(1)) == int(config.group(1)) == \
        int(traffic.group(1)) == model.Config().pool_kb


def test_codec_window_and_lookahead():
    doc = find(ZSWAP, r"Window (\d+) and lookahead (\d+) come out of the sweep")
    cfg = TOP / "sys" / "arch" / "rp2040" / "heatshrink" / \
        "heatshrink_config.h"
    assert int(doc.group(1)) == define(cfg, "HEATSHRINK_STATIC_WINDOW_BITS")
    assert int(doc.group(2)) == define(cfg, "HEATSHRINK_STATIC_LOOKAHEAD_BITS")


def test_u_area_reservation():
    doc = find(TRAFFIC, r"`SR_WORST\(USIZE\)`, ([\d,]+) bytes held")
    usize = define(TOP / "sys" / "arch" / "rp2040" / "include" /
                   "machparam.h", "USIZE")
    assert int(doc.group(1).replace(",", "")) == model.sr_worst(usize)


def test_flash_layout():
    rows = {m.group(1): (int(m.group(2), 16), int(m.group(3)))
            for m in re.finditer(r"\| ([^|]+?) \| (0x[0-9a-f]+) \| (\d+) KB \|",
                                 text(STORAGE))}
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


def test_pool_placement():
    """zswap.md places the pool at the start of kernel RAM, above the user
    window; kern.ldscript places it after ordinary bss, as STORAGE.md and
    swapram.c say."""
    zswap = find(ZSWAP, r"places the pool in the NOLOAD `\.swapram_pool` "
                        r"section at ([^.]+)\.")
    script = find(TOP / "sys" / "arch" / "rp2040" / "conf" /
                  "kern.ldscript", r"The compressed swap pool follows "
                                   r"ordinary kernel data and bss")
    storage = find(STORAGE, r"pool is kernel memory after ordinary data "
                            r"and BSS")
    assert script and storage
    if "start of the kernel RAM region" in zswap.group(1):
        pytest.xfail("finding: zswap.md puts the pool at " +
                     repr(zswap.group(1)) + "; kern.ldscript and "
                     "STORAGE.md put it after ordinary data and bss")
