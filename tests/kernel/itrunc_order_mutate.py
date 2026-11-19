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
"""Generate calibrated mutations of itrunc() in sys/kern/ufs_inode.c.

Each mutation removes one condition of the inode-before-free ordering that
itrunc_order_test.c holds. An anchor that is missing or not unique stops the
run, so a mutant never silently equals the shipped source.
"""

import argparse
from pathlib import Path

COMMENT = "".join(
    line + "\n"
    for line in (
        "    /*",
        "     * The shortened inode reaches the device before the first block is",
        "     * freed, so a reset between the two leaves an inode that names only",
        "     * blocks it still owns and fsck returns the unreferenced ones to the",
        "     * free list; the reverse order leaves an on-disk inode claiming blocks",
        "     * the free list may already hold or have handed out again.  ICHG|IUPD",
        "     * make iupdat() write, since it returns at once on clean flags, and a",
        "     * waitfor of 1 submits the write through bwrite() whatever MNT_ASYNC",
        "     * says.  The frees change only the copy in tip, so no second write",
        "     * follows them.",
        "     */",
    )
)
FLAGS = "    oip->i_flag |= ICHG|IUPD;\n"
RESTORE = """            oip->i_size = tip.i_size;
            bcopy(tip.i_addr, oip->i_addr, NADDR * sizeof(daddr_t));
"""
TAIL_RELEASE = "            if (tailbp)\n                brelse(tailbp);\n"
CHECK = (
    "        if (error) {\n"
    + TAIL_RELEASE
    + RESTORE
    + """            u.u_error = error;
            return;
        }
"""
)
INDIRECT = "        if ((aflags & B_SYNC) || lastbn >= 0) {\n"
UPDATE = (
    """    {
        int error = iupdat(oip, &time, &time, 1);

        /*
         * Nothing is freed while the device still holds the old addresses,
         * and the inode goes back to naming them, so that it agrees with
         * the device and a later truncation finds the blocks to free.  The
         * bytes past the new end in the last kept block are still valid
         * under the old size, so that block is released as it was read.
         */
"""
    + CHECK
    + "    }\n"
)
WRITE = COMMENT + FLAGS + UPDATE
RETURN = """    /* The inode was written before the frees and the frees leave it as is. */
    return;

doquotaupd:"""

# name -> (old fragment, new fragment, ...) in anchored pairs
MUTATIONS = {
    # The inode reaches the device after the frees, as 2.11BSD orders it.
    "inode-after-frees": (
        WRITE,
        "",
        RETURN,
        "\ndoquotaupd:",
    ),
    # A clean flag word makes iupdat() return before it writes.
    "flags-not-marked": (
        WRITE,
        WRITE.replace(FLAGS, ""),
    ),
    # A delayed write leaves the inode in the cache while the frees run.
    "inode-write-delayed": (
        WRITE,
        WRITE.replace("&time, &time, 1)", "&time, &time, 0)"),
    ),
    # A failed inode write is followed by the frees all the same.
    "write-error-ignored": (
        WRITE,
        WRITE.replace(CHECK, "        (void)error;\n"),
    ),
    # A failed inode write leaves the shortened inode in core.
    "failed-write-not-restored": (
        WRITE,
        WRITE.replace(RESTORE, ""),
    ),
    # A kept indirect block is written behind the frees of its entries.
    "indirect-write-async": (
        INDIRECT,
        "        if (aflags & B_SYNC) {\n",
    ),
    # An unlinked inode is freed although its truncation freed nothing.
    "unlinked-inode-freed-anyway": (
        "            if (ip->i_size == 0) {\n",
        "            {\n",
    ),
    # A failed write of a kept indirect block leaves no cleared copy.
    "indirect-not-requeued": (
        "                    bdwrite(bp);\n                }\n",
        "                    brelse(bp);\n                }\n",
    ),
    # A failed inode write still zeroes the tail of the last kept block.
    "tail-zeroed-on-failure": (
        TAIL_RELEASE,
        "            if (tailbp) {\n"
        "                bzero (FSBADDR(INODE_FILESYSTEM(oip), tailbp, bn)"
        " + offset,\n"
        "                    (u_int) (IN_BSIZE(oip) - offset));\n"
        "                bdwrite(tailbp);\n"
        "            }\n",
    ),
    # The final update repeats the write the early one made.
    "second-write-kept": (
        RETURN,
        "\ndoquotaupd:",
    ),
}


def mutate(source: str, name: str) -> str:
    pairs = MUTATIONS[name]
    for old, new in zip(pairs[0::2], pairs[1::2]):
        count = source.count(old)
        if count != 1:
            raise SystemExit(f"{name}: expected one anchor, found {count}")
        source = source.replace(old, new, 1)
    return source


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mutation", required=True, choices=sorted(MUTATIONS))
    arguments = parser.parse_args()
    text = arguments.source.read_text(encoding="utf-8")
    arguments.output.write_text(mutate(text, arguments.mutation), encoding="utf-8")


if __name__ == "__main__":
    main()
