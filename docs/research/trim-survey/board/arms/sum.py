"""Sum the storage counter deltas of the compile steps of arm captures.

A compile step is the interval between two counter prints whose command in
between runs `cc -o d d.c`; the file write and delete steps of the short
captures are left out.
"""

import re
import sys

KEYS = ("root_writes", "root_program_pages", "root_erase_sectors", "swap_erase_sectors")
for path in sys.argv[1:]:
    text = open(path, errors="replace").read()
    prev = None
    compiled = False
    steps = []
    times = []
    for block in text.split("### "):
        head = block.split("\n", 1)[0]
        if head.startswith("sysctl machdep.storage_stats"):
            cur = {k: int(re.search(rf"^{k} (\d+)", block, re.M).group(1)) for k in KEYS}
            if prev is not None and compiled:
                steps.append({k: cur[k] - prev[k] for k in KEYS})
            prev = cur
            compiled = False
        elif "cc -o d d.c" in head:
            compiled = True
            times += [float(t) for t in re.findall(r"([\d.]+) real", block)]
    n = len(steps)
    d = {k: sum(step[k] for step in steps) for k in KEYS}
    rest = sum(step["root_erase_sectors"] for step in steps[1:])
    per = [step["root_erase_sectors"] for step in steps]
    print(
        f"{path.rsplit('/', 1)[-1]:10} compiles={n} root_writes/cc {d['root_writes'] / n:6.1f} "
        f"root_erases {d['root_erase_sectors']:5} /cc {d['root_erase_sectors'] / n:6.1f} "
        f"(after first {rest / (n - 1):6.1f}) "
        f"pages/write {d['root_program_pages'] / d['root_writes']:5.2f} "
        f"swap_erases/cc {d['swap_erase_sectors'] / n:5.1f} mean {sum(times) / len(times):5.2f} s "
        f"min/max erases {min(per)}/{max(per)}"
    )
