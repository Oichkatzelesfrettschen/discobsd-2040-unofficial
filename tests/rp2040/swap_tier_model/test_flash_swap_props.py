"""Properties of the shipped flash_swap.c against a NOR model.

The model (engine.Nor) rejects 0-to-1 programs, unaligned or partial-page
programs and unaligned erases. Images are written the way swap_image_copy_out
writes them: data, stack and u area as ascending pieces, each starting at a
1 KiB block boundary of an erase-aligned extent. Rewrites take arbitrary
page-aligned ranges through the spare sector, as fl_raw() does for
/dev/tempN.
"""

import hypothesis.strategies as st
from hypothesis import given, settings
from hypothesis.stateful import RuleBasedStateMachine, invariant, rule

from swap_tier_model.engine import BLOCK, PAGE, SECTOR, FlashSwapDriver, Nor

BASE = 0x101A0000
SECTORS = 8
SPARE = BASE  # the first sector stages rewrites


def blocks(n):
    return (n + BLOCK - 1) // BLOCK


class FlashMachine(RuleBasedStateMachine):
    def __init__(self):
        super().__init__()
        self.nor = Nor(BASE, SECTORS * SECTOR)
        self.driver = FlashSwapDriver(self.nor)
        # A shadow of the bytes the writer must have left, and a mask that
        # holds 0xff where the shadow's byte is known; the invariant
        # compares them as integers, one AND per check.
        self.shadow = bytearray(SECTORS * SECTOR)
        self.known = bytearray(SECTORS * SECTOR)

    def _flash(self):
        return bytes(self.nor.data)

    @rule(
        data=st.data(),
        lengths=st.lists(st.integers(0, 5000), min_size=1, max_size=3),
        seed=st.integers(0, 255),
    )
    def append_image(self, data, lengths, seed):
        span = sum(blocks(n) for n in lengths)
        count = max(1, (span * BLOCK + SECTOR - 1) // SECTOR)
        if count > SECTORS - 1:
            return
        first = data.draw(st.integers(1, SECTORS - count))
        erases = list(self.nor.erases)
        offset = BASE + first * SECTOR
        written = 0
        pieces = []
        for i, n in enumerate(lengths):
            payload = bytes((seed + i * 31 + k * 7) & 0xFF for k in range(n))
            if n:
                assert self.driver.append(offset, payload) == 0, self.nor.violations
                pieces.append((offset - BASE, payload))
                written = offset + n - BASE
            offset += blocks(n) * BLOCK
        assert not self.nor.violations
        # Exactly the sectors the image's bytes reach were erased, once each.
        reached = range(first, (written + SECTOR - 1) // SECTOR) if written else range(0)
        for s in range(SECTORS):
            delta = self.nor.erases[s] - erases[s]
            assert delta == (1 if s in reached else 0), (s, delta, reached)
        # An erased sector forgets its old bytes; the image's pieces are known.
        for s in reached:
            self.known[s * SECTOR : (s + 1) * SECTOR] = bytes(SECTOR)
        for rel, payload in pieces:
            self.shadow[rel : rel + len(payload)] = payload
            self.known[rel : rel + len(payload)] = b"\xff" * len(payload)

    @rule(data=st.data(), seed=st.integers(0, 255))
    def rewrite(self, data, seed):
        start_page = data.draw(st.integers(SECTOR // PAGE, SECTORS * SECTOR // PAGE - 1))
        pages = data.draw(st.integers(1, min(40, SECTORS * SECTOR // PAGE - start_page)))
        offset = BASE + start_page * PAGE
        payload = bytes((seed + k * 13) & 0xFF for k in range(pages * PAGE))
        before = self._flash()
        assert self.driver.rewrite(offset, payload, SPARE) == 0, self.nor.violations
        assert not self.nor.violations
        after = self._flash()
        lo, hi = offset - BASE, offset - BASE + len(payload)
        assert after[lo:hi] == payload
        # Every other byte outside the spare sector keeps its value.
        assert after[SECTOR:lo] == before[SECTOR:lo]
        assert after[hi:] == before[hi:]
        self.shadow[lo:hi] = payload
        self.known[lo:hi] = b"\xff" * len(payload)

    @invariant()
    def bytes_hold(self):
        mask = int.from_bytes(self.known, "big")
        flash = int.from_bytes(self._flash(), "big")
        assert flash & mask == int.from_bytes(self.shadow, "big") & mask


TestFlash = FlashMachine.TestCase
TestFlash.settings = settings(max_examples=60, stateful_step_count=25)


@given(st.integers(1, 255).map(lambda n: n * 4))
def test_append_refuses_unaligned_start(skew):
    nor = Nor(BASE, SECTORS * SECTOR)
    driver = FlashSwapDriver(nor)
    if skew % PAGE:
        assert driver.append(BASE + SECTOR + skew, b"x" * 10) == -1
        assert nor.erases == [0] * SECTORS


def test_rewrite_refuses_spare_destination():
    nor = Nor(BASE, SECTORS * SECTOR)
    driver = FlashSwapDriver(nor)
    assert driver.rewrite(SPARE, b"\0" * PAGE, SPARE) == -1
