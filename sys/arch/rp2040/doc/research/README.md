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

Every other note written during the port (option surveys, backport and size
audits, tuning reports, handback notes, USB and storage investigations)
lives in the rpi notes repository under research/discobsd-rp2040/, which is
the canonical home for research on this port.
