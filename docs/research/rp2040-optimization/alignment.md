# RP2040 byte-alignment policy

## Decision

Retain 8-byte alignment at the public stack/call boundary and at the existing
kernel section boundaries. Do not introduce a blanket 16-byte rule. Treat
4-byte alignment as the ordinary baseline for 32-bit kernel scalars, pointers,
word-copy loops and typed payloads. Use 1- or 2-byte alignment only for an
object whose access contract is correspondingly narrow or which is decoded
through copies rather than cast to a wider type.

This is an ARMv6-M Cortex-M0+ build using Thumb-1 and the soft-float AAPCS
(`-mcpu=cortex-m0plus -mabi=aapcs -mthumb -mfloat-abi=soft`). The core has no
vector or hardware floating-point load/store width for a 16-byte policy to
serve. C17 does not change those hardware or ABI facts.

## Granularities are different contracts

| Granularity | Appropriate use on this target | Admission rule |
| ---: | --- | --- |
| 1 byte | character data, octet streams and encoded on-flash/wire layouts | access as bytes, or copy/decode into an aligned object before typed access |
| 2 bytes | naturally aligned 16-bit arrays or generated halfword tables | prove every address is even and no consumer performs a wider typed load |
| 4 bytes | pointers, 32-bit integers, MMIO words, `.data` startup copying and most kernel state | default for ordinary 32-bit objects and private ioctl staging |
| 8 bytes | AAPCS stack/call boundary, existing linker section ends, 64-bit objects and stacks that can enter C | retain; lowering it is not an accepted size optimization |
| 16 bytes | no general CPU benefit on RP2040 | require a named peripheral, encoded layout or measured loop that needs it |
| 256/1,024/4,096 bytes | flash program page, Dhara logical page and chip erase sector respectively | storage geometry only; never infer C object alignment from it |

`sys/kern/exec_subr.c` already uses `STACKALIGN 8`. The RP2040 linker script
keeps `.text`, `.rodata`, `.data`, `.ramfunc`, `.bss` and the SwapRAM pool on
8-byte boundaries while using 4-byte alignment for the startup data load and
scratch area. The startup comment records why `_sidata` must support aligned
word loads. `lcd_render_stack` is explicitly 8-byte aligned because it is a C
stack. These are retained contracts, not padding to remove opportunistically.

## Where narrower alignment can pay

The useful experiment is local hole removal, not changing the ABI:

1. Obtain `sizeof`, `_Alignof` and `offsetof` for a candidate under the exact
   RP2040 compiler. Inspect its arrays and every typed consumer.
2. Reorder members or separate a byte-oriented cold payload from word-oriented
   hot state. Prefer a copied/decode boundary over `packed`; `packed` can turn
   one saved byte into repeated byte assembly or an invalid typed access.
3. Add `_Static_assert` checks for size, offsets and minimum alignment at the
   owner. Public/on-media layouts also need an encode/decode compatibility
   test.
4. Compare all four kernel maps and final images. A source-level padding
   reduction that does not reduce `.data`, `.bss`, the flash image or a bounded
   pool is not a target win.
5. Run the relevant host, Thumb-1 and board gate. Interrupt/DMA/MMIO buffers
   additionally need the device-specific alignment proof.

Candidates include byte-only name/message pools, generated 16-bit offset
tables and structures with cold character arrays between hot words. Do not
lower alignment of `struct user`, saved contexts, stacks, MMIO objects,
function pointers or arbitrary ioctl payload storage.

The `struct user` reorder demonstrates the preferred technique: it preserved
the 972-byte object and its 8-byte call/stack environment while moving hot
word fields into Thumb-1's 124-byte immediate reach. Alignment stayed correct;
instruction count fell.
