Clocks per instruction for i86 -S: the speed.asm loop on 8088, 8086, 80186 and 80286
=====================================================================================

What it settles: the tenths of a clock an instruction takes, per processor,
that `i86 -S` paces the guest by (`usr.bin/i86/main.c`, `-c`). Class of
evidence: Intel's published clock counts (specification), per-instruction
cycle traces captured from real chips (silicon, one instruction at a
time), and 86Box's model (another implementation). The figures are those of
one loop; other instruction mixes run at other rates on the real chips.

The loop: the inner loop of measure/speed.asm, 11 instructions, 28 code
bytes, the loop the board measurement (board-speed-pico.txt) ran. nasm and
ndisasm give 8B 87 d16 (mov ax,[bx+buf]), 01 C8, 89 87 d16, 31 C5, D1 E0,
83 D5 00, 43, 81 E3 FF 00, E8 rel16 to a lone C3, C3, E2 rel8 (taken).
buf is at 7C51h and BX steps by one, so the two word operands are at odd
addresses on every other turn; the stack is even.

Sources (sha256, URL):
  9800722-03 The 8086 Family User's Manual, Oct 1979: table 2-20 (EA,
    p. 2-51: displacement + base = 9) and table 2-21 (pp. 2-52 to 2-68);
    the 8088 adds 4 clocks a word transfer, the 8086 4 a word at an odd
    address.
    717194ec03520d6499a40523abdfd4ed15a2b8ccc45a21c653e2b443c248664c
    https://www.bitsavers.org/components/intel/8086/9800722-03_The_8086_Family_Users_Manual_Oct79.pdf
  iAPX 86,88 User's Manual, 1981: the same values.
    3eea6ca77ad4046ae7ade731410793206eebe8ec9a3f8ae75895685d38f4ffe5
    https://www.bitsavers.org/components/intel/8086/1981_iAPX_86_88_Users_Manual.pdf
  210911-001 iAPX 86/88, 186/188 Programmer's Reference, May 1983: 8086 and
    80186 columns, the 8088 as n + 4t (p. 3-39), EA p. 3-41, rows pp. 3-48
    to 3-170.
    7f33bead2b7111b6871b50f14368531b90c30d960ca47865c5b0d8aa5fb55ed2
    https://www.bitsavers.org/components/intel/80186/210911-001_iAPX86_88_186_188_Programmers_Reference_1983.pdf
  210451-002 iAPX 186 data sheet (preliminary), Dec 1982, pp. 47-52; its
    counts assume even word addresses (p. 47).
    b96e770ffa1d81e8182e79acae0746758412d048e489fc81a5a3f7f232aedae3
    https://www.bitsavers.org/components/intel/80186/210451-002_iAPX186_Datasheet_Dec82.pdf
  210498-005 80286 and 80287 Programmer's Reference Manual, 1987: clock
    rules p. 8-6 (2 more a word at an odd address), rows pp. 8-23 to 8-116.
    ad487ba99b48cd9f61b14c0fe912a04c7cdb4c7c14a18419aa9faf62d8962460
    https://www.bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf
  210498-001 iAPX 286 Programmer's Reference, 1983, appendix C: CALL 7+m,
    RET 11+m (C-47), LOOP 8+m (C-48), m the bytes of the next instruction
    (C-42).
    367e1834e2f46397047084dffe1de88f65a1c115795b65ba26d1609995983c8f
    https://www.bitsavers.org/components/intel/80286/210498-001_iAPX_286_Programmers_Reference_1983.pdf
  Silicon: SingleStepTests/8088 v2 (AMD D8088, commit aea84484),
    SingleStepTests/8086 v1 (Intel P80C86A-2, e71c68d2), SingleStepTests/80286
    v1_real_mode (Harris N80C286-12, 37c73caf); the test files of the
    loop's opcodes. Each test is one instruction from a known queue state.
    SingleStepTests/80186 holds only a README.
  86Box at fafa9de4: src/cpu/808x.c for the 8088 and 8086 (and the 80186,
    which it times as the 8086), the 386 core's 80286 charges.
  Agner Fog's instruction tables (2025-09-20) begin at the Pentium; these
    processors appear only in their glossary.

Clocks for one turn of the loop:

  processor  Intel tables              silicon                    86Box
  8088       113 (1979), 122 (1983)    126 from a full queue,     156
                                       178 from an empty one
  8086       97 (1979), 106 (1983);    105 even operands,         127 even,
             +8 with odd operands      113 odd, 109 alternating   131 alternating
  80186      85 (both documents)       none                       (as 8086)
  80286      48, 55 with m;            traces start from a        47 to 63
             57 alternating            flushed queue: lower
                                       bounds only, all met

The 8088 moves 36 bytes a turn over its byte-wide bus (28 of code, 2 each
of load, store, push and pop) at 4 clocks a byte: 144 clocks, more than any
execution sum above, so the bus sets the floor.

Where the documents disagree, silicon decides: RET near is 8 clocks in the
1979 tables and 16 in the 1983 reference, and the 8086 takes 16 (the 8088
20); INC r16 is 2 in 1979 and 3 in 1983, and both chips take 2. Silicon is
one clock above the tables on the 8088's memory operands, as the 1983
reference allows (the execution unit waits up to a clock for the bus,
p. 3-39). 86Box is not a tie-breaker here: it charges the 01 and 31 register
forms 11 clocks where the 8088 takes 8, and CALL, RET and LOOP 4 to 6 low.

The figures i86 uses (tenths of a clock an instruction, -c argument):

  8088  131  the bus floor, 144 / 11; the loop's true time lies between
             144 and 178 clocks (13.1 to 16.2), undecided until a
             continuous trace of the loop on silicon exists
  8086   99  silicon, alternating operands, 109 / 11
  186    77  Intel's 85 / 11, even operands; odd operands, by the bus
             mechanism, would give about 89 (8.1); no silicon exists
  286    52  Intel's 55 plus 2 for the alternating odd operand, 57 / 11

A 4.77 MHz 8088 thus runs this loop at about 364,000 instructions a
second; the measured 0.32 million on a Pico at 125 MHz is about 0.88 of
that.

Reproduce: the worker's scripts and outputs (sst_stats2.py,
adhoc_8086_alignment.py, moo.py, s8088.json, s8086.json, s80286.json,
rerun.sh, and an 86Box harness that includes the unmodified 808x.c) are
kept outside the tree with the downloaded files; the PDFs are filed in the
owner's technical documents repository.
