# Research documents kept with the port

This directory holds the documents the port's code, Makefiles and root
manifest cite as the authority for a shipped mechanism:

- audit-findings.md and audit-response.md -- the read-only audit ledger and
  the response that records how each ranked item was resolved or deferred.
- float-libs.md and board-libc.md -- the Boot ROM float provider contract and
  the board libc composition (distrib/rp2040/mkboardlibc.py).
- benchmarks.md -- the CoreMark port and reporting contract.
- utilbox.md, posix-tools.md, v7-tools.md, games.md, menu-shell.md,
  rockbox-ui.md -- what each multicall binary in distrib/rp2040/mi.rp2040
  carries and why.
- zswap.md and emulation.md -- the RAM-tier swap design and the Renode flow.
- sh-posix-audit.md -- the bin/sh POSIX ledger whose rows bin/sh/tests/posix-sh.sh
  tests and whose xfail cases pin the open items.

The parent `doc` directory also carries `CAPACITY.md`, the authority for the
generated metadata representations, fixed-table counters, and u-area
watermark that the metadata generator, `sys/kern/subr_capacity.c`, and
`sbin/sysctl` implement.

Every other note written during the port (option surveys, backport and size
audits, tuning reports, handback notes, USB and storage investigations)
lives in docs/research at the top of this tree. docs/INDEX.md maps both
directories.
