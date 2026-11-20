# uLisp as a C17 program for DiscoBSD on the PicoCalc

## Finding

The PicoCalc's stock uLisp (uLisp PicoCalc Release 4.8f, an Arduino sketch
that runs on bare metal) ports to a DiscoBSD program. `usr.bin/ulisp` is that
port: a C17 interpreter that links against the tree's libc and libm into a
51,800-byte OMAGIC a.out, 53,860 bytes resident, and leaves a default
workspace of 14,672 cells in the 192 KiB process window of a kernel booted
under Renode. The claim that the port cannot be done is refuted by
construction for every evidence class below the board; no board run has
been made, and the last section lists what that leaves open.

Source revision: `technoblogy/ulisp-picocalc` commit
`c031b63c00b1e55ef64e556fd3372e38ed0ab062`, `ulisp-picocalc.ino`, 7,793
lines, SHA-256
`65608bb7d6a48dc02dd38f67d10a826bc7a60456e106c4b7212a2dd0483ebd8b`, MIT
license. Toolchain: arm-none-eabi-gcc 16.2.0 with the tree's flags
(`-std=gnu17 -Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft`), host gcc
with `-std=c17`, qemu-arm with the `cortex-m0` model.

## Candidate blockers and their outcome

| Candidate | Outcome | Evidence |
| --- | --- | --- |
| License | MIT; the notice is kept in `ulisp.c` and NOTICE section 19 | source header, lines 1-4 |
| C++ source | The interpreter uses no class, template or overload of its own. The C++ it does rely on is implicit: the float overloads of `<math.h>`, `abs` on a float, default `int` enumerations and anonymous unions in two Wi-Fi helpers | host build under `-std=c17 -Wall -Wextra -Werror` |
| 192 KiB window | 53,860 bytes resident: text and read-only data 51,528, `.data` 240, `.bss` 2,092 | `arm-none-eabi-size` on the linked ELF |
| Workspace | 14,672 cells of 8 bytes with a 24 KiB stack reserve (14,736 under qemu with an empty environment); upstream has 22,280 on a Pico and 14,510 on a Pico W | the first prompt under Renode |
| Execution | The kernel executes the a.out and the interpreter runs at its console | `check-renode-ulisp`, `check-ulisp-qemu` |
| Machine code | `defcode` output executes from the process's data under the kernel's MPU map | `(add1 4241)` under Renode; `tests/ulisp/code.lisp` under qemu |

About 36.7 KiB is the interpreter (`ulisp.o`), 1.7 KiB the POSIX layer,
1.6 KiB the drawing code, 0.7 KiB the device layer, 0.9 KiB the font and
about 10 KiB libc, libm and startup code. The link map names no stdio and
no `malloc` member: the program uses file descriptors alone, and reads a
directory through a static stream because `opendir()` would take its
stream from `malloc()` and move the break into the span kept for the
stack.

## What an earlier estimate got wrong

An estimate made from an unlinked object file preceded this work. Three of
its statements did not survive measurement:

- It gave `.text` as 28,250 bytes. The linked program has 51,528 bytes of
  text and read-only data, about 36 KiB of it the interpreter proper. The
  total it predicted (53-58 KiB with the documentation external) was
  nonetheless right.
- It named `/dev/adc*` as the path for `analogread`. The RP2040 kernel has
  no ADC device node (`distrib/rp2040/mi.rp2040` lists none and
  `sys/arch/rp2040/dev/adc.c` has no read entry), so `analogread` reports an
  error.
- It called the 64-bit host port a matter of the `type` field and six casts.
  Strings pack four characters in a 32-bit word of the cdr slot; a 32-bit
  store leaves the upper half of a 64-bit slot undefined, and a 64-bit local
  breaks the shift-out loop of the string comparison. Both failures appeared
  only in execution.

## Mechanisms the port adds

**Workspace sized at startup.** DiscoBSD places the break and the stack in
one window and `brk` refuses only what crosses the stack's low-water mark
(`sys/kern/kern_mman.c`). `sys_init()` measures the span between the break
and its own frame, keeps 24 KiB for the stack and takes the rest as cells;
`-w cells` asks for fewer and thereby leaves more stack.

**Documentation outside the process.** The 240 documentation strings are
25,611 bytes, 13% of the window. `builtins.def` holds every builtin as one
line (name, function, argument counts, documentation); `ulisp.c` expands it
into the enumeration and the lookup table, and `mkdoc.c` expands it on the
build host into `/usr/share/ulisp/ulisp.doc`, which `(documentation)` and
`(?)` read by index. A static assertion fails the build when a line named in
the enumeration is out of place.

**A collector that uses no stack.** Upstream's `markobject` recurses on every
car. Nothing separates the stack from the cells here, so a deep structure
would have the collector write its own frames over the heap it is marking.
The port's `markobject` reverses pointers (Deutsch, Schorr and Waite): the
way back is held in the cells under trace, with one more bit beside the mark
bit, which the 8-byte cell alignment leaves free.

**Bounded recursion everywhere else.** Upstream checks the stack in `eval`
alone. The compiler's call graph (`-fcallgraph-info`) with the indirect
calls resolved by hand shows twelve further functions on a cycle that does
not pass through `eval`: `apply`, `mapcarcan`, `read`, `readrest`,
`printobject`, `pslice`, `buildarray`, `rslice`, `subwidth`, `superprint`,
`equal` and `edit`. Each now starts with the same check. The deepest path
from a checked frame through unchecked functions is 384 bytes by
`-fstack-usage`; the margin below the limit is 3 KiB, which also has to hold
the libc, libm and system-layer frames and a signal frame, none of which
the figure includes and none of which is measured. The
cycles that remain in the graph pass through `error2`, which does not
return.

**Relocatable images.** An image is the cells with their absolute
addresses. The header records the address they were saved from, and the
load rebases them. Character cells of strings and long symbols are not
distinguishable from pairs by content, so the load marks every chain from
its header first and rebases the rest.

**Faults as errors.** SIGSEGV, SIGBUS and SIGILL raise a Lisp error, so a
wrong `defcode` body or a `register` access the MPU refuses returns to the
prompt. Two faults in a row are caught on the host and, raised by the MPU,
on the kernel under Renode.

## Defects the comparison found

The port's transcript is compared with that of the unmodified upstream
source built for the host (`tests/ulisp/oracle`). The comparison and the
target run found these, in order:

1. Upstream's error paths in `ignore-errors` and `unwind-protect` keep
   locals across `setjmp` without `volatile`; the C compiler reports them.
2. `pow(10, n)` truncated to an integer in the float printer: `3.5` printed
   as `3.49997` on the target. `lib/libm/pow.c` computes
   `exp(y * log(x))`, and a probe linked against it found the result
   unequal to the exact product for every n from 1 to 8 under the qemu
   loader. The port computes the power by exact multiplication. Whether
   the board's ROM float routines give the same `pow` results is
   unmeasured; the loader models them.
3. The reader accepted end of file as a symbol character and filled its
   buffer with 0xff bytes when a file ended in a symbol.
4. The reader looped without end on an unterminated list read from a string
   or a file.
5. An error raised while `princ` runs left the print-readably flag clear for
   the rest of the session.
6. Negative coordinates were stored in unsigned 16-bit parameters, so a
   shape that starts off the panel vanished instead of being clipped.

7. Three signed overflows the undefined-behavior sanitizer reports: the
   radix-40 packing of a symbol, the character shift in `concatenate` and
   the exponent of a number with many digits. They are unsigned or bounded
   now.
8. On the kernel, the first run stopped with "not enough memory": this
   libc's `brk()` returns the new break, where the port expected zero. The
   qemu loader returned zero and hid it; the loader now returns what the
   kernel does and the port asks through `sbrk()`.

9. A review of the port found edge inputs that end the process or are
   undefined, each held by `tests/ulisp/repairs.lisp` (or `devices.lisp`
   for the drawing ones) and the sanitizer pass of `tests/ulisp`:
   `(random 0)` and a negative limit divided by zero (now a Lisp error);
   `rem` and `mod` of INT_MIN by -1 raised SIGFPE (now 0); `(- INT_MIN)`
   and printing INT_MIN negated in `int`; array dimensions multiplied
   before any bound, so `'(65536 32768 2 2 2)` wrapped to an empty array
   and 1500000000 elements overflowed the power-of-two rounding (now
   "no room"); `get-key` copied terminal state after a failed query
   (no observable difference; the copy is now inside the success branch);
   and drawing geometry summed ints, so `(fill-rect 100 100 2147483647 1 c)`
   drew nothing (all geometry is now 64-bit and clips; a triangle with a
   corner beyond 2^24 is not drawn, as a line that long is not walked).
   Upstream's `random` and `mod` share the first three; upstream's
   `mod` also returns -1 for `(mod 5 -1)` and -3 for `(mod 6 -3)`, where
   Common Lisp returns 0; the `ulisp/cl-conformance` branch repairs that
   (see `ulisp-common-lisp-conformance.md`).
10. The image loader trusted the saved pointers. `checkimage()` now reads
   the file before any cell reaches the workspace and refuses a pointer
   outside the image, a string chain that loops or is shared, a pair or
   root that points into a string chain, and a code block, stream or
   builtin symbol outside its table. The `images` corpus loads 27 copies
   of a real image, each with one pointer or cell damaged by
   `corrupt-image.py`, and requires each refusal with the workspace
   unchanged. An array's tree is checked against its dimensions by the
   `ulisp/cl-conformance` branch (see `ulisp-common-lisp-conformance.md`).

11. A second review and a sweep of every integer builtin found more
   undefined or fatal edges. `tests/ulisp/gen-arith.py` applies each
   builtin that takes a number to the edge values of a 32-bit integer (all
   pairs, a sample of larger tuples, floats past the int range, number text
   for the reader, floats at the limits of the printer): 12686 forms in the
   `arith` corpus, run under the address, undefined-behavior and
   float-cast sanitizers. The reports, now fixed: `(/ INT_MIN -1)` raised
   SIGFPE in the remainder (the pair is checked first); `ceiling`,
   `floor` and `truncate` divided by zero and by -1 in `int` (a Lisp
   error, and a float for INT_MIN / -1); `truncate`, `floor`,
   `ceiling` and `round` converted floats beyond the int range (the
   result is the float itself, as `+` does on overflow); `ash` and
   `logbitp` shifted by 32 or more, by a negative count, or a negative
   value left (counts of 32 or more clear the bits or leave the sign, the
   left shift keeps the low 32 bits, a negative `logbitp` index is an
   error); `expt` squared past the int range; `note` summed octaves in
   `int` and indexed the scale with a negative remainder (a negative note
   counts down); `sleep` multiplied seconds in `int`; `delay` and
   `for-millis` turned a negative count into a wait of about 49 days or
   more (now no wait); strings built from a byte above 127 and `setf` of a
   character or bit shifted signed values. Not a sanitizer report but kept:
   `char` and `setf` of `char` with a negative index read the position
   the low bits named (now an error). Upstream's `1+`-style printing of
   -Inf as `Inf` is left. `register` (the address is the argument) and
   `gc` (a flag, a timed report) stay out of the corpus. The loader also
   checks the cells it installed, not only the file it read first (the
   `race` and `truncrace` corpora change the file between the two reads
   with an LD_PRELOAD shim on Linux), and `initworkspace()` zeroes the
   free count; a top-level error recovery recounts it, so no transcript
   shows that last defect and no mutant holds it.

Item 3 reproduces in the unmodified upstream program built for the host
(a file ending in a symbol reads back with a tail of 0xff bytes). Items 4
to 6 are read from the upstream source and were not run there. One
difference is kept on purpose and
held by `tests/ulisp/repairs.lisp`: upstream prints `1e-4` as `10.0e-5`,
because its exponent is the floor of a double quotient that falls just
below the integer; the port's float quotient prints `1.0e-4`.

## What does not map

- `with-i2c`, `with-spi`, `with-serial`, `restart-i2c`, the Wi-Fi
  functions, `analogreference` and the two resolution functions are
  absent: the kernel exposes none of those devices, and the resolutions are
  fixed at upstream's defaults of 10 bits in and 8 bits out.
- `set-rotation` and `invert-display` are absent.
- `analogread` reads GP26-GP29 through `machdep.adc.N` as a 10-bit
  value, upstream's default, from the kernel's 12-bit count. On a PicoCalc
  the kernel refuses GP26 and GP27 (the audio amplifier) and the function
  reports "no analogue input device", the message it also gives when the
  kernel lacks the node; GP28 is the expansion header's ADC2. `analogwrite`
  sets a duty of 0-255 on pins GP0-GP29 through the GPIO device's PWM
  request, a 1.9 kHz carrier; the kernel refuses the pins the board holds,
  and on a PicoCalc only GP28 can be driven, on the PICOCALC_SD kernel
  alone (the `DEV_GPIO_PWM` option) and not while `/dev/audio` is open, reported as "no analogue output device". The
  pins need the superuser. The host model is a loopback; the ADC and PWM
  paths have run on no board.
- `note` selects a tone that sounds while `delay`, `for-millis` or `sleep`
  waits, since `/dev/audio` takes samples and has no free-running tone.
- The pins need the superuser, and the PicoCalc leaves GP28.
- `read-pixel` and `save-bmp` need the superuser (`LCDG_READ`).
- Opening `/dev/lcdgfx` takes the panel from the text console. The port
  opens it at the first drawing call and closes it when the next line of
  input arrives, so a drawing stays until a line is typed.
- The screen editor's line editing, parenthesis highlighting and completion
  belonged to upstream's own terminal emulator; the port reads lines from
  the tty driver.

## Evidence classes and what is open

| Class | What ran |
| --- | --- |
| Host, 64-bit and 32-bit | every corpus; thirteen mutants rejected; the image reload under address-space randomization |
| Upstream oracle on the host | the `language` corpus, 310 lines, equal byte for byte |
| Host under ASan and UBSan (`make -C tests/ulisp check`), and built with clang | every corpus, including the damaged images |
| qemu-arm, linked a.out | `language`, `system`, `images`, `repairs`, `stack`, `code`, image reload |
| Renode, PICO_UART kernel | `check-renode-ulisp`: 34 stages over UART0, listed in `tools/renode/check-ulisp.py` |
| Board | nothing |

Unmeasured, and needing a PicoCalc with the SD root: the drawing functions
through `/dev/lcdgfx` and the hand-over of the panel between text and
graphics; the keyboard through the console tty, including `get-key`; sound;
the pins; the SD root itself, since Renode booted the flash root; the ROM
float routines on silicon; and timing of any kind.

Two observations are open. In one Renode session the shell printed
`status=2000` for `$?` after an interpreter that had caught an interrupt
ended at end of input; a session without the interrupt reported 0, and the
gate asserts the statuses of four other exits. The cause is not found.
And no published test suite for uLisp was found among the upstream
author's GitHub repositories (listed through the API on 2026-11-18); the
web site was not searched page by page. The corpora name all but a
handful of the 248 table entries: `time`, `edit`, `get-key`,
`*features*`, `*pc*` and `closure` are not exercised by name.
