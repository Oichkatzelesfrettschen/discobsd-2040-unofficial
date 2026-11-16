"""Calibrate the property tests: every mutant of the shipped C must fail them.

Each mutant replaces one expression in a copy of a shipped source file. The
copy must compile (a build failure is an error, not a rejection), and the
named property test file must then fail with assertion failures (pytest exit
status 1). The clean sources must pass the same files first.

usage: $PYTHON mutants.py
"""

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from swap_tier_model import native  # noqa: E402

MUTANTS = [
    ("pool-exact-fit-refused", "pool", "test_pool_props.py",
     "if (mp->p_seg[i].s_size < want)",
     "if (mp->p_seg[i].s_size <= want)"),
    ("pool-no-merge-above", "pool", "test_pool_props.py",
     "if (i < mp->p_used && off + want == mp->p_seg[i].s_off) {",
     "if (i < mp->p_used && off + want + SWAPRAM_GRAIN == "
     "mp->p_seg[i].s_off) {"),
    ("pool-overlap-below-unchecked", "pool", "test_pool_props.py",
     "if (i > 0 && mp->p_seg[i - 1].s_off + mp->p_seg[i - 1].s_size > off)\n"
     "        return -1;",
     "if (i > 0 && mp->p_seg[i - 1].s_off + mp->p_seg[i - 1].s_size > "
     "off + SWAPRAM_GRAIN)\n        return -1;"),
    ("pool-trim-leaks-one-grain", "pool", "test_pool_props.py",
     "return swapram_pool_free(mp, off + keep, had - keep);",
     "return had - keep > SWAPRAM_GRAIN ? swapram_pool_free(mp, "
     "off + keep + SWAPRAM_GRAIN, had - keep - SWAPRAM_GRAIN) : 0;"),
    ("append-erases-wrong-page", "flash_swap", "test_flash_swap_props.py",
     "if ((offset & (FLASH_SECTOR_BYTES - 1)) == 0 &&",
     "if ((offset & (FLASH_SECTOR_BYTES - 1)) == FLASH_PROG_BYTES &&"),
    ("rewrite-restores-from-erased", "flash_swap", "test_flash_swap_props.py",
     "if (ops->read(spare_sector + page, scratch,",
     "if (ops->read(destination_sector + page, scratch,"),
]


def pytest(test, overrides):
    env = dict(os.environ)
    env["PYTEST_DISABLE_PLUGIN_AUTOLOAD"] = "1"
    if overrides:
        env["SWAP_TIER_MODEL_OVERRIDES"] = json.dumps(overrides)
    else:
        env.pop("SWAP_TIER_MODEL_OVERRIDES", None)
    return subprocess.run(
        [sys.executable, "-m", "pytest", "-q", "-x", "-p", "no:cacheprovider",
         str(HERE / test)], cwd=HERE, env=env, capture_output=True, text=True,
        check=False)


def main():
    failures = 0
    tests = sorted({m[2] for m in MUTANTS})
    for test in tests:
        result = pytest(test, None)
        if result.returncode != 0:
            print(f"FAIL clean sources fail {test}:\n{result.stdout[-2000:]}")
            return 1
    with tempfile.TemporaryDirectory(prefix="swap-tier-mutant-") as scratch:
        for name, key, test, old, new in MUTANTS:
            text = native.SOURCES[key].read_text(encoding="utf-8")
            if text.count(old) != 1:
                print(f"FAIL {name}: pattern occurs {text.count(old)} times")
                failures += 1
                continue
            path = Path(scratch) / f"{name}-{native.SOURCES[key].name}"
            path.write_text(text.replace(old, new), encoding="utf-8")
            overrides = {key: str(path)}
            try:
                native.build(overrides)
            except RuntimeError as error:
                print(f"FAIL {name}: mutant does not compile\n{error}")
                failures += 1
                continue
            result = pytest(test, overrides)
            if result.returncode == 1 and "AssertionError" in result.stdout:
                print(f"rejected {name} ({test})")
            else:
                print(f"FAIL {name}: survived or errored "
                      f"(pytest status {result.returncode})\n"
                      f"{result.stdout[-1500:]}")
                failures += 1
    print(f"mutants: {len(MUTANTS) - failures} of {len(MUTANTS)} rejected")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
