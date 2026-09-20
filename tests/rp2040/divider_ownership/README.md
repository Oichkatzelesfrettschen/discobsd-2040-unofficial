# RP2040 divider ownership verifier tests

The verifier rejects a linked RP2040 kernel when machine code, aligned
literals, or dependency-closed source reaches the per-core SIO divider at
`0xd0000060..0xd000007b`.

Run the fixture gate with a caller-selected interpreter:

```sh
PYTHON=/usr/bin/python bmake MACHINE=rp2040 MACHINE_ARCH=arm check
```

The safe fixture reads and writes non-divider SIO GPIO registers. The three
negative lanes cover immediate-offset, synthesized register-offset,
literal-loaded, cross-basic-block, disconnected residual code, source-symbol,
source-expression, and missing-dependency failures. The negative commands must
exit with the verifier's finding status 1 or input-error status 2, as
appropriate.

`source_comment_safe.c` is the positive control for the source scan: it names
every divider register, the SIO base plus a divider offset, and a direct
divider address, each inside a block comment, a line comment, a trailing
comment or a comment spanning lines, and the verifier must accept it with
`source_divider_findings=0`. A comment emits no instruction, so an access has
to appear in code; `sys/arch/rp2040/rp2040/mpu.c` needs this to state which
registers the boot ROM's `fdiv_n` writes while it programs an MPU region over
them. `source_negative.c` keeps its two findings in code, so the calibration
that the source lane still rejects a real reference is unchanged.

Run the integrated two-kernel gate from the repository root after selecting
the RP2040 machine:

```sh
PYTHON=/usr/bin/python bmake MACHINE=rp2040 MACHINE_ARCH=arm check-divider
```

The integrated target builds PICO and PICO_UART, scans both linked ELFs, and
scans the source/header closure recorded in both `.deps` directories. The
gate checks offline ownership. Interrupt injection and result preservation
remain attended hardware tests.
