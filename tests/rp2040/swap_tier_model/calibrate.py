"""Calibration of the compile trace against the board's counters.

The targets are the counter deltas of the retained capture
docs/research/trim-survey/board/swap/admission/fixed-sh.txt, read at run
time through that directory's own delta.py. The tolerances below were
fixed before the first fit and are not widened to make a trace pass.

FITTED counters must lie within tolerance for a trace to count as
calibrated. HELD_OUT counters were not used to choose sizes; their
residuals are reported, never gated. DERIVED states identities that follow
from the span bins alone, so their agreement is not independent evidence.

usage: $PYTHON calibrate.py [--main]   print targets, model values, residuals
"""

import importlib.util
import sys
from dataclasses import replace
from pathlib import Path

HERE = Path(__file__).resolve().parent
if __package__ in (None, ""):
    sys.path.insert(0, str(HERE.parent))
    __package__ = "swap_tier_model"  # noqa: A001

from . import model as model_mod  # noqa: E402
from . import trace as trace_mod  # noqa: E402
from .trace import Image, Workload  # noqa: E402

CAPTURES = HERE.parents[2] / "docs" / "research" / "trim-survey" / "board" / "swap" / "admission"
COMPILES = 24

# (counter, kind, bound): kind "rel" bounds |model - board| / board,
# "abs" bounds |model - board|. Fixed before fitting.
FITTED = [
    ("swapram_attempts", "rel", 0.05),
    ("swapram_admissions", "rel", 0.10),
    ("swapram_flash_swapouts", "rel", 0.10),
    ("swapram_exchange_swapouts", "rel", 0.10),
    ("swapram_refused_fragment", "rel", 0.10),
    ("swapram_refused_nospace", "abs", 8),
    ("swapram_ram_span_02", "abs", 5),
    ("swapram_ram_span_05", "abs", 8),
    ("swapram_flash_span_05", "abs", 8),
    ("swapram_flash_span_06", "abs", 5),
    ("swapram_flash_span_07", "abs", 5),
    ("swapram_flash_span_12", "abs", 1),
    ("swapram_short_1k", "abs", 15),
    ("swapram_short_2k", "abs", 10),
    ("swapram_short_3k", "abs", 10),
    ("swapram_short_4k", "abs", 10),
    ("swapram_short_6k", "abs", 10),
    ("swapram_short_7k", "abs", 2),
    ("swap_erase_sectors", "rel", 0.03),
]

HELD_OUT = [
    "swapram_refused_want_bytes",
    "swapram_refused_free_bytes",
    "swapram_refused_largest_bytes",
    "swapram_refused_data_fits",
    "swapram_admitted_data_bytes",
    "swapram_admitted_u_bytes",
    "swapram_admit_scan_bytes",
    "swapram_refuse_scan_bytes",
    "swap_write_bytes",
]

DERIVED = (
    "swap_erase_sectors equals the sum over flash span bins of bin x "
    "count when every flash image erases its span; the board's 1,412 is "
    "49 x 5 + 192 x 6 + 15, so the bin of 12 or more holds one 15-sector "
    "image, and erases per compile (58.8) restate the span bins."
)


def load_delta(name):
    spec = importlib.util.spec_from_file_location("admission_delta", CAPTURES / "delta.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.deltas(str(CAPTURES / name))


def targets(name="fixed-sh.txt"):
    return load_delta(name)


# The fitted parameters (trace model 3, README.md "Calibration"), in the
# order of VECTOR: compressed bytes per class, then jitter, the update
# schedule (every, phase), the number of as/ld displacements, the compile
# whose smlrc update displaces, and the spawns between update runs ahead
# of a fresh child. Raw sizes stay at BASE's, which put each class in its
# board span bin. fit.py found each vector under its jitter seed.
BASE = Workload(
    init=Image(data=5200, stack=1200, cdata=0, cstack=300, cu=0),
    rc=Image(data=12800, stack=2000, cdata=0, cstack=350, cu=0),
    update=Image(data=1400, stack=900, cdata=0, cstack=200, cu=0),
    shell=Image(data=12800, stack=2000, cdata=0, cstack=300, cu=0),
    driver=Image(data=16000, stack=2000, cdata=0, cstack=350, cu=0),
    compiler=Image(data=54000, stack=2000, cdata=0, cstack=400, cu=1200),
    command=Image(data=12800, stack=1000, cdata=0, cstack=200, cu=0),
)
VECTOR = [
    ("init", "cdata"),
    ("init", "cu"),
    ("update", "cdata"),
    ("update", "cu"),
    ("rc", "cdata"),
    ("rc", "cu"),
    ("shell", "cdata"),
    ("shell", "cu"),
    ("driver", "cdata"),
    ("driver", "cu"),
    ("compiler", "cdata"),
    ("command", "cdata"),
    ("command", "cu"),
]


def from_vector(v, seed):
    w = BASE
    for (image, name), x in zip(VECTOR, v):
        w = replace(w, **{image: replace(getattr(w, image), **{name: int(x)})})
    jitter, every, phase, n, first, before = (int(x) for x in v[len(VECTOR) :])
    shifts = tuple((int((i + 0.5) * w.compiles / n), 1 + i % 2) for i in range(n))
    return replace(
        w,
        jitter=jitter,
        update_every=every,
        update_phase=phase,
        displacements=((first % w.compiles, 0),) + shifts,
        update_before_child=before,
        seed=seed,
    )


FITS = {
    1: [
        1333,
        1280,
        584,
        1196,
        2387,
        676,
        3412,
        839,
        4927,
        767,
        10514,
        3513,
        1294,
        84,
        30,
        15,
        3,
        7,
        48,
    ],
    3: [
        1148,
        1251,
        717,
        1000,
        4840,
        840,
        3786,
        600,
        5570,
        1493,
        10915,
        4128,
        1172,
        196,
        28,
        13,
        3,
        4,
        74,
    ],
    4: [
        1474,
        1241,
        633,
        1300,
        3703,
        983,
        3423,
        708,
        4536,
        891,
        10702,
        2723,
        881,
        163,
        30,
        23,
        3,
        6,
        38,
    ],
    5: [
        1283,
        1184,
        1130,
        772,
        3307,
        1500,
        3383,
        877,
        4695,
        860,
        10082,
        3943,
        660,
        171,
        24,
        9,
        2,
        13,
        48,
    ],
}
FIXED = from_vector(FITS[1], 1)

# Independent fits that calibrate alike; the variant table reports its
# range over them.
ENSEMBLE = tuple(from_vector(v, seed) for seed, v in sorted(FITS.items()))


# The shell before the break-growth fix (main-sh.txt). Only the two bin/sh
# classes change, each from main-sh.txt's own sums: the driver's data is one
# 4 KiB sector longer (refuse scan bytes +879,360 over 192 images); the
# login shell's compressed data is 284 bytes and its u area 31 bytes
# shorter (admitted data -13,656 and u -1,466 over 48 admissions); the
# driver's compressed data is 541 bytes longer (refused want +89,885 less
# the shell's -284 x 49, over 192). Nothing else is refitted.
def main_of(w):
    return replace(
        w,
        shell=replace(w.shell, cdata=w.shell.cdata - 284, cu=w.shell.cu - 31),
        driver=replace(w.driver, data=w.driver.data + 4096, cdata=w.driver.cdata + 541),
    )


MAIN = main_of(FIXED)


def evaluate(records, config=None, name="fixed-sh.txt"):
    m = model_mod.run(records, config)
    got, _erases = m.delta()
    want = targets(name)
    rows = []
    for counter, kind, bound in FITTED:
        board, mod = want.get(counter, 0), got.get(counter, 0)
        err = abs(mod - board) / board if kind == "rel" and board else abs(mod - board)
        rows.append((counter, board, mod, err, bound, err <= bound))
    held = [(c, want.get(c, 0), got.get(c, 0)) for c in HELD_OUT]
    return rows, held, m


def report(rows, held):
    lines = ["counter                          board      model  residual"]
    for counter, board, mod, err, bound, ok in rows:
        lines.append(
            f"{counter:32s} {board:9d} {mod:9d}  {err:.3f}/{bound} {'ok' if ok else 'MISS'}"
        )
    lines.append("held out:")
    for counter, board, mod in held:
        rel = (mod - board) / board if board else 0.0
        lines.append(f"{counter:32s} {board:9d} {mod:9d}  {rel:+.3f}")
    return "\n".join(lines)


def main(argv):
    name = "main-sh.txt" if "--main" in argv else "fixed-sh.txt"
    workload = MAIN if name == "main-sh.txt" else FIXED
    rows, held, _m = evaluate(trace_mod.generate(workload), name=name)
    print(report(rows, held))
    return 0 if all(r[-1] for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
