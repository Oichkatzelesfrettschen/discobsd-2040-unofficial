# Multicall applet BSS lifetime overlay

Each multicall process dispatches exactly one applet and exits when that
applet returns. Applet-private zero-initialized objects therefore have
disjoint lifetimes even though every applet's code resides in one executable.
The linker may give those objects one shared address extent. Dispatcher and
libc BSS remain live across the call and stay outside the extent.

The build first relocatably links each applet, localizes every definition
except its renamed entry point, and renames that object's `.bss` to
`.app_bss_<applet>`. `tools/generate_multicall_bss_overlay.sh` emits one
NOLOAD output section per applet at `__app_bss_start`, sizes
`.app_bss_extent` to the largest member, and inserts the extent before the
ordinary `.bss`. Initialized data remains in independent `.data` input
sections and retains its bytes. The a.out loader sees one ordinary contiguous
mutable image whose BSS size includes the maximum private extent plus shared
BSS.

The build preserves the following boundaries:

- The dispatcher calls one applet entry point and returns directly to process
  exit; the process never dispatches a second applet.
- Symbol localization prevents one applet from resolving another applet's
  private definition.
- The linker uses `NOCROSSREFS` between private BSS sections and treats every
  linker warning as fatal.
- The generated section retains each input symbol's alignment. The ELF layout
  test includes an eight-byte-aligned member and checks shared virtual
  addresses, maximum extent size, shared-BSS separation, initialized-data
  retention, and external N_BSS symbol conversion.
- A negative ELF fixture places a relocation from one private BSS section to
  another and requires the linker to reject it before conversion to a.out.

The exact RP2040 builds at base `7425dbe3` produced these OMAGIC header
measurements. Mutable bytes are `a_data + a_bss`, the bytes allocated and
swapped for every invocation of that box.

| Executable | Base mutable bytes | Overlay mutable bytes | Reduction |
|---|---:|---:|---:|
| gamebox | 3,932 | 2,192 | 1,740 |
| adminbox | 7,684 | 6,484 | 1,200 |
| box | 11,800 | 10,316 | 1,484 |
| sysbox | 7,336 | 7,132 | 204 |
| textbox | 2,940 | 1,700 | 1,240 |
| utilbox | 9,484 | 5,336 | 4,148 |

The utilbox result includes sort as a nineteenth applet. Sort independently
removes four 256-byte resident classification tables, bounds the merge
pointer array at the seven-way merge fan-in, and allocates a temporary-name
buffer from the selected directory length. Its standalone mutable image falls
from 3,432 to 1,080 bytes. The new sort host test compares the complete byte
domain and supported option modes with the host C-locale sort, forces external
runs, merges 23 files, and verifies temporary-file cleanup after SIGTERM.

Packed-root accounting measures utilbox growing from 38,649 bytes and 39
blocks to 42,275 bytes and 43 blocks. Removing standalone sort removes 9,892
bytes and 11 blocks. The multicall change therefore saves 6,266 packed bytes
and seven 1,024-byte root blocks. The multicall change saves ten blocks in a
raw root image because the raw standalone sort occupies fourteen blocks.

The manifest also activates the packed format for all 33 entries that
`hsaout -s` proves are OMAGIC executables. A base-commit image uses 812 blocks
raw or 676 blocks packed. The overlay-and-sort image uses 802 blocks raw or
669 blocks packed. The completed composition therefore leaves 302 of 971
data blocks free: seven blocks come from multicall consolidation in a packed
image, and 136 come from activating packed installation relative to the raw
base image.

Reproduce the host gates with:

```
bmake -C tests/rp2040/elf2aout_layout check MACHINE=rp2040 PYTHON="${PYTHON}"
bmake -C usr.bin/sort test MACHINE=rp2040 HOST_CC=cc PYTHON="${PYTHON}"
shellcheck -S error tools/generate_multicall_bss_overlay.sh
```

Run a clean RP2040 build before accepting size output. Inspect intermediate
ELF sections and symbols with `arm-none-eabi-readelf -SW` and
`arm-none-eabi-nm -S -n`; use `tools/bin/hsaout -s` for raw, packed, and root
block counts. The board gate must invoke every hard-link name, force sort to
create more than seven runs, and swap each box out and back in before the
production image ships.
