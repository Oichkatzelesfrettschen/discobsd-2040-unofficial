"""Design variants of the swap tiers, replayed over the calibrated traces.

Each variant is a model Config, a workload change, or both. For each the
table reports admissions, flash erases per compile, the hottest sector's
erases per compile over many sessions, peak flash blocks in use, flash
refusals, and the kernel RAM and text the change would add. RAM is exact
where the change is a buffer size; text is an estimate, and TEXT_BASIS says
what each estimate rests on.

The hottest-sector figure sums every sector's erases over SESSIONS boots,
each with the swap cursor at a uniformly drawn sector (swap_cursor_init()
in sys/arch/rp2040/rp2040/machdep.c), and divides the largest sum by
SESSIONS x 24 compiles. Life in compiles is the rated 100,000 erases per
sector over that figure.

usage: $PYTHON variants.py [--sessions N]   print the table as Markdown
"""

import random
import sys
from dataclasses import replace
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    __package__ = "swap_tier_model"     # noqa: A001

from . import calibrate, trace  # noqa: E402
from .model import Config, Model  # noqa: E402

ENDURANCE = 100_000
SESSIONS = 40

# name, Config, workload transform, extra RAM bytes, text estimate bytes
VARIANTS = [
    ("baseline (PICO kernel)", Config(), None, 0, 0),
    ("1 u area reserved at compressed size + 64",
     Config(u_compressed=True, u_slack=64), None, 0, 120),
    ("2 compaction on refusal", Config(compaction=True), None, 0, 200),
    ("3a pool 24 KB", Config(pool_kb=24), None, 8192, 0),
    ("3b pool 32 KB", Config(pool_kb=32), None, 16384, 0),
    ("4 refused images compressed on flash",
     Config(flash_compressed=True), None, 0, 600),
    ("5 zero-sector elision on flash", Config(zero_elision=True), None,
     32, 80),
    ("6 cc driver shell never swaps out", Config(),
     lambda w: replace(w, driver_swaps=False), 0, 0),
    ("1+2", Config(u_compressed=True, u_slack=64, compaction=True), None,
     0, 320),
    ("1+2+4", Config(u_compressed=True, u_slack=64, compaction=True,
                     flash_compressed=True), None, 0, 920),
    ("8 swap off NOR (PSRAM or SD)", Config(flash=False), None, 0, 0),
]

FLASH_SIZES = [384, 256, 192, 128, 96, 64]

TEXT_BASIS = {
    "1": "one more counting encode of the u area plus an overflow branch "
         "that cancels the reservation and takes flash: about 30 to 60 "
         "Thumb-1 instructions",
    "2": "a pass over sr_tab in offset order with a bcopy per image and "
         "e_off updates, skipped while an exec spool is live: about 100 "
         "instructions",
    "3": "SWAPRAM_KB only; the u_short offsets of sr_ent hold up to 64 KB",
    "4": "a streaming encode into the 256-byte flash page buffer and a "
         "decode from the XIP window on swapin, reusing the codec "
         "workspace; the evacuation path already expands to flash",
    "5": "a per-process mask of skipped sectors (2 bytes x NPROC) and a "
         "scan before each sector erase",
}


def ensemble():
    """The independent fits that calibrate alike (calibrate.FITS)."""
    return list(calibrate.ENSEMBLE)


def session(workload, config, cursor_sector=0):
    m = Model(config, cursor_sector).replay(trace.generate(workload))
    counters, erases = m.delta()
    return m, counters, erases


def measure(config, transform=None, sessions=SESSIONS, seed=7):
    rows = []
    for workload in ensemble():
        if transform is not None:
            workload = transform(workload)
        nsectors = (config.flash_kb * 1024 // 1024 - 4) // 4
        rng = random.Random(seed)
        total = None
        peak = refusals = 0
        first = None
        for _ in range(sessions):
            cursor = rng.randrange(nsectors)
            m, counters, erases = session(workload, config, cursor)
            if first is None:
                first = counters
            total = erases if total is None else \
                [a + b for a, b in zip(total, erases)]
            peak = max(peak, m.peak_blocks)
            refusals += counters.get("flash_refusals", 0)
        compiles = workload.compiles
        hottest = max(total[1:]) / sessions / compiles if total else 0.0
        mean = sum(total[1:]) / max(1, len(total) - 1) / sessions / compiles
        rows.append({
            "admitted": first.get("swapram_admissions", 0),
            "erases": first.get("swap_erase_sectors", 0) / compiles,
            "hottest": hottest, "mean": mean, "peak_blocks": peak,
            "refusals": refusals,
        })
    return rows


def span(values, fmt):
    lo, hi = min(values), max(values)
    return fmt.format(lo) if fmt.format(lo) == fmt.format(hi) else \
        fmt.format(lo) + " to " + fmt.format(hi)


def table(sessions=SESSIONS):
    out = []
    base = measure(Config(), sessions=sessions)
    base_erases = sum(r["erases"] for r in base) / len(base)
    base_hot = sum(r["hottest"] for r in base) / len(base)
    out.append("| Variant | Admitted | Erases/compile | Hottest sector "
               "erases/compile | Life (compiles) | Peak blocks | Flash "
               "refusals | Extra RAM | Text (est.) | Erases saved/compile "
               "| Saved per KB RAM | Saved per KB text |")
    out.append("|" + " --- |" * 12)
    results = {}
    for name, config, transform, ram, text in VARIANTS:
        rows = measure(config, transform, sessions)
        erases = sum(r["erases"] for r in rows) / len(rows)
        hot = sum(r["hottest"] for r in rows) / len(rows)
        saved = base_erases - erases
        per_ram = f"{saved / (ram / 1024):.2f}" if ram else "--"
        per_text = f"{saved / (text / 1024):.1f}" if text else "--"
        life = f"{ENDURANCE / hot:,.0f}" if hot else "unbounded"
        results[name] = (saved, ram, text, hot)
        out.append(
            f"| {name} | {span([r['admitted'] for r in rows], '{}')} | "
            f"{span([r['erases'] for r in rows], '{:.1f}')} | "
            f"{span([r['hottest'] for r in rows], '{:.3f}')} | {life} | "
            f"{max(r['peak_blocks'] for r in rows)} | "
            f"{sum(r['refusals'] for r in rows)} | {ram} | {text} | "
            f"{saved:.1f} | {per_ram} | {per_text} |")
    out.append("")
    out.append("| Raw swap | Sectors | Erases/compile | Mean sector "
               "erases/compile | Hottest | Hottest/mean | Life (compiles) "
               "| Peak blocks | Flash refusals |")
    out.append("|" + " --- |" * 9)
    for kb in FLASH_SIZES:
        rows = measure(Config(flash_kb=kb), sessions=sessions)
        hot = sum(r["hottest"] for r in rows) / len(rows)
        mean = sum(r["mean"] for r in rows) / len(rows)
        out.append(
            f"| {kb} KB | {(kb - 4) // 4} | "
            f"{span([r['erases'] for r in rows], '{:.1f}')} | {mean:.3f} | "
            f"{hot:.3f} | {hot / mean:.2f} | {ENDURANCE / hot:,.0f} | "
            f"{max(r['peak_blocks'] for r in rows)} | "
            f"{sum(r['refusals'] for r in rows)} |")
    return "\n".join(out), results, base_hot


def main(argv):
    sessions = SESSIONS
    if "--sessions" in argv:
        sessions = int(argv[argv.index("--sessions") + 1])
    text, _results, _hot = table(sessions)
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
