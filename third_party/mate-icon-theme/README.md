# MATE icon theme artwork for MATEzinho

This directory holds every third-party artwork byte that the MATEzinho desktop
uses, together with the license and attribution that govern it. The
BSD-licensed shell (`usr.bin/matezinho/`) and its tools (`tools/matezinho/`)
contain no artwork. `tools/matezinho/storage.py` reads the PNGs here at build
time and writes the converted icons into an artwork volume outside the tree.

## Upstream

- Project: MATE icon theme, https://github.com/mate-desktop/mate-icon-theme
- Revision: commit 15df93ae1d5fbea067eb3d9d5d2a8b3b4a5cbec4 (upstream
  committer date 2026-06-19; not a tagged release)
- Retrieval: https://github.com/mate-desktop/mate-icon-theme/tree/15df93ae1d5fbea067eb3d9d5d2a8b3b4a5cbec4.
  The branch records no retrieval date; the files entered this tree in the
  commit dated 2026-11-12. Every file under `upstream/` has the Git blob
  identifier that the upstream tree at that revision lists for the same path.
- Authors: `upstream/AUTHORS`; per-work titles and creators remain in the SVG
  metadata under `upstream/src/`.

## License

`upstream/COPYING` declares the whole package available under either the GNU
LGPL version 3 or Creative Commons Attribution-ShareAlike 3.0. This tree
distributes the artwork under the second option, CC BY-SA 3.0 Unported
(SPDX `CC-BY-SA-3.0`), whose exact legal text is `LICENSES/CC-BY-SA-3.0.txt`
(byte-identical to https://creativecommons.org/licenses/by-sa/3.0/legalcode.txt).
The LGPL text is not included, because this tree does not exercise that
option.

Attribution line: "MATE icon theme, MATE and GNOME contributors (upstream
AUTHORS), CC BY-SA 3.0; RGB565 conversion by MATEzinho."

Only icon artwork is imported: no Mint CSS, SCSS, theme code or logos, and
no MATE implementation code. MATEzinho is independent of MATE and Linux Mint.

`provenance.json`, this README and `derived/screenshots/README.md` are
records written for this repository and fall under its BSD 3-Clause
`LICENSE` at the repository root. They contain no artwork.

## Changes and derived works

The upstream PNGs and SVGs are unchanged. `tools/matezinho/storage.py`
derives the artwork volume from the nine PNGs. It flattens alpha onto the
RGB565 color 0x2124, quantizes each pixel to RGB565, and stores each icon as
a padded 2048-byte record after a 1024-byte header. Flattening and
quantization modify the icons, so the converted records are an Adaptation
under CC BY-SA 3.0 section 1(a). Section 4(b) permits distributing an Adaptation
only under this license, a later version of it, or a compatible license;
this tree uses CC BY-SA 3.0 itself, so every generated volume and every
capture that shows the converted icons is CC BY-SA 3.0. The conversion
program itself stays BSD 3-Clause; its output does not.

No converted icon data is committed. The two captures under
`derived/screenshots/` reproduce the converted icons and are distributed under
CC BY-SA 3.0.

## Regeneration

From the repository root, with Pillow installed and `PYTHON` set:

    "$PYTHON" tools/matezinho/storage.py artwork16.mzar --profile rom16

The builder checks every `provenance.json` hash before converting. It refuses
an existing output, and it writes `artwork16.mzar.artwork/`, a copy of this
whole directory, beside the volume. `bmake MACHINE=rp2040 check-matezinho-host`
also checks that every file here has a row in the table below whose SHA-256
matches.

## What must carry this license

- An artwork volume (`*.mzar`, or an rom16, sd32 or sd32gb image built with
  `--kind assets`) contains the converted icons. Distribute it with its
  `.artwork` companion directory, which `storage.py` creates. Writing the
  volume to an SD card copies only the volume, not the companion, so a card
  handed on needs this directory supplied with it.
- A screenshot or capture that shows the icons, including the files under
  `derived/screenshots/`.
- Root images (`sdcard*.img`), flash images and UF2 files contain no artwork.
  The `matezinho` closure in `distrib/rp2040/mi.rp2040` installs only
  `/usr/bin/matezinho`, which reads icons from the volume at run time, and no
  manifest installs this directory.

## Files

| File | Upstream path at 15df93ae | License (SPDX) | SHA-256 |
| --- | --- | --- | --- |
| `LICENSES/CC-BY-SA-3.0.txt` | creativecommons.org/licenses/by-sa/3.0/legalcode.txt | license text | `3f941b3b89cf7b8370ceb83cc76d2120d471b58735d8ca60238a751a48d7f72f` |
| `derived/screenshots/README.md` | none (record written for this tree) | BSD-3-Clause (repository LICENSE) | `0ad097744f1271ba93df8c82bebb983369b5a168b0c41a0c0106c5f25aea20cb` |
| `derived/screenshots/matezinho-desktop.png` | none (capture of converted icons) | CC-BY-SA-3.0 | `b56b64d6c31392e87c7fc171d1f935c3f0b94b222f90dac7fe2a691542727962` |
| `derived/screenshots/matezinho-renode.png` | none (capture of converted icons) | CC-BY-SA-3.0 | `bb8e6ba2ac1c76ef962bb3462d9ef3ca98fc2d5a99a2ae0d35b37a48b33d85bb` |
| `provenance.json` | none (record written for this tree) | BSD-3-Clause (repository LICENSE) | `9c2464aab6a64143581a3cf1e61a7828fb68559e2c2051868c185693c08b82b8` |
| `upstream/AUTHORS` | AUTHORS | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `7f11632490358a747e45f639ab0df5dda8520bcc5bade09f2efb1c343a7fb29c` |
| `upstream/COPYING` | COPYING | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `31138451a6046a755497578b2220e3dbdd998774bea9ec037214846ad3a52035` |
| `upstream/mate/24x24/actions/Makefile.am` | mate/24x24/actions/Makefile.am | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `cc7ec1124abd446edd94e71f769b77a79d95c3fb19b0f6804c3d269097a7ac07` |
| `upstream/mate/24x24/actions/help-contents.png` | mate/24x24/actions/help-contents.png | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `5503ece1c2b524da853fc9764fc8cc1f5369971eda712014a0c63c35fca2fcb8` |
| `upstream/mate/24x24/apps/Makefile.am` | mate/24x24/apps/Makefile.am | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `91447dfbcb77fd876d901cee3b853d83306975a1e86f5abacaac80c917234024` |
| `upstream/mate/24x24/apps/accessories-calculator.png` | mate/24x24/apps/accessories-calculator.png | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `445dd8ba7ae60669318d5a0d9375775507038b9812bf0871683e8d0cf168ea1b` |
| `upstream/mate/24x24/apps/accessories-text-editor.png` | mate/24x24/apps/accessories-text-editor.png | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `b3aa0be5e2a250ea155a4d9ad7afc07e4388f5459bb3b178dc2221560dab60fd` |
| `upstream/mate/24x24/apps/utilities-system-monitor.png` | mate/24x24/apps/utilities-system-monitor.png | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `a5bb30c2202823f7f222ce1cd9329085571e18fad56af59a4485deb4360c4f01` |
| `upstream/mate/24x24/apps/utilities-terminal.png` | mate/24x24/apps/utilities-terminal.png | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `75bbc64beb4d425437db3aa081a6a4f69f78d46305c07b4adcc56fa3692e97e7` |
| `upstream/mate/24x24/categories/Makefile.am` | mate/24x24/categories/Makefile.am | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `9f10b942beba18cb2ba8952bb429b35241a16480aa67fe58fd9db17d518d3550` |
| `upstream/mate/24x24/categories/preferences-system.png` | mate/24x24/categories/preferences-system.png | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `daa306645c9bb455c4236490c4410aabff65369785bca593074b21e26d40c18a` |
| `upstream/mate/24x24/devices/Makefile.am` | mate/24x24/devices/Makefile.am | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `0b76a5e437bf91c2d810398a569b9cd78e23d0aed4c94caadd46438b5b1a3c68` |
| `upstream/mate/24x24/devices/drive-harddisk.png` | mate/24x24/devices/drive-harddisk.png | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `5ed133011bf70bdace8c7ce31ad68eaad774eeed58e747c172a0829ea7aa9bfe` |
| `upstream/mate/24x24/places/Makefile.am` | mate/24x24/places/Makefile.am | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `3ae6f2b0690af037bb24ef353338d371d92b66fffd3fd0f7c74b401680ce3e87` |
| `upstream/mate/24x24/places/folder.png` | mate/24x24/places/folder.png | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `65f112134722adc0a1b38646a773f215997bd35848c86e764ae911ecb8ea69b7` |
| `upstream/mate/24x24/status/Makefile.am` | mate/24x24/status/Makefile.am | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `8f5caf7e5162c77897ee3b62fb99c42c167a31a938962a674db33baa79086c62` |
| `upstream/mate/24x24/status/appointment-soon.png` | mate/24x24/status/appointment-soon.png | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `c0db5fa611e316ef31cb2e7bf0c6daea1d64e61ac993b0236b29585e53a0a22b` |
| `upstream/src/accessories-calculator.svg` | src/accessories-calculator.svg | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `5a57357f6cb7c1f4732d43925e98fb28238a356d7096b704b39220b6aeaf0d58` |
| `upstream/src/accessories-text-editor.svg` | src/accessories-text-editor.svg | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `fb0fd868afdd16c4cbe82b5c740260212e49db2af112d3927174a68c44597e93` |
| `upstream/src/clocks.svg` | src/clocks.svg | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `af93c89681dce203f4c51f3700008e8bfa1c1f315934a4d133d325dc1e6ce5b0` |
| `upstream/src/drive-harddisk.svg` | src/drive-harddisk.svg | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `5ea41c58a728efa9cebd181657c60e9b9f77c22913ad328dbb38cf16e09bcdf5` |
| `upstream/src/folders.svg` | src/folders.svg | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `d2d441df9dd93ba9324caab13a42ba6435cb7d5181a0fe93e890d5baad1be2b6` |
| `upstream/src/help-contents.svg` | src/help-contents.svg | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `51641fc586ff4da1005b5bc93bac1bfb07756993ccc0e0ae04bd6e183c96d493` |
| `upstream/src/preferences-system.svg` | src/preferences-system.svg | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `000ec818ab75b47b770dad803607e9e962ff0d444171e319dd56464871a1bf68` |
| `upstream/src/terminals.svg` | src/terminals.svg | CC-BY-SA-3.0 (upstream offers GNU LGPL v3 or CC BY-SA 3.0) | `a4221c80a850d80733d10684d536d1779a6f444c23336ae0455242d0fccf6e42` |
