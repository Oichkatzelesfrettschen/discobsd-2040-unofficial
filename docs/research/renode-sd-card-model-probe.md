# Renode SD card model as the PicoCalc SD card

Renode's `SD.SDCard` in SPI mode serves as the PicoCalc card under the existing machine
with no model patch. Evidence class: Renode 1.17.0+20260907gitf1dd1b4af, Renode_RP2040
models with patch series through 0008, the PICOCALC kernel and `flash-PICOCALC.bin` built
from this tree (`bmake MACHINE=rp2040 build`, then `BOARD=PICOCALC flash`). Hardware was
not touched.

## Working lines

`SDCard` constructor (read from the `Infrastructure.dll` error report): `imageFile`,
`capacity`, `persistent`, `spiMode`, plus optional block length, compression, others.
`spiMode: true` is required; the class implements `ISPIPeripheral`, so patch 0008 hands
it every SPI0 frame. A repl file loads without error:

```text
sd: SD.SDCard @ spi0
    imageFile: "<absolute path of card.img>"
    capacity: 8388608
    persistent: true
    spiMode: true
```

The same loads from a driver as one monitor line (verified to construct; `$var` is not
expanded inside the quoted string, so the caller formats the path in):

```text
machine LoadPlatformDescriptionFromString "sd: SD.SDCard @ spi0 { imageFile: \"<path>\"; capacity: 8388608; persistent: true; spiMode: true }"
```

Load it after `include tools/renode/picocalc-machine.resc`. A relative `imageFile` was
not tried; the image must exist and equal `capacity`.

Card image: 16384 sectors, MBR at offset 446 with entry type `0xb7` (start 2, 8192
sectors) and `0xb8` (start 8194, 8000 sectors), signature `55 aa`, marker text at sector 100.

## Results

Chip select does not matter. The PL022 model never calls `FinishTransmission`, and the card
works anyway: `sdspi_init` and every read and write completed. Inference from the
disassembled `SDCard.Transmit`: the SPI state returns to waiting-for-command once a response
or data phase drains, so the reset that chip-select release would perform is not needed.

First open of `/dev/rsd0` (root via `su`, empty operator password), panel text:

```text
sd0: SD 2.0, 8192 kbytes
sd0a: partition type b7, sector 2, size 4096 kbytes
sd0b: partition type b8, sector 8194, size 4000 kbytes
```

`dd if=/dev/rsd0 bs=1k skip=50 count=1` printed `SENTINEL-SECT100` (sector 100 of the image).

Raw write: `echo WRITTEN-BY-DISCOBSD | dd of=/dev/rsd0a bs=1k seek=100 conv=sync`
reported `1+0 records out`. With `persistent: true` the host file held the bytes at offset
103424 (sector 2 + 200 sectors) immediately, before `sync`. Read-back through
`dd if=/dev/rsd0a bs=1k skip=100 count=1` printed `WRITTEN-BY-DISCOBSD`.

`persistent: false`: the kernel read back its write, the host file stayed all zero,
and the content survived a reboot, so the card holds a private copy for the process.
A gate that reads the file on the host needs `persistent: true`.

`reboot` as root: the kernel banner printed again and `login:` returned (about 40 virtual
seconds). The next first open re-initialized the card (`sd0: SD 2.0` again) and
`dd if=/dev/rsd0a bs=1k skip=100 count=1` printed `WRITTEN-BY-DISCOBSD`; the host file still
held it.

The Renode log held no `sd:` or `spi0:` warnings in any run. Card detect (GP22) has no
model; the pad pull-up reads constant, so the replaced-card path never fires.

## Buffered writes

The model writes the image file through a buffered stream, so the host file
can lag what the kernel wrote. In the crash-dump run of
`check-renode-picocalc-dump` the host file did not show the dump's header
sector after `reboot -d` and the next boot's login prompt, while `savecore`
read that header through the kernel moments later; the gate therefore reads
the card only after Renode exits. The raw write above was visible at once,
so how much the stream holds back depends on the writes that follow it
(inferred from the two runs, not traced in the model).

## Limits found

- A card above 2 GiB (4 GiB sparse tested) is inconsistent: the model addresses by
  block, but the driver saw no CCS bit and printed `SD 2.0, 4194304 kbytes`
  (not `SDHC/SDXC`), so it sent byte addresses. A write meant for sector 202 landed at
  byte 52953088 (103424 x 512) of the file. Keep `capacity` below 2 GiB; no patch written.
- The model keyboard FIFO holds 31 events (`FifoSize`); typing a line longer than about
  15 characters at once drops keys (49 queued, 21 dropped observed). Type in chunks of 8
  characters and wait until `Delivered` equals `Queued`, then send `TypeKey 10`.
- `od` is absent from the PICOCALC root; `head` and `dd` exist. Show raw reads with `dd`
  to the console. Lines over 53 columns redraw as duplicate prompt lines on the panel.
- A run (boot, write, reboot, read-back) costs about 8 wall minutes with chunked typing.

## Not established

- Card replacement through GP22 (no driver for the pin in Renode).
- A relative `imageFile`, an image shorter than `capacity`, and SDHC reporting.
- Write timing and speed against hardware.
