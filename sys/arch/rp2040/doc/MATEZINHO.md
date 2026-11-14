# MATEzinho: bounded PicoCalc desktop and development profiles

MATEzinho is an independent, MATE-inspired desktop shell. It uses original BSD
3-Clause implementation code and dark styling, the existing public-domain
Misc-Fixed 6x9 font, and a separately licensed MATE icon pack. It does not link
MATE, GTK, a window server, or Mint widget-theme code.

## Execution paths

| Path | Runs | What it establishes |
| --- | --- | --- |
| Native development | Same C state machine and band renderer; Linux files and a Tk display wrapper | Fast interactive UI development; host filesystem browsing and note saving |
| QEMU application | Same C desktop compiled for ARMv6-M, newlib and a small Linux EABI shim; cortex-m0 CPU | Thumb-1 execution, linked RAM envelope, output parity and file-backed artwork reads |
| Renode PicoCalc | Real PICOCALC kernel, 264 KiB SRAM platform, existing SPI1 ST7365P and I2C keyboard, added SPI0 FileSD | Kernel/device/application integration through the production SPI SD protocol and LCD graphics ABI |
| Physical PicoCalc | Native OMAGIC program and the same asset format on SD or a file | Board qualification remains separate; no hardware flashing or serial access is implicit |

Do not connect two independently executing emulators and call their combined
state a single accurate machine. QEMU and Renode are complementary test tiers.
The out-of-tree QEMU raspi-pico RFC is an additional future whole-kernel target,
not a prerequisite for this desktop. The existing RP2040 Renode carrier is the
whole-system path already supported in this repository. Neither is cycle-accurate.

Upstream research sources checked for this implementation:

- https://github.com/matgla/Renode_RP2040 -- third-party RP2040 models; the tree
  pins 205a5e4b25440582008a4292074bb07f80a72328 and applies its existing patches.
- https://github.com/wokwi/rp2040js -- alternative functional Pico emulator;
  flash-write support remains a required discriminator for this Dhara root.
- https://lore-kernel.gnuweeb.org/all/20260829234308.33725-1-gilles.grimaud%40univ-lille.fr/
  -- RP2040 QEMU RFC, functional test-oriented model; not a claim of a booted
  DiscoBSD system. docs/research/qemu-thumb1-test-tier.md records the later
  RFC v3 investigation already performed for this repository.

## Storage is separate from SRAM

The real Pico keeps its 2 MiB physical flash and 264 KiB SRAM. The virtual ROM
is a read-only **external data volume**, not 16 MiB of new executable address
space, a RAM disk, or a change to the kernel's fixed flash partition layout.
FileSD reads one 512-byte sector at a time from a host file. The desktop opens
its pack read-only and caches one aligned 1024-byte block. A larger volume
changes neither buffer size.

| Profile | Logical bytes | Representation |
| --- | ---: | --- |
| compact | 19,456 | Minimal pack for a regular file on guest storage |
| rom16 | 16,777,216 | Sparse asset volume, host mode 0444, Renode read-only |
| sd32 | 33,554,432 | Sparse writable development card or read-only asset volume |
| sd32gb | 31,999,918,080 | Approximately 32 decimal GB, rounded down to a CSD capacity unit |

`storage.py --kind assets` puts the pack at byte zero. Such an image is a raw
artwork volume, not a FAT/UFS filesystem. `--kind blank-sd` creates an empty
card for later partitioning/formatting. Both operations refuse an existing
output. Asset builds also create a companion `.artwork` directory containing
the pinned source icons and complete notices; distribute it with the volume. Sparse allocation is a host-filesystem property; copying to real media
requires its logical bytes. No image creation command writes a physical device.

Small FileSD cards use SD v2 standard-capacity byte addresses and CSD v1.
Cards above 2 GiB use block addresses and CSD v2. The model covers CMD0, 8, 9,
16, 17, 24, 55, 58 and ACMD41; initial command CRCs, ready/idle, data tokens,
read-only refusal, bounds and a short busy response are modeled. It does not
model wear, power interruption, electrical signals, CRC16 validation or analog
timing. Unsupported commands report illegal-command status. Reset retains the
backing bytes. Dispose releases the backing file.

A 32 GB card is not a promise of a single 32 GB DiscoBSD filesystem. Existing
signed 32-bit file offsets and the compact UFS format remain constraints. The
PICOCALC and PICOCALC_PSRAM kernels set NMOUNT=2, the flash root plus one SD
partition, so a second SD filesystem cannot mount at the same time. Raw device
I/O above 2 GiB requires separate kernel work. The model's last-sector tests
exercise the SD protocol directly and do not bypass that distinction. For the
initial PicoCalc image, use the raw read-only card as artwork storage and keep
notes on the writable root; a compact pack can later live in an independently
qualified SD filesystem. Do not overwrite a populated physical SD card with a
raw artwork image merely to install icons.

### Shared card with the vendor FAT32 partition

Requirement: DiscoBSD keeps its own 16 MiB carve-out of the SD card, while
the same card keeps the vendor's FAT32 partition and the other firmware
images on it. The storage profiles above do not yet meet it. Each profile is
a whole-card image without an MBR: an asset image holds the pack at card byte
zero and is read through `/dev/rsd0` (minor 0, the whole card), and Renode's
FileSD presents the image file as the whole card. On a vendor card, sector 0
holds the MBR, so `matezinho /dev/rsd0` finds no `MZAR0001` header and exits,
and writing an asset image to the card replaces the MBR and the FAT32 volume.

`sd.c` maps minors 1 to 4 to the four primary MBR entries without a type
filter and offsets each transfer by the entry's first sector, and the reader
issues aligned 1024-byte reads. A 16 MiB entry beside the FAT32 entry, with
the pack written at its first byte and opened as `/dev/rsd0b` (or `rsd0c` or
`rsd0d` after `mknod`), therefore matches the reader by source inspection.
No tier exercises that layout, no tool here shrinks the vendor partition or
writes a pack at a partition offset, the carve-out's MBR type is unassigned
(0xB7 marks a DiscoBSD UFS partition and 0xB9 the crash-dump partition), and
the `matezinho` closure names only `/dev/rsd0`.

## Artwork format and provenance

The header is a 1024-byte block. Bytes 0..7 are `MZAR0001`; bytes 8..11 are
9 icons, width 24, height 24 and 2 bytes per pixel. Bytes 12..15 give the logical
volume capacity in 512-byte sectors, unsigned little-endian. Remaining header
bytes are reserved zero. Icon i starts at `1024 + 2048*i`; each of its 24 rows
has 24 little-endian RGB565 pixels. Record padding is zero. The native reader
uses aligned 1024-byte reads so `/dev/rsd0` satisfies its raw-I/O alignment rule.
No icon or full volume is loaded into user RAM.

The selected nine PNGs, editable SVGs, generation-rule files, upstream AUTHORS
and COPYING are in `share/matezinho/mate/upstream`. `provenance.json` pins their
Git blobs and SHA-256 values at mate-icon-theme revision
15df93ae1d5fbea067eb3d9d5d2a8b3b4a5cbec4. The pack selects the upstream
CC BY-SA 3.0 alternative. The source PNGs/SVGs are unchanged; alpha flattening
and RGB565 quantization happen reproducibly at build time. No moving network
fetch is needed to build a pack. The builder verifies every retained source
hash before conversion.

Distribute `share/matezinho/mate/README.md`, AUTHORS, COPYING, provenance.json
and the CC legal text alongside generated packs and screenshots. Source releases
retain editable assets. The artwork does not acquire the original C code's BSD
license. A distribution including the optional artwork is mixed-license.

## Desktop behavior and limits

The screen has a 22-row top panel, a 3x3 launcher grid between panels, and a
22-row bottom taskbar. The display remains exactly 320x320 at every wrapper
zoom. Eight-row RGB565 rendering uses 5120 bytes. The shell stores managed view
state and reconstructs dirty bands; it does not retain a user framebuffer or
read old LCD pixels. One process owns the graphics device.

Nine launchers are present: Files, Notes, System, Calculator, Settings, Clock,
Console, Help and Storage. The taskbar represents open managed views in stable
application order; Tab cycles them and Ctrl-Tab invokes an MRU chooser. Release
Ctrl to commit. The native development wrapper also maps Alt-Tab/Alt release.
The physical input adapter uses the controller's documented Ctrl key, not a
claimed dedicated Alt key.

Notes retain up to 1023 bytes, use append/backspace editing, and save via a
same-directory exclusive temporary file then rename. Existing content is loaded
at start. A failed write leaves the note in memory and the previous file in
place. A pre-existing `.new` file blocks saving rather than being overwritten.
This is not a rich editor; document scrolling and arbitrary cursor movement are
not implemented. Files lists at most 12 non-hidden names of at most 31 bytes;
Enter opens a selected directory and Backspace goes to its parent. There is no
file mutation or arbitrary file viewer. The QEMU newlib seam deliberately has
no directory API; native/board backends supply it.

Calculator accepts `a + b`, `-`, `*` and integer `/`, with operands in
[-32767,32767], and rejects division by zero and invalid trailing text. It does
not replace the existing scientific `calc` program. Settings toggles larger
headings; it does not claim that all text has been enlarged. Clock and the
live desktop clock show elapsed shell-session time; wall time is not invented.
The board currently derives elapsed seconds from the existing time API; changing
the system clock affects this counter. A monotonic syscall is separate work.
System and Storage report defined capacities, not fabricated CPU or battery
telemetry. Battery polling is absent from the UI loop.

Console is an exclusive handoff on the board: close the graphics device,
restore the tty, fork/execute `/bin/sh`, wait, reopen display and raw keys, and
reconstruct the desktop. It is not a persistent graphical terminal or PTY.
Ctrl-Q quits; Ctrl-W closes the active managed view; Ctrl-S saves Notes; Esc
returns to the launcher; F1 toggles Applications and F5 refreshes Files.
A lost key queue clears modifier/chooser state. A revoked session does not
restore another foreground owner's tty modes. Last graphics close restores
the kernel text console.

## Reproduction

Install the repository's normal build dependencies, plus Pillow for asset
conversion, QEMU user emulation/newlib for ARM execution, Tk for the interactive
wrapper, and .NET 8/Renode only for the respective optional tests. Set PYTHON
in the caller. From the repository root:

```sh
export PYTHON=python3
bmake MACHINE=rp2040 logged-build
bmake MACHINE=rp2040 check-matezinho-host
bmake MACHINE=rp2040 check-matezinho-qemu
bmake MACHINE=rp2040 check-matezinho-sd

# Fast native or ARMv6-M QEMU desktop (keyboard, no mouse dependency).
"$PYTHON" tools/matezinho/desktop.py --backend host --profile sd32
"$PYTHON" tools/matezinho/desktop.py --backend qemu --profile sd32gb

# Make a new read-only external artwork volume.
"$PYTHON" tools/matezinho/storage.py artwork16.mzar --profile rom16
# Or an empty writable card; it contains no filesystem yet.
"$PYTHON" tools/matezinho/storage.py development32gb.img --profile sd32gb --kind blank-sd

# An optional root profile includes the original shell and device nodes,
# excludes the native toolchain to keep flash headroom, and does not embed artwork.
bmake MACHINE=rp2040 BOARD=PICOCALC PROFILE=matezinho logged-distribution
bmake MACHINE=rp2040 BOARD=PICOCALC PROFILE=matezinho logged-flash
```

`NEWLIB_INCLUDE` and `NEWLIB_LIB` optionally locate an extracted toolchain's
newlib headers and ARMv6-M libraries for the development builder; a normal
system installation needs neither. `HOST_CC` and `ARM_CC` select compilers.
The QEMU linker file reserves a 192 KiB region including a bounded 16 KiB heap
and 8 KiB stack, plus bounded resident argument storage. This is a linked memory envelope, not an emulated MPU guard;
CPU/system timing and kernel memory remain Renode/board questions.

For Renode, use the existing fetch/build setup, then provide the external
artwork path and include `tools/renode/matezinho.resc`. That script loads the
PICOCALC machine, adds FileSD on SPI0 with GP17 chip select, and opens the image
read-only. It does not start servers or flash hardware. From the logged-in
PicoCalc console:

```sh
matezinho /dev/rsd0 /home/operator/note.txt
```

The model-driven regression command is:

```sh
"$PYTHON" tools/renode/check-matezinho.py \
  --flash distrib/rp2040/flash-PICOCALC.bin \
  --assets artwork16.mzar \
  --reference tools/matezinho/out/reference.ppm \
  --output tools/matezinho/out/renode
```

Generate reference.ppm with the host executable and the same artwork, passing
`0` as its final argument for a single initial frame. The gate boots, logs in,
launches through I2C keys, compares the Files card against the reference with
the RGB666 quantization tolerance, and requires SPI SD reads. The outputs are
ignored build products. This gate is optional, distinct from host/QEMU tests.
