"""Calibration for tools/mutant_pool.py, the pool the fourfall, puzzlebox
and calc mutant runners share.

ordered() must yield every item in list order, run the serial items after
the pool drains with no other check running, and honor MUTANT_JOBS. A
pool that also schedules the serial items concurrently -- the defect that
would let a timing mutant survive under load -- must fail these checks.
"""

import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutant_pool  # noqa: E402

failures = 0


def check(cond, what):
    global failures
    print(("PASS " if cond else "FAIL ") + what)
    if not cond:
        failures += 1


def run(ordered, items, serial, jobs):
    """Run ordered() with a recording check; returns (names in yield order,
    peak concurrency, peak concurrency seen by a serial item)."""
    lock = threading.Lock()
    state = {"live": 0, "peak": 0, "serial_peak": 0}

    def probe(item):
        with lock:
            state["live"] += 1
            state["peak"] = max(state["peak"], state["live"])
        time.sleep(0.05)
        with lock:
            if item[0] in serial:
                state["serial_peak"] = max(state["serial_peak"], state["live"])
            state["live"] -= 1
        return item[0].upper()

    os.environ["MUTANT_JOBS"] = str(jobs)
    names = []
    for item, result in ordered(items, probe, serial=serial):
        if result != item[0].upper():
            names.append("mismatch")
        names.append(item[0])
    return names, state["peak"], state["serial_peak"]


def broken_ordered(items, check_fn, serial=frozenset(), name=lambda item: item[0]):
    """The known-bad pool: serial items share the pool with the rest."""
    import concurrent.futures

    with concurrent.futures.ThreadPoolExecutor(max_workers=mutant_pool.workers()) as pool:
        futures = [(item, pool.submit(check_fn, item)) for item in items]
        for item, future in futures:
            yield item, future.result()


def main():
    items = [("a",), ("timing",), ("b",), ("c",), ("d",), ("e",)]
    serial = {"timing"}
    names, peak, serial_peak = run(mutant_pool.ordered, items, serial, 4)
    check(names == ["a", "b", "c", "d", "e", "timing"], "list order, serial items last")
    check(peak > 1, "parallel items overlap (peak %d)" % peak)
    check(serial_peak == 1, "a serial item runs alone (saw %d)" % serial_peak)
    names, peak, _ = run(mutant_pool.ordered, items, serial, 1)
    check(peak == 1, "MUTANT_JOBS=1 runs one check at a time (peak %d)" % peak)
    _, _, bad_peak = run(broken_ordered, items, serial, 4)
    check(bad_peak > 1, "known-bad pool lets a serial item overlap (saw %d)" % bad_peak)
    if failures:
        print("mutant pool: %d failures" % failures)
        return 1
    print("mutant pool: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
