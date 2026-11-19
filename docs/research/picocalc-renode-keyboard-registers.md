# Renode keyboard model: backlight conditioning for the register sysctls

The Renode PicoCalc keyboard model (`tools/renode/picocalc/picocalc_keyboard.cs`)
conditions backlight writes as the vendor firmware does. The same change is
kept as `picocalc-renode-keyboard-backlight.patch`, a diff against the model
before it carried the conditioning.

The model already serves the registers the `machdep.picocalc` sysctls use:
0x05 and 0x0A (backlights, read and write), 0x0B (battery, set from the
monitor with `SetBattery`) and 0x0E (power-off countdown, floored at 6).
Before the change it stored a backlight write verbatim. The vendor firmware does not:
`lcd_backlight_update` in `backlight.ino` rounds the LCD level down to a
multiple of 16 and keeps it within 16..240, and `kbd_backlight_update`
rounds the keyboard level down to a multiple of 32 and sets 0 below 32 or
above 240. A guest that writes `machdep.picocalc.lcd_backlight=88` reads
back 80 from the firmware and 88 from the model, so a console check of the
write-then-read round trip passes against the model for a reason the board
would not repeat. The model now applies the same conditioning, so
the model and `tests/picocalc/kbdreq_test.c` (which models the firmware the
same way) agree.

Evidence class: `check-renode-picocalc` boots the PICOCALC kernel on the model
and asserts no backlight value. The initial LCD level is 32 (`reg_init()` in
`reg.ino` sets `LCD_BACKLIGHT_STEP * 2`), and `tests/picocalc/kbdreq_test.c`
models the same conditioning and initial values.
