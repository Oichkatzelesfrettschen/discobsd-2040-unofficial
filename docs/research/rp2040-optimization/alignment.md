# RP2040 byte-alignment policy

## Decision

The public stack and call boundary and the existing kernel section boundaries
stay 8-byte aligned. A blanket 16-byte rule is not introduced. Four bytes is
the baseline for 32-bit kernel scalars, pointers, word-copy loops and typed
payloads. One- or two-byte alignment applies to an object whose access
contract is correspondingly narrow, or which is decoded through copies rather
than cast to a wider type.

The target is an ARMv6-M Cortex-M0+ build using Thumb-1 and the soft-float
AAPCS (`-mcpu=cortex-m0plus -mabi=aapcs -mthumb -mfloat-abi=soft`, set in
`share/mk/architecture.mk`). The core has no vector or hardware floating-point
load/store width for a 16-byte policy to serve. C17 changes neither the
hardware nor the ABI.

## Granularities are different contracts

| Granularity | Use on this target | Admission rule |
| ---: | --- | --- |
| 1 byte | character data, octet streams, encoded on-flash and wire layouts | access as bytes, or copy and decode into an aligned object before typed access |
| 2 bytes | naturally aligned 16-bit arrays and generated halfword tables | every address is even and no consumer performs a wider typed load |
| 4 bytes | pointers, 32-bit integers, MMIO words, `.data` startup copying, most kernel state | default for ordinary 32-bit objects and private ioctl staging |
| 8 bytes | AAPCS stack and call boundary, existing linker section ends, 64-bit objects, stacks that can enter C | retained; lowering it is not an accepted size optimization |
| 16 bytes | no general CPU benefit on RP2040 | requires a named peripheral, encoded layout or measured loop |
| 256 / 1,024 / 4,096 bytes | flash program page, Dhara logical page, chip erase sector | storage geometry only; never a C object alignment |

`STACKALIGN` is 8 in `sys/kern/exec_subr.c`. `sys/arch/rp2040/conf/kern.ldscript`
ends `.text`, `.data`, `.ramfunc`, `.bss` and the SwapRAM pool on 8-byte
boundaries, aligns the `.data` load address and `_sidata` to 4 bytes (ARMv6-M
faults on an unaligned word load before any handler can exist) and the
scratch area to 4. `lcd_render_stack` in `sys/arch/rp2040/dev/lcdcons.c` is
`aligned(8)` because it is a C stack. These are retained contracts, not
padding to remove opportunistically.

## Where narrower alignment can pay

The useful experiment is local hole removal, not an ABI change:

1. Obtain `sizeof`, `_Alignof` and `offsetof` for a candidate under the exact
   RP2040 compiler. Inspect its arrays and every typed consumer.
2. Reorder members or separate a byte-oriented cold payload from word-oriented
   hot state. A copy-and-decode boundary is preferred over `packed`, which can
   turn one saved byte into repeated byte assembly or an invalid typed access.
3. Add `_Static_assert` checks for size, offsets and minimum alignment at the
   owner. Public and on-media layouts also need an encode/decode
   compatibility test.
4. Compare the linked maps and final images of every kernel configuration. A
   source-level padding reduction that does not reduce `.data`, `.bss`, the
   flash image or a bounded pool is not a target win.
5. Run the relevant host, Thumb-1 and board gate. Interrupt, DMA and MMIO
   buffers also need a device-specific alignment proof.

Candidates are byte-only name and message pools, generated 16-bit offset
tables (`syscall_name_spans` in `sys/kern/syscalls.c` is one) and structures
with cold character arrays between hot words. The alignment of `struct user`,
saved contexts, stacks, MMIO objects, function pointers and arbitrary ioctl
payload storage stays as it is.

The `struct user` reorder (`b9e9c02a`, `a0c2b80f`) is the worked example. It
moved `u_comm` and the three `label_t` saves behind the hot fields, so
`u_arg` starts at offset 8 and the last group credential at 120, inside the
124-byte Thumb-1 `ldr` immediate reach that `sys/sys/user.h` asserts on
Thumb-1 targets. The object stays 1,024 bytes (`UAREA_STACK_OFFSET`) with
`u_stack` last, so alignment stays correct and instruction count falls.

## Linker ordering by alignment

`ld --sort-section=alignment` groups input sections by alignment and removes
inter-section holes. On `main` at `d81b1788` (GCC 16.2.0, GNU ld, `-Os`) it
shrinks loaded text plus data by 69--96 bytes and BSS by 40--176 bytes in each
of six configurations, and it lowers no object or section alignment (see
`../kernel-footprint-pr435-closeout.md` for the table and method). The flag
is not enabled in any production link.
