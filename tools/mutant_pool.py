"""Run independent mutant checks concurrently and report them in list order.

A play mutant spends most of its wall time waiting on a pseudo-terminal
and the game's own timers, so the checks overlap well beyond the core
count. Each check owns its working directory and its processes; the caller
prints one verdict line per result as ordered() yields it, which keeps the
logged gate's idle timer fed. A timing mutant is visible only when the host
schedules the game inside a gap the case leaves, so the serial ones run one
at a time after the pool drains, with no sibling mutant competing for the
CPU. The pool takes one worker per CPU the process may use, its affinity
bounded by its cgroup's CPU quota (available_cpus.py); MUTANT_JOBS
overrides the size, and 1 restores a serial run.
"""

import concurrent.futures
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from available_cpus import available_cpus  # noqa: E402


def workers():
    value = os.environ.get("MUTANT_JOBS", "")
    if value:
        return max(1, int(value))
    return available_cpus()


def ordered(items, check, serial=frozenset(), name=lambda item: item[0]):
    """Yield (item, check(item)) for every item in list order, then the
    items whose name is in serial, each run alone after the others."""
    parallel = [item for item in items if name(item) not in serial]
    alone = [item for item in items if name(item) in serial]
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers()) as pool:
        futures = [(item, pool.submit(check, item)) for item in parallel]
        for item, future in futures:
            yield item, future.result()
    for item in alone:
        yield item, check(item)
