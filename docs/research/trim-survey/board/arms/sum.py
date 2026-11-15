"""Sum per-compile counter deltas of long-arm captures."""

import re
import sys

KEYS = ("root_writes", "root_program_pages", "root_erase_sectors", "swap_erase_sectors")
for path in sys.argv[1:]:
    text = open(path, errors="replace").read()
    snaps = []
    for block in text.split("### "):
        if block.startswith("sysctl machdep.storage_stats"):
            snaps.append({k: int(re.search(rf"^{k} (\d+)", block, re.M).group(1)) for k in KEYS})
    times = [float(t) for t in re.findall(r"([\d.]+) real", text)]
    n = len(snaps) - 1
    d = {k: snaps[-1][k] - snaps[0][k] for k in KEYS}
    rest = {k: snaps[-1][k] - snaps[1][k] for k in KEYS}
    per = [snaps[i + 1]["root_erase_sectors"] - snaps[i]["root_erase_sectors"] for i in range(n)]
    print(
        f"{path.rsplit('/', 1)[-1]:16} n={n} root_writes/cc {d['root_writes'] / n:6.1f} "
        f"root_erases/cc {d['root_erase_sectors'] / n:6.1f} "
        f"(after first {rest['root_erase_sectors'] / (n - 1):6.1f}) "
        f"units/write {d['root_program_pages'] / 4 / d['root_writes']:5.2f} "
        f"swap_erases/cc {d['swap_erase_sectors'] / n:5.1f} mean {sum(times) / len(times):5.2f} s "
        f"min/max erases {min(per)}/{max(per)}"
    )
