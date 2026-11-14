# MATEzinho screenshot provenance

matezinho-desktop.png is the original C renderer executed with
qemu-arm -cpu cortex-m0 at 320x320, exported from RGB565 to PPM, then
losslessly encoded as PNG. The native development backend produces identical
pixels for the same actions and asset volume. It is a functional application
capture, not a generated design illustration or physical-panel photograph.

Artwork: MATE icon theme, MATE/GNOME contributors (the complete upstream
AUTHORS retained in ../../upstream/AUTHORS), CC BY-SA 3.0. Native 24x24 icons
are alpha-flattened and converted to RGB565 by MATEzinho. Both captures
reproduce those converted icons, so each PNG is distributed as a whole under
CC BY-SA 3.0 (../../LICENSES/CC-BY-SA-3.0.txt). The shell code that drew the
surrounding pixels is BSD 3-Clause, and the Misc-Fixed font is public domain.
See ../../README.md and ../../provenance.json for sources, revision,
modifications and per-file hashes.

matezinho-renode.png is a separate whole-system capture from Renode 1.17.0,
the real PICOCALC kernel and the SPI0 read-only file-backed SD model. The
Files card agrees with the application reference within RGB666 quantization;
the model reported 75 SD sector reads. Physical panel colorimetry, latency,
power and behavior on a real board are not established by this capture.
