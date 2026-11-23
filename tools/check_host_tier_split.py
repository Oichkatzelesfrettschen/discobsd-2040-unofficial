#!/usr/bin/env python3
# Copyright (c) 2026 GizmoBSD
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
# 3. Neither the name of the copyright holder nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# Partition check for the macOS host tier. firmware.yml runs check-host-a and
# check-host-b as two steps with separate deadlines; together they must cover
# exactly the gates of check-host (HOST_GATES and HOST_PROGRAM_GATES), each once.
# HOST_TIER_B_GATES names the gates that run in the second step. A name in it
# that is not a host gate would fail only when its step reaches the target, so
# the gate rejects it here, and rejects an empty second step, which would leave
# the second deadline unexercised.
#
# usage: check_host_tier_split.py --all GATES --a GATES --b GATES
#        check_host_tier_split.py --selftest

import argparse
import sys


def verdict(everything, tier_a, tier_b):
    """Return the list of partition defects; empty means the split is exact."""
    defects = []
    for label, names in (("check-host", everything), ("tier A", tier_a), ("tier B", tier_b)):
        seen = set()
        for name in names:
            if name in seen:
                defects.append(f"{label} lists {name} more than once")
            seen.add(name)
    all_set, a_set, b_set = set(everything), set(tier_a), set(tier_b)
    if not b_set:
        defects.append("tier B is empty")
    if not a_set:
        defects.append("tier A is empty")
    for name in sorted(a_set & b_set):
        defects.append(f"{name} runs in both tiers")
    for name in sorted(b_set - all_set):
        defects.append(f"tier B names {name}, which is not a host gate")
    for name in sorted(a_set - all_set):
        defects.append(f"tier A names {name}, which is not a host gate")
    for name in sorted(all_set - a_set - b_set):
        defects.append(f"{name} runs in neither tier")
    return defects


def selftest():
    everything = ["g1", "g2", "g3", "g4"]
    good = (["g1", "g2"], ["g3", "g4"])
    if verdict(everything, *good):
        raise SystemExit("selftest: an exact partition was rejected")
    mutants = {
        "gate in neither tier": (["g1"], ["g3", "g4"]),
        "gate in both tiers": (["g1", "g2", "g3"], ["g3", "g4"]),
        "unknown name in tier B": (["g1", "g2", "g3"], ["g4", "g9"]),
        "empty tier B": (["g1", "g2", "g3", "g4"], []),
        "empty tier A": ([], ["g1", "g2", "g3", "g4"]),
        "duplicate in tier B": (["g1", "g2"], ["g3", "g4", "g4"]),
    }
    for label, (tier_a, tier_b) in mutants.items():
        if not verdict(everything, tier_a, tier_b):
            raise SystemExit(f"selftest: mutant accepted: {label}")
        print(f"mutant rejected: {label}")
    print("selftest: ok")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--all")
    parser.add_argument("--a")
    parser.add_argument("--b")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    if args.all is None or args.a is None or args.b is None:
        parser.error("--all, --a and --b are required")
    defects = verdict(args.all.split(), args.a.split(), args.b.split())
    for defect in defects:
        print(f"check-host-split: {defect}", file=sys.stderr)
    if defects:
        return 1
    print(
        f"check-host-split: {len(args.a.split())} gates in tier A, {len(args.b.split())} in tier B"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
