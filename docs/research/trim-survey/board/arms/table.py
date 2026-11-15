"""Print per-step deltas of the storage counters of one or more arm captures."""

import re
import sys

KEYS = ("root_writes", "root_program_pages", "root_erase_sectors", "swap_erase_sectors")
for path in sys.argv[1:]:
    text = open(path, errors="replace").read()
    blocks = text.split("### ")
    prev = None
    label = ""
    label_body = ""
    print(path.rsplit("/", 1)[-1])
    for block in blocks:
        head = block.split("\n", 1)[0]
        if head.startswith("sysctl machdep.storage_stats"):
            cur = {k: int(re.search(rf"^{k} (\d+)", block, re.M).group(1)) for k in KEYS}
            if prev is not None:
                d = {k: cur[k] - prev[k] for k in KEYS}
                units = d["root_program_pages"] / 4 / d["root_writes"] if d["root_writes"] else 0
                real = re.search(r"([\d.]+) real", label_body)
                print(
                    f"  {label[:34]:34} writes {d['root_writes']:4} units/write {units:5.2f} "
                    f"root_erases {d['root_erase_sectors']:4} "
                    f"swap_erases {d['swap_erase_sectors']:4} "
                    f"{real.group(1) + ' s' if real else ''}"
                )
            prev = cur
        else:
            label, label_body = head, block
