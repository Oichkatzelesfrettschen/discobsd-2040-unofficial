# Driving the board console from a host script

A board driver under `tests/rp2040/*/board_*.py` logs in on the USB
CDC-ACM console, runs shell commands and stages test a.outs as uuencoded
text. Four limits on that path decide whether a driver works, and each one
below names the source that sets it, what a driver observes when it is
crossed, and the rule a driver follows. `tests/rp2040/user_window/
board_user_window.py` implements every rule.

## The shell line editor caps a command line at 126 bytes

`bin/sh/edit.c` defines `ED_MAX` as 126 (a 128-byte `fbuf` less the newline),
and `editline()` limits every interactive line to it. `insert_text()` rings
the bell and discards each byte past the cap, and Enter still submits the
truncated line. A driver that appends `; echo SENTINEL` to a command over the
cap loses the sentinel and waits for output that never comes: a 188-byte
`cat` of seven named parts stalled this way.

Rule: keep every typed command, including its sentinel, at or under 126
bytes. Join many files with a glob over zero-padded names
(`cat /tmp/NAME.p* > /tmp/NAME.uu`), not an argument list.

## Every typed byte redraws the whole prompt line

`editline()` redraws the prompt and the edited line after each byte it reads,
so a command typed one byte at a time produces output roughly proportional to
the square of its length. A run that staged an a.out as one
`echo 'LINE' >> FILE; echo SENTINEL` command per uuencoded line (107 to 119
bytes each) completed 42 commands and stalled on the 43rd, with the console
showing the complete command echoed and no shell response. The capture held
about 1,300 redrawn lines longer than 100 bytes. The resource that runs out
is not yet identified. The stall differs from the console wedge in
`docs/research/invariant-map.md` section 3, where no endpoint answered at
all; here the board echoed the whole command and then stopped responding.

Rule: carry data through a program that reads the terminal directly, never
through command lines, and keep the number of typed commands small. A
command of 20 to 40 bytes costs little.

## Streamed input past one chunk loses bytes

`cat > FILE` reads the terminal in canonical mode, where the line editor is
not involved, and `sys/kern/tty.c` flushes the raw queue when it exceeds
`TTYHOG` (255 bytes). Streaming all 130 lines (about 8 KB) of an a.out into
one `cat` at 3 ms per byte and 50 ms per line delivered 52 lines; the rest,
including the terminating `end` line and the Control-D, were lost, and the
next command was typed into `cat`'s input.

Rule: send uuencoded data in parts of 20 lines (about 1.2 KB), pause before
the Control-D that ends each part, then read the part's size back with
`wc -c < PART` and resend a part whose count differs. Seven parts of 1,199
bytes each matched on the first attempt in every run that used this rule.
Join the parts, `uudecode` the result, and compare `cksum` with the host's
before running the program.

## Read command output only up to the sentinel

The shell prints its prompt after the sentinel line, and a parser that keeps
the lines after the sentinel takes the prompt as output: a byte count read
back from `wc -c` parsed as -1 because the last line was the prompt.

Rule: append `; echo __DO''NE__` (the quotes keep the echoed command from
matching), wait for a line that is exactly `__DONE__`, and split the output
there. Take the result from the lines between the echoed command and the
sentinel.

## Facts a driver cannot read from the board

The PICO root ships no `dmesg` and the kernel builds no message log
(`distrib/rp2040/mi.rp2040`, `LOG_ENABLED` unset), so boot lines are visible
only to a reader attached before the kernel prints them. `discobsd-flash`
returns before the console re-enumerates, so a driver opening the port after
it misses the boot banner. `sysctl hw.usermem` reports `freemem`, not the
window size (`sys/kern/kern_sysctl.c`). Check the window from user mode
instead: `mputest` (`usr.bin/mputest`) faults on the first kernel RAM word
above `USER_DATA_END` and reads the window's top word, and a test program
whose image only a large enough window admits proves admission.

## Test data must defeat SwapRAM compression

A process image the scheduler or fork swaps out goes first to the SwapRAM
tier, which stores it heatshrink-compressed in a 16-KiB pool
(`sys/arch/rp2040/rp2040/swapram.c`). A board test that needs an image to
reach flash swap must fill it with data that does not compress. A 100-KiB bss
written with a regular byte pattern forked successfully while `talloc` held
280 of the 380 flash swap blocks; the likely reason is that the child's copy
compressed into the pool and never asked flash for space. That run is not a
controlled comparison: the run that refused the fork changed both the data
(xorshift32, which zlib at level 9 leaves at 100.04 percent of its size) and
the hold (every grantable extent on three units), and a fork with regular data
under the full hold would decide it. `tests/rp2040/fork_refusal/forktest.c`
uses xorshift32 so the test does not depend on that answer.

## Hold flash swap with the largest grantable extents

`TFALLOC` (`sys/dev/swap.c`) frees a unit's previous extent before it
allocates the new one and needs one contiguous, `SWAP_IMAGE_ALIGN`-aligned
run, so the extent a unit can hold depends on the free runs the boot left,
which differ between boots: 280 blocks were granted on one boot and none of
300, 280, 260 or 240 on the next. A binary search per unit, each probe one
short `talloc` command, finds the largest grantable extent; three units
(`/dev/temp0` to `temp2`) held 220, 70 and 22 blocks and left no run large
enough for a 105-block image. Re-request the best size after the search,
because a failed final probe leaves the unit empty.
