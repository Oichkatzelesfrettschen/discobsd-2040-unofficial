"""Design variants of the swap tiers, replayed over the calibrated traces.

Each variant is a model Config, a workload change, or both. For each the
table reports admissions, flash erases per compile, the hottest sector's
erases per compile over many sessions, peak flash blocks in use, the
share of sessions the swap map refused, and the kernel RAM and text the
change would add. A refused session stops its replay where the kernel
would refuse the fork or defer the swap (model.Refused), so a row with any
refused session reports no erase or wear figure. RAM is exact
where the change is a buffer size; text is an estimate, and TEXT_BASIS says
what each estimate rests on.

Wear counts whole sessions: boot and login, the 24 compiles and the
closing sysctl, every sector's erases summed over SESSIONS sessions. The
hottest figure is the largest sum over SESSIONS x 24 compiles, so it
carries each session's boot share; life in compiles is the rated 100,000
erases per sector over that figure, for one fit at a time, and the table
gives its range over the fits.

Session starts follow swap_cursor_init() in
sys/arch/rp2040/rp2040/machdep.c. A cold boot draws up to eight 7-bit
samples and takes the first below the sector count, else sector 0; the
samples here are uniform, which the ring oscillator's bits only
approximate. After a software reset the cursor the last session published
(swap_cursor_publish()) is restored. BOOTS chooses cold boots for every
session or one cold boot followed by resets.

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

# A smaller pool on every board, the bare Pico included: the RAM column is
# what the kernel gets back. Each size runs as shipped, with variant 1 and
# with 1+2, since those change which images a small pool can take.
SMALL_POOLS = [12, 8, 4]
for _kb in SMALL_POOLS:
    _ram = (_kb - 16) * 1024
    VARIANTS += [
        (f"9 pool {_kb} KB", Config(pool_kb=_kb), None, _ram, 0),
        (f"9 pool {_kb} KB + 1",
         Config(pool_kb=_kb, u_compressed=True, u_slack=64), None, _ram,
         120),
        (f"9 pool {_kb} KB + 1+2",
         Config(pool_kb=_kb, u_compressed=True, u_slack=64,
                compaction=True), None, _ram, 320),
    ]
VARIANTS.append(("9 no pool", Config(pool_kb=0), None, -16384, 0))

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


def cold_cursor(sectors, rng):
    """swap_cursor_init() on a cold boot: the first of up to eight 7-bit
    samples below sectors, else sector 0."""
    for _ in range(8):
        sample = rng.getrandbits(7)
        if sample < sectors:
            return sample
    return 0


def published_index(next_block, nswap):
    """The sector index swap_cursor_publish() stores for swapnext."""
    if next_block < 4 or next_block > nswap or next_block % 4 or \
            next_block == nswap:
        return 0
    return (next_block - 4) // 4


def measure(config, transform=None, sessions=SESSIONS, seed=7,
            boots="cold"):
    rows = []
    for workload in ensemble():
        if transform is not None:
            workload = transform(workload)
        nsectors = (config.flash_kb - 4) // 4
        rng = random.Random(seed)
        total = None
        peak = refused = boot_erases = 0
        first = None
        cursor = None
        for _ in range(sessions):
            if boots == "reset" and cursor is not None and \
                    cursor < nsectors:
                start = cursor
            else:
                start = cold_cursor(nsectors, rng)
            m = Model(config, start).replay(trace.generate(workload))
            peak = max(peak, m.peak_blocks)
            if m.refused is not None:
                refused += 1
                cursor = None
                continue
            cursor = published_index(m.map.cursor(), m.nswap)
            counters, _measured = m.delta()
            if first is None:
                first = counters
                boot_erases = m.snapshots[0][0].get("swap_erase_sectors",
                                                    0)
            total = list(m.erases) if total is None else \
                [a + b for a, b in zip(total, m.erases)]
        compiles = workload.compiles
        row = {"peak_blocks": peak, "refusing": refused / sessions,
               "admitted": None, "erases": None, "hottest": None,
               "mean": None, "life": None, "boot_erases": None}
        if not refused:
            hottest = max(total[1:]) / sessions / compiles
            row.update({
                "admitted": first.get("swapram_admissions", 0),
                "erases": first.get("swap_erase_sectors", 0) / compiles,
                "hottest": hottest,
                "mean": sum(total[1:]) / max(1, len(total) - 1) /
                sessions / compiles,
                "life": ENDURANCE / hottest if hottest else None,
                "boot_erases": boot_erases})
        rows.append(row)
    return rows


def life_range(rows):
    """Life in compiles per fit, as a range over the fits."""
    lives = [r["life"] for r in rows]
    if any(life is None for life in lives):
        return "unbounded" if all(life is None for life in lives) else \
            "unbounded to " + f"{max(x for x in lives if x):,.0f}"
    return span(lives, "{:,.0f}")


def refusing(rows):
    """Share of sessions the swap map refused, over the fits."""
    return span([r["refusing"] for r in rows], "{:.0%}")


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
               "erases/compile | Life (compiles), per fit | Peak blocks | Sessions "
               "refused | Extra RAM | Text (est.) | Erases saved/compile "
               "| Saved per KB RAM | Saved per KB text |")
    out.append("|" + " --- |" * 12)
    results = {}
    for name, config, transform, ram, text in VARIANTS:
        rows = measure(config, transform, sessions)
        if any(r["refusing"] for r in rows):
            out.append(f"| {name} | refuses the workload in "
                       f"{refusing(rows)} of sessions |" + " -- |" * 10)
            continue
        erases = sum(r["erases"] for r in rows) / len(rows)
        hot = sum(r["hottest"] for r in rows) / len(rows)
        saved = base_erases - erases
        per_ram = f"{saved / (ram / 1024):.2f}" if ram > 0 else "--"
        per_text = f"{saved / (text / 1024):.1f}" if text else "--"
        life = life_range(rows)
        results[name] = (saved, ram, text, hot)
        out.append(
            f"| {name} | {span([r['admitted'] for r in rows], '{}')} | "
            f"{span([r['erases'] for r in rows], '{:.1f}')} | "
            f"{span([r['hottest'] for r in rows], '{:.3f}')} | {life} | "
            f"{max(r['peak_blocks'] for r in rows)} | "
            f"{refusing(rows)} | {ram} | {text} | "
            f"{saved:.1f} | {per_ram} | {per_text} |")
    out.append("")
    out.append("| Raw swap | Sectors | Erases/compile | Mean sector "
               "erases/compile | Hottest | Hottest/mean | Life, cold boots "
               "| Life, resets | Peak blocks | Sessions refused |")
    out.append("|" + " --- |" * 10)
    for kb in FLASH_SIZES:
        rows = measure(Config(flash_kb=kb), sessions=sessions)
        reset = measure(Config(flash_kb=kb), sessions=sessions,
                        boots="reset")
        peak = max(r["peak_blocks"] for r in rows)
        if any(r["refusing"] for r in rows):
            out.append(f"| {kb} KB | {(kb - 4) // 4} | refuses the "
                       f"workload | -- | -- | -- | -- | -- | {peak} | "
                       f"{refusing(rows)} |")
            continue
        out.append(
            f"| {kb} KB | {(kb - 4) // 4} | "
            f"{span([r['erases'] for r in rows], '{:.1f}')} | "
            f"{span([r['mean'] for r in rows], '{:.3f}')} | "
            f"{span([r['hottest'] for r in rows], '{:.3f}')} | "
            f"{span([r['hottest'] / r['mean'] for r in rows], '{:.2f}')} | "
            f"{life_range(rows)} | "
            f"{life_range(reset)} | {peak} | {refusing(rows)} |")
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
