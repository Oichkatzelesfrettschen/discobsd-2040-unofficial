"""usage: summarize.py runN.txt...

Per-run differences between the two `sysctl machdep.storage_stats` reads of
a capture: the swapwear table (erases of each of the 96 swap sectors) and
the swapwho table (completed swap-outs by command and path, with exact
sector sums), cross-checked against the kernel's own counters. A capture
whose tables are incomplete or disagree with the counters is rejected.
"""
import re
import sys

SECTORS = 96
PATHS = ("ram", "copy", "exch")
METRIC = re.compile(
    r"^(swap_erase_sectors|swapram_attempts|swapram_admissions|swapram_flash_swapouts"
    r"|swapram_exchange_swapouts|swapram_refused_fragment|swapram_refused_nospace) (\d+)$"
)


def snapshots(text):
    """Return [(wear, who, metrics)] for each complete probe print."""
    out, wear, who, metrics, pending = [], None, None, {}, {}
    for line in text.split("\n"):
        m = METRIC.match(line)
        if m:
            pending[m.group(1)] = int(m.group(2))
        elif line == "swapwear":
            if out and pending:
                out[-1][2].update(pending)
            wear, who, pending = [], {}, {}
        elif line == "swapwear end":
            pass
        elif line.startswith("swapwear ") and wear is not None:
            wear += [int(x) for x in line.split()[1:]]
        elif line == "swapwho end" and who is not None:
            out.append((wear, who, metrics))
            wear, who, metrics = None, None, {}
        elif line.startswith("swapwho ") and who is not None:
            f = line.split()
            row = {}
            for i, path in enumerate(PATHS):
                if f[2 + 5 * i] != path:
                    raise SystemExit("unexpected swapwho line: " + line)
                row[path] = tuple(int(x) for x in f[3 + 5 * i:7 + 5 * i])
            who[f[1]] = row
    if out and pending:
        out[-1][2].update(pending)
    return out


def summarize(path):
    snaps = snapshots(open(path, errors="replace").read())
    if len(snaps) != 2:
        raise SystemExit("%s: %d probe prints, expected 2" % (path, len(snaps)))
    (wear0, who0, met0), (wear1, who1, met1) = snaps
    for wear in (wear0, wear1):
        if len(wear) != SECTORS:
            raise SystemExit("%s: a swapwear table has %d entries, expected %d"
                             % (path, len(wear), SECTORS))
    # Each print is checked whole against the counters read with it, so a
    # missing row fails even when that command did nothing in the interval.
    for label, wear, who, met in (("first", wear0, who0, met0), ("second", wear1, who1, met1)):
        absolute = (
            ("per-sector erases", sum(wear), met["swap_erase_sectors"]),
            ("flash sectors by command",
             sum(r["copy"][1] + r["exch"][1] for r in who.values()), met["swap_erase_sectors"]),
            ("copy-outs", sum(r["copy"][0] for r in who.values()), met["swapram_flash_swapouts"]),
            ("exchanges", sum(r["exch"][0] for r in who.values()),
             met["swapram_exchange_swapouts"]),
            ("RAM swap-outs", sum(r["ram"][0] for r in who.values()), met["swapram_admissions"]),
        )
        for what, got, want in absolute:
            if got != want:
                raise SystemExit("%s: %s print: %s %d disagrees with the kernel counter %d"
                                 % (path, label, what, got, want))
    gone = sorted(set(who0) - set(who1))
    if gone:
        raise SystemExit("%s: commands of the first print missing from the second: %s"
                         % (path, ", ".join(gone)))
    delta = [b - a for a, b in zip(wear0, wear1)]
    used = [v for v in delta if v]
    mean = sum(used) / len(used)
    metric = {k: met1[k] - met0[k] for k in met1}
    print(path)
    print("  swap erases %d over %d sectors; unused %s"
          % (sum(delta), len(used), [i for i, v in enumerate(delta) if v == 0]))
    print("  hottest %d, least-used %d, mean %.2f, hottest/mean %.3f"
          % (max(delta), min(used), mean, max(delta) / mean))
    for name in sorted(who1):
        cells = []
        for p in PATHS:
            count1, sect1, low, high = who1[name][p]
            count0, sect0 = who0.get(name, {}).get(p, (0, 0, 0, 0))[:2]
            if count1 - count0:
                cells.append("%s %d (%d sectors, spans %d-%d)"
                             % (p, count1 - count0, sect1 - sect0, low, high))
        if cells:
            print("  %-8s %s" % (name, "; ".join(cells)))
    print("  " + "; ".join("%s %d" % kv for kv in sorted(metric.items())))
    print("  cross-checks: both prints agree with the five kernel counters read with them")

for arg in sys.argv[1:]:
    summarize(arg)
