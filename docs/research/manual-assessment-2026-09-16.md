# The manual, assessed for a tester and for a buyer

What exists, in the port tree at fcc4161a:

| Document | Lines | Audience | Covers |
| --- | --- | --- | --- |
| `README.md` | 420 | tester, buyer, builder | what runs on it, the login screen line by line, per-platform install, troubleshooting, hardware and care and recovery, V6, build and flash, host tools, source tree, gates, references, license and redistribution, other platforms |
| `distrib/rp2040/host/README.md` | 190 | tester | install per OS, LAN console, keys and exits per system, accessibility, development |
| `usr.bin/pdp11/README.md` | 75 | curious tester, builder | the emulated machine, the pack, rebuilding it, testing |
| `sys/arch/rp2040/doc/*.md` | 5 files | builder | boot map, storage, user access, multicall overlay, datasheet index |
| `NOTICE`, `/etc/COPYRIGHT` | | redistributor | every notice; the on-device short form |

For a tester handed a board, the README's "Use a board you were
handed" is complete: install, plug in, probe, terminal, web console,
exits, troubleshooting, V6. Verified against the board on 2026-09-15
and 2026-09-16 from headless Firefox and the serial console.

For a buyer of a pre-installed board, what was missing on
2026-09-15 and what happened:

| Gap | Status |
| --- | --- |
| hardware facts, power, care, data location | added to README "Hardware, care and recovery" (PR #80) |
| recovery of a board that does not answer | added, same section |
| where to ask for help | added, same section |
| license and redistribution terms | added: NOTICE, /etc/COPYRIGHT, README section (PR #80) |
| which build is on the board | `/etc/release` generated at image build with commit, date and builder (PR #81) |
| one printable document for a box or a listing | open: the README is the manual; a PDF or a single page derived from it is a packaging task, not a documentation gap |
| screen-reader runs on real assistive technology | open, recorded in web-console-ux.md |
| V6 clock from the host | open, recorded in v6-emulator-on-discobsd.md |
