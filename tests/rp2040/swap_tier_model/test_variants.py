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
        assert (sum(r["refusals"] for r in rows) > 0) == refuses, kb


def test_waiting_driver_removed():
    w = replace(calibrate.FIXED, driver_swaps=False)
    got = run(model.Config(), trace.generate(w)).delta()[0]
    assert got.get("swapram_flash_span_06", 0) == 0
