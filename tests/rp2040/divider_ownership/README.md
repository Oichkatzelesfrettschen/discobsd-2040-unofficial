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

`--user-helper OBJECT` checks the libc division helpers the kernel gate leaves
to user code. The Makefile assembles `lib/libc/arm/gen/aeabi_div.S` for
Cortex-M0+ and requires `user_helper_entries=2 sio_accesses=8
min_wait_cycles=8 user_helper_findings=0`, then `user_helper_mutants.sh`
rebuilds the source with a branch removed from the wait, the result loads
swapped, a CSR poll in a wait slot, an `svc`, a `bl`, a `push` and a `bkpt`
inside the window, a wrong base constant and a helper symbol removed; each must
exit 1 with its own finding. The window rule rests on the kernel switching
processes and delivering signals only on the return from a system call or
fault, which the kernel gate's no-consumer result and `MPU.md` argue.

Each production helper alias also carries a Thumb entry and a positive ELF
function extent. The verifier derives each extent from the next distinct
entry or the text boundary and requires both aliases to share that owner.
`helper_extent_mutants.py` changes only symbol-table fields and requires six
named rejections: a missing extent, a short extent, overlap into the next
owner, escape past text, an alias redirected to another valid owner, and a
cleared Thumb bit. Unique `.build-logs/divider-extent-controls-*` directories
retain each object, diagnostic and terminal receipt. The production fixture
contains no literal pools; `DIVIDER_MODEL` arithmetic remains a separate gate.
The [extent record](../../../docs/research/xip-shared-libc/divider-function-extents.md)
owns the model-pool and native-archive identity measurements.

Run the integrated kernel gate from the repository root after selecting
the RP2040 machine:

```sh
PYTHON=/usr/bin/python bmake MACHINE=rp2040 MACHINE_ARCH=arm check-divider
```

The integrated target builds PICO, PICO_UART, PICOCALC and PICOCALC_PSRAM,
scans their linked ELFs, and scans the source/header closure recorded in their
`.deps` directories. The
gate checks offline ownership. Interrupt injection and result preservation
remain attended hardware tests.
