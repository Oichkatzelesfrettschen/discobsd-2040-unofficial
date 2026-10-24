# RP2040 ioctl input payload ownership

## Accepted boundary

The `ioctl(2)` syscall now copies every input or input/output payload accepted
by the shipped PICO and PICO_UART providers into typed, aligned kernel storage
before provider dispatch. The change covers these exact command values:

| Payload | Commands | Target bytes |
| --- | --- | ---: |
| `int` | `FIONBIO`, `FIOASYNC`, `FIOSETOWN`, `TIOCSETD`, `TIOCFLUSH`, `TIOCLBIS`, `TIOCLBIC`, `TIOCLSET`, `TIOCSPGRP` | 4 |
| `struct sgttyb` | `TIOCSETP`, `TIOCSETN` | 6 |
| `struct tchars` | `TIOCSETC` | 6 |
| `struct ltchars` | `TIOCSLTC` | 6 |
| `char` | `TIOCSTI` | 1 |
| `struct winsize` | `TIOCSWINSZ` | 8 |
| `off_t` | `TFALLOC` | 4 |

One automatic union owns the copied bytes. Its member types supply each
provider's required alignment without a shared buffer or a maximum-size
automatic array. The syscall copies `TFALLOC` output back only after provider
success. A provider state transition remains committed when a later copyout
fails, which preserves the temporary-swap allocation contract.

The classifier compares the complete command. A reserved-bit variant or an
unknown command still reaches its provider with the original pointer and
complete command value. This preserves provider-specific rejection and avoids
inventing a size limit for commands that no shipped provider accepts. A new
accepting provider or command must extend the classifier and its fixture before
the finite shipped-provider guarantee applies to that payload.

Syscall 54, its four-argument dispatch record, descriptor and access error
precedence, eight-bit payload geometry, zero-length admission, provider
selection, command values and public layouts remain unchanged. `FIOGETOWN` and
`FIONREAD` retain their previously accepted result staging.

## Behavioral and compiler evidence

The production-path fixture now executes 185 assertions. The inherited source
fails 46 of the added assertions. The cases cover every accepted input type,
odd caller addresses, complete copy length, aligned provider storage,
unchanged input-only caller bytes, copyin refusal before dispatch, `TFALLOC`
success, provider error and copyout failure, exact command identity, output
publication and unknown-command forwarding.

Ten warning-clean mutations restore direct `FIONREAD` access, unconditional
`FIONREAD` publication, a short result copy, unmasked encoded size,
first-and-last-address admission, direct accepted-input access, a one-byte
input copy, reserved-bit-stripped classification, unconditional `TFALLOC`
publication or omitted `TFALLOC` publication. Each exits with assertion status
1 at its named falsifier. The existing file-owner and file-status mutations
were updated to remove their command from common staging before restoring the
older direct-pointer behavior; their original rejection contracts still pass.

The fixture passes as strict C17 on the host, at ILP32 width, and under address,
undefined-behavior and alignment sanitizers. Both generated kernel
configurations compile and link under their exact GNU17 Cortex-M0+ commands
with warnings as errors. The complete `logged-check` receipt records a
successful build and non-hardware check tier. These results establish the
exercised host behavior and target compilation; they do not execute an ioctl
on RP2040 silicon.

## Static resources and dependency closure

Clean schema-2 distributions compare parent
`a028337a3c8e50e6f8eaa6a06089ef291247b6de` with implementation commit
`101823d26adf66292a9598165ac6e7189750ef4a`. Both captures use bmake
20260912, arm-none-eabi-gcc 16.2.0 and GNU ld 2.47.20260726. Their dependency
closures account for 173 PICO C/header paths, 165 PICO_UART paths, all 36
shipped artifact groups, the native startup object and libc archive, generated
inputs and the packed root.

| Surface | Parent | Candidate | Delta |
| --- | ---: | ---: | ---: |
| PICO flash | 105,134 bytes | 105,342 bytes | +208 bytes |
| PICO_UART flash | 92,660 bytes | 92,868 bytes | +208 bytes |
| PICO `.bss` / SwapRAM / scratch | 15,272 / 16,384 / 8,192 bytes | 15,272 / 16,384 / 8,192 bytes | 0 / 0 / 0 |
| PICO_UART `.bss` | 14,832 bytes | 14,832 bytes | 0 |
| Native `libc.a` | 43,010 bytes, 151 members | 43,010 bytes, 151 members | 0 |
| Packed-root data/free blocks | 692 / 287 | 692 / 287 | 0 |
| Packed-root free inodes | 33 | 33 | 0 |

Every shipped a.out measurement, native-artifact size, archive selection and
archive-member set is unchanged. Exact recorded compiler commands replayed
with `-fstack-usage` attribute the linked increase to `sys_generic.o` and
`ioctl()`: object text grows from 2,132 to 2,340 bytes and the function symbol
from 436 to 644 bytes in both configurations. Object data remains zero,
object BSS remains eight bytes, and the `ioctl()` static frame remains 24
bytes.

The default resource comparer correctly rejects the two positive flash deltas.
The bounded exception accepts 208 bytes per kernel because exact private input
ownership removes alignment-sensitive provider dereferences across all shipped
accepted input commands while preserving provider authority for unknown
commands. The candidate retains 25,730 PICO and 38,204 PICO_UART bytes below
the 128 KiB kernel region. Runtime stack high-water, heap use and storage
traffic remain unmeasured.

The evidence records have these SHA-256 identities:

- aggregate logged build and check: `37f8b09bd9748d0973b00cbfc78ff3a2bdf05f3d732e38e3f8a3fb8654667460`
- parent source snapshot: `d78270228f1fb3c0af2fce1fb0a52f2ffd471be459c8fbac81b50ce5e49ed0f1`
- parent dependency closure: `e88b9b2412a53e3eba40634653e907e25d3acd1552ff7f071f1c0e10b338d0bf`
- parent schema-2 resources: `f96a1aa7e42cce664234d7100d541577280f35a83433af44bd88478a5a729d2f`
- parent object and frame: `b4deea3c341609909acc4c2d86fe2ca8874c2f9375c79233735bd3cd75bed774`
- candidate source snapshot: `741e6da71d6af1b2a6106a5e617c9dcc8c5c7255f5f615901e7dcb111cef37fb`
- candidate dependency closure: `ae3f9c7566496fe96f5687f814d468f593e56661f813292c49e10bc4759ac743`
- candidate schema-2 resources: `251303aa80ac0a3fcfcf69ff6b7ed963a072dbebe58754b39d9a1f6f4b37bac8`
- static-resource comparison: `22aaf693f838bb49b354f192fd40a863500a8b94b5ae379fa0ce71b085645250`
- candidate object and frame: `6e33c03e82a5748b6c9f6b7811fa2989d4cdbd1288fac6596de06269446d7981`
- object and frame comparison: `8edcf7603928f470865acd7708db62c6f20eb1f60b374b4d2d7f3953b2a4c035`

## Residual boundaries

Output-only tty, flash and swap commands other than `FIOGETOWN` and
`FIONREAD` still let providers write through caller-selected addresses after
span admission. Kernel-internal ioctl callers, optional providers, tty
local-mode signed arithmetic, `/dev/mem`, `ptrace`, runtime resource peaks,
MPU fault behavior and physical-board execution retain separate migration or
evidence states.
