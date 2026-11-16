"""Enumerate every transferred prefix at selected production V7 write calls.

Ordinary errors must compensate when subsequent I/O succeeds. Interrupted
images instead characterize the existing format's inspection boundary; the
script reports old/new semantic states and rejected intermediate images
separately. The enumeration covers byte prefixes, rather than arbitrary
programmed-bit subsets, device ordering changes, or electrical erase faults.
"""

import argparse
import json
import tempfile
from collections import Counter
from pathlib import Path

from storage_trace import Device, Fault
from v7_image import verify
from v7fs_backend import Backend, compile_backend


def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)


def prepare(library, order, blocks):
    backend = Backend(library, Device(bytes(512 * 2048)), order)
    try:
        require(backend.format(64) == 0, "format failed")
        require(backend.fill_parent(blocks) == 0, "full parent construction failed")
        image = backend.device.export()
        verify(image, order)
        return image
    finally:
        backend.close()


def execute(library, image, order, directory, fault=None):
    device = Device(image, faults=[fault] if fault else [])
    backend = Backend(library, device, order)
    try:
        require(backend.mount() == 0, "mount failed before selected write")
        result = backend.create("new", directory)
        operations = list(device.events)
        if not device.frozen:
            require(backend.sync() == 0, "compensated state failed writeback")
        device.verify_injections()
        return result, device.export(), operations
    finally:
        backend.close()


def run_case(library, order, blocks, directory):
    image = prepare(library, order, blocks)
    old = verify(image, order)
    result, successful, events = execute(library, image, order, directory)
    require(result == 0, "positive creation failed")
    new = verify(successful, order)
    counts = Counter()
    first_intermediate = None
    writes = [event for event in events if event.kind == "write"]
    for event in writes:
        for amount in range(event.length + 1):
            fault = Fault(event.sequence, "after_error", amount)
            result, persistent, _events = execute(library, image, order, directory, fault)
            require(result != 0, f"write failure hidden at {event.sequence}/{amount}")
            require(verify(persistent, order) == old,
                    f"compensation differs at {order}/{blocks}/{directory}/"
                    f"{event.sequence}/{amount}")
            counts["compensated_prefixes"] += 1

            fault = Fault(event.sequence, "interrupt", amount)
            result, persistent, _events = execute(library, image, order, directory, fault)
            require(result != 0, "interrupted creation reported success")
            reason = None
            try:
                recovered = verify(persistent, order)
                category = ("interrupted_old" if recovered == old else
                            "interrupted_new" if recovered == new else "interrupted_other")
            except ValueError as error:
                category = "interrupted_invalid"
                reason = str(error)
            counts[category] += 1
            if (category in {"interrupted_invalid", "interrupted_other"} and
                    first_intermediate is None):
                first_intermediate = {"sequence": event.sequence, "offset": event.offset,
                                      "transferred": amount, "reason": reason or category}
        print(json.dumps({"progress": {"order": order, "parent_blocks": blocks,
                                       "directory": directory, "sequence": event.sequence}},
                         sort_keys=True), flush=True)
    require(first_intermediate is not None,
            "interruption characterization lost its negative control")
    summary = {"order": order, "parent_blocks": blocks, "directory": directory,
               "write_calls": len(writes), "counts": dict(counts),
               "first_intermediate": first_intermediate}
    print(json.dumps(summary, sort_keys=True), flush=True)
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parent-blocks", type=int, nargs="+", default=[1, 10, 138],
                        choices=[1, 10, 138])
    parser.add_argument("--orders", nargs="+", default=["little", "pdp"],
                        choices=["little", "pdp"])
    args = parser.parse_args()
    totals = Counter()
    with tempfile.TemporaryDirectory(prefix="v7-prefix-matrix-") as directory:
        library = compile_backend(Path(directory))
        for order in args.orders:
            for blocks in args.parent_blocks:
                for is_directory in (False, True):
                    totals.update(run_case(library, order, blocks, is_directory))
    print(json.dumps({"totals": dict(totals), "model": "complete byte-prefix enumeration",
                      "atomic_recovery": "unadmitted; intermediate images require inspection"},
                     sort_keys=True), flush=True)


if __name__ == "__main__":
    main()
