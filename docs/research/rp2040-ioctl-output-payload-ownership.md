# RP2040 ioctl output payload ownership

## Accepted boundary

The `ioctl(2)` syscall now gives every output-only command accepted by the
shipped PICO and PICO_UART providers typed, aligned kernel storage before
provider dispatch. The accepted set is finite:

| Payload | Commands | Target bytes |
| --- | --- | ---: |
| `int` | `TIOCGETD`, `TIOCLGET`, `TIOCGPGRP`, `TIOCOUTQ`, `DIOCGETMEDIASIZE` | 4 |
| `struct sgttyb` | `TIOCGETP` | 6 |
| `struct tchars` | `TIOCGETC` | 6 |
| `struct ltchars` | `TIOCGLTC` | 6 |
| `struct winsize` | `TIOCGWINSZ` | 8 |
| `struct diskpart` | `DIOCGETPART` | 16 |

The existing automatic ioctl union now includes `struct diskpart`, the largest
accepted payload at 16 bytes with four-byte target alignment. `ioctl()` clears
the union before dispatch, passes its aligned address to the provider, and
publishes exactly the encoded payload width through `copyout()` only after
provider success. A provider that succeeds without writing therefore publishes
zero-valued bytes from initialized storage rather than kernel stack contents. A
provider error suppresses publication even when the provider changed its
private payload first. A later copyout failure preserves the provider's
completed state transition.

The classifier compares complete command values. Reserved-bit variants,
`TIOCMODG`, `TIOCMGET`, socket commands, and commands accepted only by optional
providers retain their original pointer and provider-specific rejection path.
A new accepting provider or command must extend the classifier and calibrated
fixture before the finite shipped-provider guarantee applies.

Syscall 54, its argument record, public command values and layouts,
descriptor/access/span precedence, provider selection, provider errors,
`FIOGETOWN`, `FIONREAD`, accepted input staging, and `TFALLOC` publication
remain unchanged.

## Behavioral and compiler evidence

`tests/kernel/fionread_test.c` compiles the exact production `ioctl()` and
`ino_ioctl()` definitions against bounded kernel-header shims. The candidate
passes 338 assertions at native and ILP32 widths and under address,
undefined-behavior, and alignment sanitizers. The inherited implementation
fails 60 of the 311 assertions it reaches after the output cases are added.

The fixture covers all ten exact output commands, odd caller destinations,
typed provider alignment, complete copyout widths, adjacent canaries, provider
failure after a private write, copyout failure after provider success,
deterministic zero output after a successful provider writes nothing, exact
upper-bound success, crossing-span rejection, reserved-bit variants, and an
unaccepted modem command. Existing geometry, input, input/output, file-owner,
file-status, and `FIONREAD` cases remain active.

Fifteen warning-clean mutations calibrate the combined fixture. Five added
mutations restore a direct output pointer, unconditional output publication, a
one-byte output copy, omitted output publication, or nonzero initialization.
All fifteen mutations terminate at their named assertion. Both generated
kernel configurations compile and link under their exact GNU17 Cortex-M0+
commands with warnings treated as errors. The host fixtures establish the
exercised ownership and failure contracts; target compilation does not execute
an ioctl on RP2040 silicon.

## Static resources and dependency closure

Clean schema-2 distributions compare parent
`6c67ebe664d1d61ed875dc50bc74908da65107bb` with implementation commit
`baa4f41d537125ec2f3a6ab5ed938ee89cde3729`. Both captures use bmake
20260912, arm-none-eabi-gcc 16.2.0, and GNU ld 2.47.20260726. Their dependency
closures account for 173 PICO C/header paths, 165 PICO_UART paths, all 36
shipped artifact groups, the native startup object and libc archive, generated
inputs, and the packed root.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PICO flash | 105,344 bytes | 105,520 bytes | +176 bytes |
| PICO_UART flash | 92,868 bytes | 93,036 bytes | +168 bytes |
| PICO `.bss` / SwapRAM / scratch | 15,272 / 16,384 / 8,192 bytes | 15,272 / 16,384 / 8,192 bytes | 0 / 0 / 0 |
| PICO_UART `.bss` | 14,832 bytes | 14,832 bytes | 0 |
| Native `libc.a` | 43,010 bytes, 151 members | 43,010 bytes, 151 members | 0 |
| Packed-root data/free blocks | 692 / 287 | 692 / 287 | 0 |
| Packed-root free inodes | 33 | 33 | 0 |

Every shipped a.out measurement, native-artifact size, archive selection, and
archive-member set is unchanged. Exact recorded compiler commands replayed
with `-fstack-usage` attribute the implementation increase to `sys_generic.o`
and `ioctl()`: object text and the function symbol each grow from 2,308 to
2,480 bytes and from 644 to 816 bytes, respectively, in both configurations.
Object data remains zero, object BSS remains eight bytes, and the `ioctl()`
static frame grows from 24 to 32 bytes.

The default resource comparer correctly rejects both positive flash deltas.
The bounded exception accepts 176 PICO bytes, 168 PICO_UART bytes, and the
eight-byte static-frame increase because private output ownership removes
alignment-sensitive provider stores, premature caller-memory changes, and
uninitialized output across the complete shipped accepted set. The candidate
retains 25,552 PICO and 38,036 PICO_UART bytes below the 128 KiB kernel limit.
Runtime stack high-water, heap use, and storage traffic remain unmeasured.

The complete non-hardware aggregate invocation
`rp2040-check-local-20261024T025554Z-1921003-52589d07` ran the required clean
build and check stages with `PYTHON=/usr/local/bin/python3`. The build stage
completed in 48.063 seconds, the check stage completed in 364.260 seconds, and
both exited zero. The immutable aggregate status receipt covers 412.325
seconds and records neither termination nor dropped terminal output.

The evidence records have these SHA-256 identities:

- parent source snapshot: `d4f603aeb9412f8f608a659479d6f912731018ef32d493ba4d696d02989550e2`
- parent dependency closure: `93b87e5043c471571a2f316c11bd9fe82ec1d5177c48908f3ba14a6ead824f3a`
- parent schema-2 resources: `9b01beb8174e6b3f28fc9e40845f424bea3e82a264bc5c18e04cd2b5eacf68b6`
- parent object and frame: `ba04c986777cab4bfb144c85916e0171393c254c432667a64ce7521c96592f64`
- candidate source snapshot: `190015a1572b333c9705a027b9c7586150dbab7b1ba8cb64b4ef5bb4ecc3ce99`
- candidate dependency closure: `c56b8866f0695c36da2bd103d13e75352f6acc918423a1720933c36287a28ca5`
- candidate schema-2 resources: `99465b31abd83b1c2df754816bb903dc5c281d0838d5cb30e059b1446523e817`
- static-resource comparison: `9464b3bd2498e0e01e7b321c6f31f1ceccb493cc200b1c79176023796ffdaf87`
- candidate object and frame: `67cb7d471996e2ab3b7be101249a12740e0863d88e2676b500d32140ba9cb0cc`
- object and frame comparison: `9dcaa358c92410d3f265e6ea696b1a4456654b96ba0913f6def8fcf95691f476`
- aggregate `logged-check` status: `7ba5a6386001c8b57dd836aba89f4c18cf994d8c449111d939d81cdfcc397568`

## Residual boundaries

Kernel-internal ioctl callers and optional providers retain their own
caller-ownership contracts. Tty local-mode signed arithmetic, `/dev/mem`,
`ptrace`, runtime resource peaks, MPU fault behavior, and physical-board
execution remain separate migration or evidence states. The retained hardware
corpus applies only to its recorded revision and workload.
