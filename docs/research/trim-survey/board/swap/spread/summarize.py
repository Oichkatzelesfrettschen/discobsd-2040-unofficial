"""usage: summarize.py runN.txt... -- per-run deltas of the swapwear and
swapwho probe tables and of the swap counters between the two sysctl reads."""
import re
import sys

METRIC = re.compile(
    r"^(swap_erase_sectors|swapram_attempts|swapram_admissions|swapram_refused_fragment"
    r"|swapram_refused_nospace|swap_write_bytes) (\d+)$"
)


def tables(text):
    wear, who, cur_w, cur_h, stats = [], [], None, None, []
    for line in text.replace("\r", "").split("\n"):
        if line == "swapwear":
            cur_w, cur_h = [], {}
        elif line.startswith("swapwear ") and line != "swapwear end" and cur_w is not None:
            cur_w += [int(x) for x in line.split()[1:]]
        elif line == "swapwear end" and cur_w is not None:
            wear.append(cur_w)
        elif line.startswith("swapwho ") and line != "swapwho end" and cur_h is not None:
            f = line.split()
            cur_h[f[1]] = (int(f[3]), [int(x) for x in f[5:]])
        elif line == "swapwho end" and cur_h is not None:
            who.append(cur_h)
            cur_w = cur_h = None
        m = METRIC.match(line)
        if m:
            stats.append((m.group(1), int(m.group(2))))
    return wear, who, stats


for path in sys.argv[1:]:
    wear, who, stats = tables(open(path, errors="replace").read())
    a, b = wear[0], wear[-1]
    d = [y - x for x, y in zip(a, b)]
    used = [v for v in d if v]
    print(path)
    mean = sum(used) / len(used)
    print("  swap erases %d over %d sectors used" % (sum(d), len(used)))
    print("  hottest %d coldest-used %d mean %.2f hottest/mean %.3f"
          % (max(d), min(used), mean, max(d) / mean))
    print("  unused sectors:", [i for i, v in enumerate(d) if v == 0])
    for name in sorted(who[-1]):
        r1, f1 = who[-1][name]
        r0, f0 = who[0].get(name, (0, [0] * 12))
        fl = {i + 1: y - x for i, (x, y) in enumerate(zip(f0, f1)) if y - x}
        if r1 - r0 or fl:
            print("  %-8s ram %3d flash by sectors %s -> %d erases"
                  % (name, r1 - r0, fl, sum(k * v for k, v in fl.items())))
    seen = {}
    for k, v in stats:
        seen.setdefault(k, []).append(v)
    print("  " + "; ".join("%s %d" % (k, v[-1] - v[0]) for k, v in seen.items()))
