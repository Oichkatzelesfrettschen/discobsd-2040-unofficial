# Renode keyboard model: backlight conditioning for the register sysctls

The Renode PicoCalc machine (`tools/renode/picocalc/picocalc_keyboard.cs`
and the machine scripts) lives on the branch `picocalc/test-pipeline` and is
absent from `main`, so the change below ships as a patch against that
branch's file. Apply it there with
`git apply docs/research/picocalc-renode-keyboard-backlight.patch`, run
from a checkout of that branch.

The model already serves the registers the `machdep.picocalc` sysctls use:
0x05 and 0x0A (backlights, read and write), 0x0B (battery, set from the
monitor with `SetBattery`) and 0x0E (power-off countdown, floored at 6).
It stores a backlight write verbatim. The vendor firmware does not:
`lcd_backlight_update` in `backlight.ino` rounds the LCD level down to a
multiple of 16 and keeps it within 16..240, and `kbd_backlight_update`
rounds the keyboard level down to a multiple of 32 and sets 0 below 32 or
above 240. A guest that writes `machdep.picocalc.lcd_backlight=88` reads
back 80 from the firmware and 88 from the model, so a console check of the
write-then-read round trip passes against the model for a reason the board
would not repeat. The patch makes the model apply the same conditioning, so
the model and `tests/picocalc/kbdreq_test.c` (which models the firmware the
same way) agree.

Evidence class: the patch is not applied or run here, because the machine
is not on this branch. A Renode check of the sysctls (boot the PICOCALC
kernel, `sysctl machdep.picocalc.battery`, a backlight write and a
`poweroff` write, then the monitor's `BacklightLevel`, `KeyBacklightLevel`
and `PowerOffCountdown` properties) belongs with that branch's
`check-renode-picocalc` once both land.
