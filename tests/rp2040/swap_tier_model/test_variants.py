"""Checks on the variant model itself, over the calibrated trace."""

from dataclasses import replace

from swap_tier_model import calibrate, model, trace, variants

TRACE = trace.generate(calibrate.FIXED)
TIER_COUNTERS = ("swapram_attempts", "swapram_admissions",
                 "swapram_refused_fragment", "swapram_refused_nospace",
                 "swapram_admitted_data_bytes", "swapram_admitted_u_bytes")


def run(config, records=TRACE, cursor=0):
    return model.Model(config, cursor).replay(records)


def test_swap_off_nor_changes_nothing_but_erases():
    """Variant 8: with swap on PSRAM or SD the NOR erases vanish and every
    pool decision stays as it was, because admission never reads the
    flash path's state."""
    base = run(model.Config()).delta()[0]
    off = run(model.Config(flash=False)).delta()[0]
    assert off.get("swap_erase_sectors", 0) == 0
    for name in TIER_COUNTERS:
        assert off.get(name, 0) == base.get(name, 0), name


def test_pool_size_only_adds_admissions():
    counts = [run(model.Config(pool_kb=kb)).delta()[0]["swapram_admissions"]
              for kb in (16, 24, 32)]
    assert counts == sorted(counts)


def test_compaction_never_admits_less():
    base = run(model.Config()).delta()[0]
    compact = run(model.Config(compaction=True))
    got = compact.delta()[0]
    assert got["swapram_admissions"] >= base["swapram_admissions"]
    assert got.get("compactions", 0) > 0


def test_cursor_rotates_placements():
    """swap_cursor_init() draws the boot cursor uniformly; on the ring the
    erase counts of two cursors are rotations with equal totals."""
    a = run(model.Config(), cursor=0).delta()[1]
    b = run(model.Config(), cursor=10).delta()[1]
    assert sum(a) == sum(b)
    assert a[1:] != b[1:]


def test_smallest_flash_that_never_refuses():
    """Under the compile trace the largest live set of flash images needs
    fewer than 124 blocks: 128 KB never refuses and 96 KB does."""
    for kb, refuses in ((128, False), (96, True)):
        rows = variants.measure(model.Config(flash_kb=kb), sessions=4)
        assert all((r["refusing"] > 0) == refuses for r in rows), kb


def test_flash_refusal_stops_the_replay():
    """ENOMEM leaves both images where they were (sched() keeps the
    outgoing process resident, newproc() creates no child), so the replay
    stops at the refused record and reports no completed workload."""
    m = run(model.Config(flash_kb=64))
    r = m.refused
    assert r is not None and r["op"] == "out"
    assert r["pid"] not in m.where
    if r.get("incoming") is not None:
        assert r["incoming"] in m.where
    try:
        m.delta()
    except model.Refused:
        pass
    else:
        raise AssertionError("a refused replay reported a delta")


def test_refusal_share_does_not_scale_with_sessions():
    for kb in (384, 96, 64):
        one = [r["refusing"] for r in variants.measure(
            model.Config(flash_kb=kb), sessions=1)]
        four = [r["refusing"] for r in variants.measure(
            model.Config(flash_kb=kb), sessions=4)]
        assert one == four, kb


def test_waiting_driver_removed():
    w = replace(calibrate.FIXED, driver_swaps=False)
    got = run(model.Config(), trace.generate(w)).delta()[0]
    assert got.get("swapram_flash_span_06", 0) == 0


def test_zero_elision_skips_the_named_sector():
    """An all-zero sector in the middle of an image is neither erased nor
    programmed; the sectors around it are, and the NOR writer agrees with
    the count sector by sector."""
    image = {"op": "out", "pid": 5, "name": "x", "fork": True,
             "incoming": None, "data": 18 * 1024, "stack": 1024,
             "u": 3072, "cdata": 900, "cstack": 100, "cu": 900,
             "zero_sectors": [1]}
    records = [{"op": "meta", "schema": trace.SCHEMA},
               {"op": "mark", "what": "snapshot"}, image,
               {"op": "mark", "what": "snapshot"}]
    for i, r in enumerate(records):
        r.update(seq=i, phase="prefix" if i < 2 else "measure",
                 compile=None)
    trace.validate(records)
    m = model.Model(model.Config(pool_kb=0, zero_elision=True), 0,
                    nor=True).replay(records)
    counters, erases = m.delta()
    # The image spans blocks 4..25: sectors 1 to 6, of which sector 2 is
    # the image's sector 1.
    assert counters["swap_erase_sectors"] == 5
    assert [s for s, n in enumerate(erases) if n] == [1, 3, 4, 5, 6]
    assert m.nor.erases == m.erases
    assert not m.nor.violations
