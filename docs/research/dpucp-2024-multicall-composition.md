# DPUCP-2024 multicall and packed-root composition

The RP2040 root ships seven multicall executables with 80 hard-linked applet
names. DPUCP-2024 treats that set as a finite composition surface rather than
as a POSIX or UNIX conformance claim. The machine-readable ledger in
`dpucp-2024-multicall-composition.json` binds the dispatch tables, manifest,
link maps, packed sizes, root arithmetic, and source identities used by the
measurement.

## Finite shipped surface

The selected root contains 32 packed executable inodes and 84 hard links.
Seven of those inodes are multicall boxes, and 80 hard links name their
applets. The verifier reads each box's `PROG`, `TOOLS`, `BUILTIN_TOOLS`, and
`ALIASES` values through bmake and compares the resulting names with
`distrib/rp2040/mi.rp2040`. Built-in applets and aliases own no standalone
code image, so the standalone comparison includes only the 71 primary applet
executables that have a separately linked form.

| Executable | Shipped packed blocks | Primary standalone packed blocks | Saved blocks |
| --- | ---: | ---: | ---: |
| box | 29 | 134 | 105 |
| sysbox | 27 | 68 | 41 |
| adminbox | 25 | 53 | 28 |
| textbox | 33 | 156 | 123 |
| utilbox | 45 | 162 | 117 |
| grepbox | 11 | 18 | 7 |
| gamebox | 12 | 24 | 12 |
| **Total** | **182** | **615** | **433** |

The block comparison measures complete packed a.out images in 1,024-byte root
blocks. It does not infer savings from source line counts or object sizes.

## Static archive closure

The optional `LINK_MAP` seam leaves normal links unchanged and emits a GNU ld
map only when the caller names a destination. The composition gate relinks the
seven boxes and `/bin/sh`, then compares the maps with the committed archive
inventory. The measured eight-consumer closure has 669 consumer edges across
192 distinct libc members: 130 members serve multiple consumers and 62 serve
one consumer. Per-consumer counts are 116 for adminbox, 112 for box, 37 for
gamebox, 46 for grepbox, 57 for sh, 101 for sysbox, 87 for textbox, and 113 for
utilbox.

Static linking shares one selected archive member inside one executable. It
does not share that member across separate process images. Equal private symbol
names in localized applet objects also do not prove equal behavior, state
ownership, or lifetime. The measured result therefore retains seven process
images and the existing BSS overlays instead of admitting unproved
cross-applet or cross-process folding.

## Packed-root delta

Two clean distributions isolate the `/usr/bin/logname` manifest addition.
Both roots contain 95 files, 692 data blocks, and 287 free blocks. The
candidate contains 84 links instead of 83. The hard link points at the existing
utilbox inode and consumes zero additional data blocks or inodes.

The DPUCP `logname` row remains `partial`, with host evidence from
`check-id-aliases` and composition evidence from this gate. Packed-image
execution must still observe session identity, operand rejection, the
missing-login diagnostic, and exit status before the residual can close.

## Verification

Run a clean RP2040 build before the composition gate because libc and public
header changes must reach every linked program:

```sh
: "${PYTHON:?set PYTHON to the intended interpreter}"
export PYTHON
bmake MACHINE=rp2040 build
bmake MACHINE=rp2040 check-multicall-composition
```

The gate relinks consumers sequentially because each multicall build cleans
shared applet directories. It then validates the source identities, dispatch
and manifest denominators, archive inventory, image sizes, root arithmetic,
and eleven calibrated negative mutations. `check-dpucp-profile` separately
rejects removal or weakening of the `logname` compatibility binding.

Runtime peak stack and heap remain unmeasured. Renode execution remains a
separate evidence class. Physical-board execution remains unrun and requires
explicit authorization immediately before flash or serial access.
