# calc: scientific calculator for the PicoCalc console

Status: `usr.bin/calc` implements rows 3, 4 and 5 of the staged import plan in
`picocalc-calculator-and-gui.md` (engine with infix, RPN and programmer modes;
units and statistics; item bar). usr.bin/menu lists calc with every other
command on the PATH, so it needs no /etc/menu line. Host, sanitizer, tree-libm,
qemu-user and mutation evidence is recorded below. No board run and no
execution of the DiscoBSD a.out has happened. Every figure is marked measured
(a named command produced it) or derived.

## Result

- `calc` evaluates infix expressions over `double` with correct precedence
  (`^` right-associative and tighter than unary minus, so `-2^2` is `-4`),
  41 functions, 17 NIST CODATA 2022 constants, `ans`, ten memory registers,
  16 named variables, degree and radian mode, and a nesting bound of 16
  that is rejected with `error: nested too deeply`. Division by zero,
  domain, overflow and syntax errors print one `error:` line and keep the
  session, the variables and `ans`.
- RPN mode keeps a 16-level stack with `swap dup drop over roll rollu sto rcl
  stack clr`; programmer mode evaluates C-precedence integer expressions over
  an 8, 16 or 32 bit word and prints hex, decimal (with the signed value when
  the top bit is set), octal and binary.
- `conv` converts between 73 units in 11 dimensions and four temperature
  scales from NIST SP 811 factors; `stat` and `reg` use `lib/libcalc` for sum,
  mean, median, extrema, variance, standard deviation and correlation, and
  compute the least-squares line.
- On a terminal (stdin and stdout both terminals) the program draws a ten-item
  bar on the last row, selects items with F1 to F10 (PicoCalc and xterm
  sequences), keeps a scroll region above the bar and restores the terminal on
  every exit path. With a pipe, `-l`, or expression arguments it is line
  oriented and writes no escape sequence.
- Measured size of the cross build: 39,140 bytes raw, 31,014 packed, 32
  root blocks (`hsaout -s`), about twice the plan's 20 KB estimate. The
  evidence section itemizes the cause.

## Interface

| Input | Meaning |
| --- | --- |
| `2+3*4`, `x = 7`, `m3 = x^2`, `ans` | expression, assignment, memory register, previous result |
| `+ - * / % ^ !`, unary `+ -`, `( )` | `%` is C `fmod`; `!` is postfix factorial of 0 to 170 |
| `0x1F`, `0b101`, `0o17`, `1.5e-3` | literals |
| `sin cos tan asin acos atan atan2 sinh cosh tanh asinh acosh atanh exp ln log10 log2 sqrt cbrt abs floor ceil round trunc frac sign pow mod hypot fact torad todeg` | functions; trigonometry follows `deg` and `rad` |
| `sum mean median min max stdev stdevp var varp` | list functions of 1 to 32 arguments |
| `pi e c0 planck hbar qe kB NA Rgas sigsb g0 Gn me mp eps0 mu0 alpha` | constants; `const` lists them |
| `deg rad angle`, `alg rpn prog`, `bits 8\|16\|32`, `digits 1..17` | state |
| `conv 100 degC degF`, `units` | unit conversion and the table |
| `stat 2 4 4 4 5 5 7 9`, `reg 1 2 3 ; 2 4 5` | statistics over blank or comma separated expressions |
| `help`, `vars`, `clr`, `stack`, `quit` | housekeeping |
| `-r`, `-p`, `-d`, `-l`, `expr ...` | start in RPN or programmer mode, in degrees, in line mode, or evaluate arguments and exit |

Item bar, last row, 53 columns: `1Help2Alg 3RPN 4Prog5Rad 6Unit7Stat8Cnst9Clr
10Quit`. Each digit is drawn in normal video and each label in reverse video;
the item of the current mode is drawn in normal video and item 5 names the
current angle unit. F1 runs `help`, F2 `alg`, F3 `rpn`, F4 `prog`, F5 `angle`,
F6 `units`, F7 `stat`, F8 `const`, F9 `clr`, F10 `quit`.

## Design

- **Error path.** `fail()` stores a message and calls `longjmp` to the
  `setjmp` in `run_line()`, so no evaluator function carries an error return.
  RPN operations compute before they commit, so a failing operation leaves the
  stack as it was.
- **Nesting bound.** `unary()` and `punary()` count their entries in `depth`;
  every parenthesis, call argument, prefix sign and exponent link passes one
  entry. The seventeenth nested level fails. The line-mode table pins the
  boundary on all four paths: 16 levels evaluate and 17 fail.
- **Binding.** `unary` calls `power` and `power` calls `unary` for its
  exponent. A prefix minus therefore binds looser than `^` (`-2^2` is `-4`,
  as in Python) and `2^-2` and `2^3^2` parse as written.
- **Numeric contract.** The ROM double routines return an infinity where IEEE
  arithmetic returns a NaN (`lib/libcalc/mathematics.h`). `fin()` tests every
  result as `x - x == 0`, which holds for finite values and fails for an
  infinity and for a NaN on both providers; divisors, logarithm and root
  arguments are checked before the operation.
- **Degrees.** Trigonometric arguments in degrees reduce modulo 360 first, and
  multiples of 90 return exact values (`sin(180)` is `0`, `tan(90)` is a
  domain error). Inverse functions use `asin(x) = atan2(x, sqrt((1-x)(1+x)))`
  and `acos(x) = 2 atan2(sqrt(1-x), sqrt(1+x))`, which are accurate over the
  whole domain and avoid the tree libm `acos`, whose negative arguments
  `lib/libcalc/calc_internal.h` records as wrong.
- **Output formatting.** `fmtnum()` builds the `%.*g` text from `%e`. The
  target's `%g` (`__doprnt_cvt` in `lib/libc/stdio/doprnt_float.c`) counts the
  zeros after the decimal point of a value below 0.1 as significant digits.
  A harness that compiled that file for the host and compared `%.15g` with
  glibc over 200,000 values (measured) found 52,353 differing strings and 320
  that differ by more than 1e-12 relative, up to 4.5e-12; at `%.12g` the
  error reaches 4.5e-9. Its `%e` differed from glibc in 2,037 of 200,000
  values at 15 significant digits, all within 1e-14 relative. `fmtnum()`
  matches glibc's `%.*g` on 180,114 formatted values (measured, host).
  The libc defect is open and outside this change.
- **Output and input.** Output goes through one buffer and `write(2)`, which
  keeps stdio's allocator, `fgets` and buffering code out of the program
  (about 2.4 KB of the a.out, measured) and gives the signal handler and the
  main path one descriptor. Line input reads fd 0 through a 256 byte buffer.
- **Hypot.** `hypot` calls `lib/libm` rather than `calc_magnitude`, which
  removes `calc_vector.o` (1.5 KB) from the link; the tests keep the overflow
  safety (`hypot(1e200,1e200)`).
- **Terminal.** The terminal runs CBREAK (sgtty) or non-canonical termios,
  keeps `ISIG` so `^C` raises SIGINT, and keeps output processing so `\n`
  reaches the console as `\r\n`. `tty_leave()` restores the saved modes and
  writes `ESC [ r`, `ESC [ 0 m`, a move to the last row and an erase of that
  row; the handler for SIGINT, SIGQUIT, SIGHUP and SIGTERM and the `atexit`
  hook both call it. The handlers go in before the mode changes, and the
  restore flag goes up before the mode does, so a signal at any point
  restores a mode that is current. SIGTSTP (^Z under CBREAK) restores the
  terminal, re-raises the stop with the default action, and on continue
  re-enters CBREAK; the input loop redraws the screen at the next key,
  because the screen code is not safe inside a handler. A lone ESC is told from a sequence by a 50 ms wait for
  the next byte (`select`).
- **Units.** `v * f + off` gives the SI value; the target value is
  `(base - off_to) / f_to`. Temperature scales carry the offset, so
  Celsius, Fahrenheit, Kelvin and Rankine round-trip. The factors are the
  exact NIST SP 811 Appendix B.8 values or the definitions behind its rounded
  entries; the constants are the CODATA 2022 values NIST publishes at
  physics.nist.gov/cuu/Constants.

## Evidence

All commands run from the repository root of this worktree with
`export PYTHON=python3`. Source revision: the working tree of branch
`picocalc/calc-app` on top of the `picocalc/calculator-and-games` review fixes.

| Check | Result |
| --- | --- |
| `bmake -C usr.bin/calc test-line` (host, glibc libm) | 686 table cases, 781 checks, pass |
| `bmake -C usr.bin/calc test-line-san` (AddressSanitizer + UBSan) | same table, pass |
| `bmake -C usr.bin/calc test-line-treelibm` (lib/libm sources on host IEEE hardware) | same table, pass |
| `bmake -C usr.bin/calc test-play` (pty, 53 by 32 model) | 13 cases, 163 checks, pass |
| `bmake -C usr.bin/calc check-qemu` (Thumb-1, newlib, `-DCALC_NO_TTY`) | same table, pass |
| `bmake -C usr.bin/calc test-mutants` | 200 mutants tried, 200 rejected, 0 failures |
| `bmake -C usr.bin/calc MACHINE=rp2040 all` | warning free under `-Wall -Wextra -Werror -std=c17 -pedantic-errors -nostdinc` |

Size. `bmake -C usr.bin/calc MACHINE=rp2040 all` followed by
`tools/bin/rp2040/hsaout -s usr.bin/calc/calc` reports 39,140 bytes raw and
31,014 packed, 32 root blocks; the `calc` closure costs 32 free blocks and
one inode in the `picocalc` image (`sys/arch/rp2040/doc/PROFILES.md`). A map
of an earlier build puts about 16 KB in `calc.o` (9.7 KB of code, the rest
the unit, constant and function tables), about 7.3 KB in the libcalc
`stats`, `scalar` and `util` objects, which link whole, about 4.4 KB in
libm and about 5 KB in printf and its float conversion.

The line table compares each expression with Python `float` and `math`
(1e-12 relative), every unit with a factor written from its definition and 32
named conversions with the NIST statements (1e-9 relative), three fixed
vectors and one regression with `statistics` (1e-12 relative), and checks the
RPN stack words, the programmer mode at three word sizes, exit statuses,
argument mode and the table bounds. The pty test reads the F1 to F10 byte
strings from `sys/arch/rp2040/dev/picokbd_map.c` instead of repeating them.

## Not run

- Any execution on the board and any execution of the DiscoBSD a.out. The
  qemu-user run executes Thumb-1 code compiled from `calc.c` and the libcalc
  sources against newlib and libgcc soft-float; it uses neither the boot ROM
  double routines nor the tree libm nor the DiscoBSD tty.
- The sgtty branch of the terminal code compiles for the target and is not
  executed; the host build exercises the termios branch with identical
  control flow.
- The console's response to the scroll-region, save-cursor and reverse-video
  sequences is read from `sys/arch/rp2040/dev/lcdterm.c`, not observed.
