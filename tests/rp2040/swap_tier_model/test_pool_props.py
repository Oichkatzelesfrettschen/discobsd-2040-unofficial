"""Stateful properties of the shipped swapram_pool.c.

The reference keeps the set of allocated grains. The pool's free list must
be exactly the complement of that set as maximal runs (no overlap, no leak,
coalesced), free plus allocated bytes must equal the pool, and alloc must
return the first run that fits (first fit, swapram_pool.c). A free the pool
refuses for want of a segment slot discards the range, as the file's header
comment states, so the reference keeps such a range allocated.
"""

import hypothesis.strategies as st
from hypothesis.stateful import Bundle, RuleBasedStateMachine, consumes, invariant, rule

from swap_tier_model.engine import GRAIN, Pool, roundgrain


def runs_of(size, taken):
    """Maximal free runs of [0, size) outside the grain set taken."""
    runs, start = [], None
    for g in range(0, size, GRAIN):
        free = g not in taken
        if free and start is None:
            start = g
        if not free and start is not None:
            runs.append((start, g - start))
            start = None
    if start is not None:
        runs.append((start, size - start))
    return runs


class PoolMachine(RuleBasedStateMachine):
    allocations = Bundle("allocations")

    def __init__(self):
        super().__init__()
        self.size = 0
        self.pool = None
        self.taken = set()
        self.owned = {}
        self.serial = 0

    @rule(target=allocations, request=st.integers(1, 6000))
    def alloc(self, request):
        self._ensure()
        runs = runs_of(self.size, self.taken)
        want = roundgrain(request)
        fit = [off for off, n in runs if n >= want]
        off = self.pool.alloc(request)
        if not fit or want > self.size:
            assert off is None, (request, runs)
            return None
        assert off == fit[0], ("first fit", request, off, fit[0])
        grains = set(range(off, off + want, GRAIN))
        assert not grains & self.taken, "overlap"
        self.taken |= grains
        self.serial += 1
        self.pool.write(off, bytes([self.serial & 0xff]) * want)
        self.owned[self.serial] = (off, request)
        return self.serial

    @rule(handle=consumes(allocations))
    def free(self, handle):
        if handle is None or handle not in self.owned:
            return
        off, size = self.owned.pop(handle)
        before = self.pool.segments()
        if self.pool.free(off, size):
            self.taken -= set(range(off, off + roundgrain(size), GRAIN))
        else:
            # Only a full segment table may refuse, and only for a range
            # that touches zero free neighbors.
            assert len(before) == self.pool.nseg, ("refused free", before)
            assert self.pool.segments() == before

    @rule(handle=allocations, data=st.data())
    def trim(self, handle, data):
        if handle is None or handle not in self.owned:
            return
        off, size = self.owned[handle]
        new = data.draw(st.integers(1, size))
        if self.pool.trim(off, size, new):
            keep, had = roundgrain(new), roundgrain(size)
            self.taken -= set(range(off + keep, off + had, GRAIN))
            self.owned[handle] = (off, new)
        else:
            assert len(self.pool.segments()) == self.pool.nseg

    @rule(data=st.data())
    def free_overlapping_free_space(self, data):
        """A range that reaches any free grain is a double free or a stray
        offset: the pool must refuse it and stay unchanged."""
        self._ensure()
        runs = runs_of(self.size, self.taken)
        if not runs:
            return
        run_off, run_len = data.draw(st.sampled_from(runs))
        # Anchor on a free grain; the run's edges are where an off-by-one
        # in the overlap checks would let a partial overlap through.
        inside = data.draw(st.one_of(
            st.just(0), st.just(run_len - GRAIN),
            st.integers(0, run_len // GRAIN - 1).map(lambda g: g * GRAIN)))
        grain = run_off + inside
        start = data.draw(st.one_of(
            st.just(grain), st.integers(0, grain // GRAIN).map(
                lambda g: g * GRAIN)))
        end = data.draw(st.one_of(
            st.just(grain + GRAIN), st.integers(grain // GRAIN + 1,
                                                self.size // GRAIN).map(
                lambda g: g * GRAIN)))
        before = self.pool.segments()
        assert not self.pool.free(start, end - start), (start, end, before)
        assert self.pool.segments() == before

    @rule(size=st.sampled_from([512, 4096, 16384]), nseg=st.integers(2, 17))
    def reset(self, size, nseg):
        self.pool = Pool(size, nseg)
        self.size = size
        self.taken = set()
        self.owned = {}

    def _ensure(self):
        if self.pool is None:
            self.reset(16384, 17)

    @invariant()
    def free_list_is_complement(self):
        if self.pool is None:
            return
        assert self.pool.segments() == runs_of(self.size, self.taken)
        assert self.pool.avail() + len(self.taken) * GRAIN == self.size
        largest = max((n for _o, n in runs_of(self.size, self.taken)),
                      default=0)
        assert self.pool.largest() == largest

    @invariant()
    def contents_untouched(self):
        for handle, (off, size) in self.owned.items():
            assert self.pool.read(off, roundgrain(size)) == \
                bytes([handle & 0xff]) * roundgrain(size)


TestPool = PoolMachine.TestCase


def test_alloc_takes_a_run_of_exactly_its_size():
    """A request equal to a free run's size takes that run, in a hole and in
    a pool that is one run.

    The stateful search reaches an exact fit only when a random request
    equals a free run's size, which depends on the hypothesis version's
    example stream; these cases hold the boundary of the ``s_size < want``
    test in swapram_pool_alloc() without it."""
    pool = Pool(64, 17)
    first = pool.alloc(16)
    hole = pool.alloc(8)
    last = pool.alloc(16)
    assert (first, hole, last) == (0, 16, 24), (first, hole, last)
    assert pool.free(hole, 8)
    assert pool.segments() == [(16, 8), (40, 24)], pool.segments()
    assert pool.alloc(8) == 16, "a hole of exactly 8 bytes holds an 8 byte request"
    assert pool.segments() == [(40, 24)], pool.segments()
    assert pool.alloc(24) == 40, "the last run holds a request of its size"
    assert pool.segments() == [] and pool.avail() == 0

    whole = Pool(64, 17)
    assert whole.alloc(64) == 0, "an empty pool holds one request of its size"
    assert whole.segments() == [] and whole.avail() == 0
    assert whole.alloc(1) is None


class CompactionMachine(RuleBasedStateMachine):
    """Pool compaction (a model, not shipped code) keeps every live
    allocation's bytes and address order and leaves one free run."""

    def __init__(self):
        super().__init__()
        self.pool = Pool(16384, 17)
        self.live = {}
        self.serial = 0

    @rule(request=st.integers(1, 3000))
    def alloc(self, request):
        off = self.pool.alloc(request)
        if off is None:
            return
        self.serial += 1
        payload = bytes((self.serial * 7 + i) & 0xff
                        for i in range(roundgrain(request)))
        self.pool.write(off, payload)
        self.live[self.serial] = (off, request, payload)

    @rule(data=st.data())
    def free(self, data):
        if not self.live:
            return
        key = data.draw(st.sampled_from(sorted(self.live)))
        off, size, _payload = self.live.pop(key)
        assert self.pool.free(off, size)

    @rule()
    def compact(self):
        before = sorted(self.live, key=lambda k: self.live[k][0])
        moved = self.pool.compact({k: (v[0], v[1])
                                   for k, v in self.live.items()})
        for key, new in moved.items():
            _old, size, payload = self.live[key]
            assert self.pool.read(new, len(payload)) == payload
            self.live[key] = (new, size, payload)
        assert sorted(self.live, key=lambda k: self.live[k][0]) == before
        used = sum(roundgrain(v[1]) for v in self.live.values())
        assert self.pool.segments() == ([(used, 16384 - used)]
                                        if used < 16384 else [])


TestCompaction = CompactionMachine.TestCase
