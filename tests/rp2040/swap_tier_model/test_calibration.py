"""Calibration of the compile trace against the retained board captures.

The targets come from docs/research/trim-survey/board/swap/admission
through its delta.py at run time. KNOWN_MISSES records the fitted counters
that no trace model reached (README.md, "Calibration"); the gate fails if
that set changes in either direction, so an improvement is noticed too.
"""

import pytest

from swap_tier_model import calibrate, model, trace

KNOWN_MISSES = {"swapram_short_6k"}


def misses(rows):
    return {r[0] for r in rows if not r[-1]}


def test_capture_counters():
    t = calibrate.targets()
    assert (t["swapram_attempts"], t["swapram_admissions"]) == (305, 63)
    assert (t["swapram_refused_fragment"], t["swapram_refused_nospace"]) == \
        (196, 46)


@pytest.mark.parametrize("name", ["fixed-sh.txt", "main-sh.txt"])
def test_board_identities(name):
    """Attempts are admissions plus the two flash paths, and the erases are
    the flash span bins weighted by span; the one image of 12 or more
    sectors is 15 sectors in both captures."""
    t = calibrate.targets(name)
    assert t["swapram_attempts"] == t["swapram_admissions"] + \
        t["swapram_flash_swapouts"] + t["swapram_exchange_swapouts"] + \
        t.get("swapram_sched_refusals", 0)
    spans = sum(int(k[-2:]) * v for k, v in t.items()
                if k.startswith("swapram_flash_span_") and k[-2:] != "12")
    assert t["swap_erase_sectors"] == spans + 15 * t["swapram_flash_span_12"]
    # Every refused image writes its raw data, stack and USIZE to flash.
    refused = t["swapram_refused_fragment"] + t["swapram_refused_nospace"]
    assert t["swap_write_bytes"] - t["swapram_refuse_scan_bytes"] == \
        refused * 3072


@pytest.mark.parametrize("index", range(len(calibrate.ENSEMBLE)))
def test_fixed_shell_calibrates(index):
    rows, _held, m = calibrate.evaluate(
        trace.generate(calibrate.ENSEMBLE[index]))
    assert misses(rows) == KNOWN_MISSES, calibrate.report(rows, _held)
    got, _ = m.delta()
    assert got["swapram_attempts"] == got["swapram_admissions"] + \
        got["swapram_flash_swapouts"] + got["swapram_exchange_swapouts"]


def test_shell_before_fix_out_of_sample():
    """main-sh.txt: only the two bin/sh classes change, from that
    capture's own byte sums; the fragment and nospace split (240/2 against
    196/46) and every bin follow without refitting."""
    rows, held, _m = calibrate.evaluate(trace.generate(calibrate.MAIN),
                                        name="main-sh.txt")
    assert misses(rows) == set(), calibrate.report(rows, held)


def test_trace_round_trip(tmp_path):
    records = trace.generate(calibrate.FIXED)
    trace.validate(records)
    path = tmp_path / "compile.jsonl"
    trace.dump(records, path)
    again = trace.load(path)
    first = model.run(records).delta()[0]
    assert model.run(again).delta()[0] == first


def test_schema_rejects_bad_records():
    records = trace.generate(calibrate.FIXED)
    bad = [dict(r) for r in records]
    for r in bad:
        if r["op"] == "out":
            r["u"] = 1024
            break
    with pytest.raises(ValueError):
        trace.validate(bad)


def test_nor_erases_match_span_arithmetic():
    """The shipped flash_swap_append() over the NOR model erases exactly
    the sectors the model's span arithmetic counts, sector by sector."""
    m = model.run(trace.generate(calibrate.FIXED), nor=True)
    assert not m.nor.violations
    assert m.nor.erases == m.erases
